#include "snow/image/resource_estimate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace snow::image {
namespace {

Result<std::uint64_t> checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return Status::error(ErrorCode::limit_exceeded,
                             "The resource estimate exceeds addressable limits.");
    }
    return left + right;
}

Result<std::uint64_t> checked_multiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return Status::error(ErrorCode::limit_exceeded,
                             "The resource estimate exceeds addressable limits.");
    }
    return left * right;
}

Result<std::uint64_t> frame_bytes(std::uint32_t width, std::uint32_t height, PixelFormat format) {
    auto bytes_per_pixel = format.bytes_per_pixel();
    if (!bytes_per_pixel)
        return bytes_per_pixel.error();
    auto pixels = checked_multiply(width, height);
    if (!pixels)
        return pixels.error();
    return checked_multiply(pixels.value(), bytes_per_pixel.value());
}

Result<std::uint64_t> document_bytes(const DocumentInfo& document) {
    std::uint64_t total = 0;
    for (const FrameInfo& frame : document.frames) {
        auto bytes = frame_bytes(frame.width, frame.height, frame.native_format);
        if (!bytes)
            return bytes.error();
        auto sum = checked_add(total, bytes.value());
        if (!sum)
            return sum.error();
        total = sum.value();
    }
    return total;
}

DocumentInfo document_info(const Document& document) {
    DocumentInfo info;
    info.format = document.format;
    info.canvas_width = document.canvas_width;
    info.canvas_height = document.canvas_height;
    info.loop_count = document.loop_count;
    info.color = document.color;
    info.frames.reserve(document.frames.size());
    for (const Frame& frame : document.frames) {
        info.frames.push_back({frame.image.width(), frame.image.height(), frame.x, frame.y,
                               frame.duration, frame.image.format(),
                               frame.image.format().alpha != AlphaMode::none, frame.cursor_hotspot,
                               frame.color, frame.metadata, frame.blend, frame.disposal});
    }
    return info;
}

Result<std::uint64_t> scaled(std::uint64_t bytes, std::uint64_t factor, std::uint64_t headroom) {
    auto product = checked_multiply(bytes, factor);
    if (!product)
        return product.error();
    return checked_add(product.value(), headroom);
}

} // namespace

Result<std::uint64_t> ResourceEstimate::total_bytes() const {
    auto resident = checked_add(private_memory_bytes, mapped_bytes);
    if (!resident)
        return resident.error();
    return checked_add(resident.value(), output_bytes);
}

Result<ResourceEstimate> estimate_decode_resources(const DocumentInfo& document,
                                                   const DecodeOptions& options) {
    auto output = document_bytes(document);
    if (!output)
        return output.error();
    if (output.value() > options.limits.maximum_owned_output_bytes) {
        return Status::error(ErrorCode::limit_exceeded,
                             "Decoded output exceeds the configured resource limit.");
    }
    auto private_memory = scaled(output.value(), 2, std::uint64_t{8} << 20U);
    if (!private_memory)
        return private_memory.error();
    return ResourceEstimate{private_memory.value(), 0, output.value()};
}

Result<ResourceEstimate> estimate_transform_resources(const DocumentInfo& document,
                                                      const TransformOptions& options) {
    std::uint64_t output = 0;
    std::uint64_t row_working = 0;
    const std::size_t frame_count = options.animation_policy == AnimationPolicy::first_frame
                                        ? std::min<std::size_t>(1, document.frames.size())
                                        : document.frames.size();
    for (std::size_t index = 0; index < frame_count; ++index) {
        const FrameInfo& frame = document.frames[index];
        const std::uint32_t width = options.resize ? options.resize->width : frame.width;
        const std::uint32_t height = options.resize ? options.resize->height : frame.height;
        auto bytes = frame_bytes(width, height, frame.native_format);
        if (!bytes)
            return bytes.error();
        auto sum = checked_add(output, bytes.value());
        if (!sum)
            return sum.error();
        output = sum.value();
        auto bytes_per_pixel = frame.native_format.bytes_per_pixel();
        if (!bytes_per_pixel)
            return bytes_per_pixel.error();
        auto row = checked_multiply(std::max(frame.width, width), bytes_per_pixel.value() * 16U);
        if (!row)
            return row.error();
        std::uint64_t working = row.value();
        if (options.resize && frame.height != height) {
            const double scale = static_cast<double>(height) / frame.height;
            const double base_radius = options.resize->method == ResamplingMethod::nearest  ? 0.5
                                       : options.resize->method == ResamplingMethod::linear ? 1.0
                                                                                            : 3.0;
            const std::uint64_t ring_rows = static_cast<std::uint64_t>(
                std::max(1.0, std::ceil(base_radius / std::min(1.0, scale) * 2.0 + 1.0)));
            auto pixel_row = checked_multiply(width, sizeof(float) * 4U);
            if (!pixel_row)
                return pixel_row.error();
            auto ring = checked_multiply(pixel_row.value(), ring_rows);
            if (!ring)
                return ring.error();
            auto row_index = checked_multiply(ring_rows, 32U);
            if (!row_index)
                return row_index.error();
            auto cached = checked_add(ring.value(), row_index.value());
            if (!cached)
                return cached.error();
            const bool unbounded_cache = options.resize->maximum_worker_cache_bytes ==
                                         std::numeric_limits<std::uint64_t>::max();
            const bool streaming = !unbounded_cache && frame.height > height &&
                                   (ring_rows > kDefaultResizeMaximumCachedRows ||
                                    cached.value() > options.resize->maximum_worker_cache_bytes);
            std::uint32_t hardware = std::max(1U, std::thread::hardware_concurrency());
            std::uint32_t threads = options.resize->maximum_threads == 0
                                        ? hardware
                                        : std::min(hardware, options.resize->maximum_threads);
            threads = std::clamp<std::uint32_t>(threads, 1, height);
            std::uint64_t per_worker = 0;
            if (streaming) {
                threads = 1;
                const std::uint64_t active_rows =
                    static_cast<std::uint64_t>(std::ceil(base_radius * 2.0 + 2.0));
                auto accumulators = checked_multiply(pixel_row.value(), active_rows + 1U);
                auto coefficient_count = checked_multiply(height, ring_rows);
                if (!accumulators || !coefficient_count)
                    return !accumulators ? accumulators.error() : coefficient_count.error();
                auto coefficient_peak = checked_multiply(coefficient_count.value(), 16U);
                auto source_offsets = checked_multiply(
                    static_cast<std::uint64_t>(frame.height) + 1U, sizeof(std::size_t));
                auto target_ranges = checked_multiply(height, sizeof(int) * 2U);
                if (!coefficient_peak || !source_offsets || !target_ranges)
                    return !coefficient_peak ? coefficient_peak.error()
                           : !source_offsets ? source_offsets.error()
                                             : target_ranges.error();
                auto tables = checked_add(coefficient_peak.value(), source_offsets.value());
                if (!tables)
                    return tables.error();
                tables = checked_add(tables.value(), target_ranges.value());
                if (!tables)
                    return tables.error();
                auto combined = checked_add(accumulators.value(), tables.value());
                if (!combined)
                    return combined.error();
                per_worker = combined.value();
            } else {
                auto cached_with_output = checked_add(cached.value(), pixel_row.value());
                if (!cached_with_output)
                    return cached_with_output.error();
                per_worker = cached_with_output.value();
            }
            if (!streaming && options.resize->maximum_threads == 0 && per_worker != 0) {
                constexpr std::uint64_t kAutomaticWorkingSet = std::uint64_t{256} << 20U;
                threads = std::min<std::uint32_t>(
                    threads, static_cast<std::uint32_t>(
                                 std::max<std::uint64_t>(1, kAutomaticWorkingSet / per_worker)));
            }
            auto rings = checked_multiply(per_worker, threads);
            if (!rings)
                return rings.error();
            auto combined = checked_add(working, rings.value());
            if (!combined)
                return combined.error();
            working = combined.value();
        }
        row_working = std::max(row_working, working);
    }
    auto private_memory = checked_add(row_working, std::uint64_t{8} << 20U);
    if (!private_memory)
        return private_memory.error();
    return ResourceEstimate{private_memory.value(), output, 0};
}

Result<ResourceEstimate> estimate_export_resources(const DocumentInfo& document, Format format,
                                                   bool reduce_palette) {
    auto raw = document_bytes(document);
    if (!raw)
        return raw.error();
    const bool high_precision = format == Format::avif || format == Format::jxl;
    const std::uint64_t conversion_factor = high_precision ? 2U : 1U;
    auto mapped = checked_multiply(raw.value(), conversion_factor);
    if (!mapped)
        return mapped.error();
    auto private_memory = scaled(mapped.value(), reduce_palette ? 2U : 1U, std::uint64_t{8} << 20U);
    if (!private_memory)
        return private_memory.error();
    return ResourceEstimate{private_memory.value(), mapped.value(), 0};
}

Result<ResourceEstimate> estimate_preview_decode_resources(const DocumentInfo& document) {
    auto raw = document_bytes(document);
    if (!raw)
        return raw.error();
    auto private_memory = scaled(raw.value(), 2, std::uint64_t{8} << 20U);
    if (!private_memory)
        return private_memory.error();
    return ResourceEstimate{private_memory.value(), raw.value(), 0};
}

Result<ResourceEstimate> estimate_encode_resources(const Document& document,
                                                   const EncodeOptions& options) {
    const DocumentInfo info = document_info(document);
    auto raw = document_bytes(info);
    if (!raw)
        return raw.error();
    std::uint64_t private_factor = 4;
    std::uint64_t runtime_headroom = std::uint64_t{16} << 20U;
    std::uint64_t private_floor = 0;
    switch (options.format) {
    case Format::jpeg:
        private_factor = 0;
        runtime_headroom = (std::uint64_t{2} << 20U) + (std::uint64_t{256} << 10U);
        break;
    case Format::png:
        private_factor = 2;
        break;
    case Format::webp:
        private_factor = 6;
        runtime_headroom = std::uint64_t{32} << 20U;
        break;
    case Format::heif:
    case Format::avif:
        private_factor = 10;
        runtime_headroom = std::uint64_t{64} << 20U;
        break;
    case Format::jxl:
        private_factor = 16;
        runtime_headroom = std::uint64_t{64} << 20U;
        // libjxl has a substantial fixed encoder working set, especially in Debug
        // builds. A purely pixel-scaled estimate under-admits small images.
        private_floor = std::uint64_t{640} << 20U;
        break;
    default:
        break;
    }
    auto private_memory = scaled(raw.value(), private_factor, runtime_headroom);
    if (!private_memory)
        return private_memory.error();
    if (options.format == Format::png && !document.frames.empty()) {
        const PixelFormat& format = document.frames.front().image.format();
        const bool palette_eligible =
            format.sample_type == SampleType::unsigned_integer && format.bits_per_channel == 8 &&
            (format.channels == ChannelLayout::rgb || format.channels == ChannelLayout::rgba ||
             format.channels == ChannelLayout::bgr || format.channels == ChannelLayout::bgra);
        if (palette_eligible) {
            auto pixels = checked_multiply(document.frames.front().image.width(),
                                           document.frames.front().image.height());
            if (!pixels)
                return pixels.error();
            // The automatic palette probe owns one byte per candidate pixel and a
            // fixed lookup table, even when the 257th color rejects the conversion.
            // PaletteSlot contains a 32-bit key and two one-byte fields.  Its
            // alignment rounds the slot to eight bytes on the supported ABIs; use
            // that padded size so the plan covers the actual fixed table.
            auto table = checked_multiply(512U, 8U);
            if (!table)
                return table.error();
            auto palette = checked_multiply(256U, sizeof(std::uint32_t));
            if (!palette)
                return palette.error();
            auto extra = checked_add(pixels.value(), table.value());
            if (!extra)
                return extra.error();
            extra = checked_add(extra.value(), palette.value());
            if (!extra)
                return extra.error();
            private_memory = checked_add(private_memory.value(), extra.value());
            if (!private_memory)
                return private_memory.error();
        }
    }
    // A raw-size bound plus container overhead is conservative for the editor codecs.
    auto output = checked_add(raw.value(), std::uint64_t{1} << 20U);
    if (!output)
        return output.error();
    return ResourceEstimate{std::max(private_memory.value(), private_floor), 0, output.value()};
}

} // namespace snow::image
