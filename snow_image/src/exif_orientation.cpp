#include "exif_orientation.h"

#include "codec_registry.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace snow::image::internal {
namespace {

struct ExifView final {
    std::span<const std::byte> bytes;
    std::size_t base = 0;
    bool little_endian = true;
};

std::optional<ExifView> tiff_view(std::span<const std::byte> exif) noexcept {
    std::size_t base = 0;
    if (exif.size() >= 6 && std::memcmp(exif.data(), "Exif\0\0", 6) == 0)
        base = 6;
    if (exif.size() - std::min(exif.size(), base) < 8)
        return std::nullopt;
    const auto byte = [&](std::size_t offset) {
        return std::to_integer<std::uint8_t>(exif[base + offset]);
    };
    const bool little = byte(0) == 'I' && byte(1) == 'I';
    const bool big = byte(0) == 'M' && byte(1) == 'M';
    if (!little && !big)
        return std::nullopt;
    const std::uint16_t magic = little ? static_cast<std::uint16_t>(byte(2) | (byte(3) << 8U))
                                       : static_cast<std::uint16_t>((byte(2) << 8U) | byte(3));
    if (magic != 42)
        return std::nullopt;
    return ExifView{exif, base, little};
}

std::optional<std::uint16_t> read_u16(const ExifView& view, std::size_t offset) noexcept {
    if (offset > view.bytes.size() - view.base || view.bytes.size() - view.base - offset < 2)
        return std::nullopt;
    const auto a = std::to_integer<std::uint8_t>(view.bytes[view.base + offset]);
    const auto b = std::to_integer<std::uint8_t>(view.bytes[view.base + offset + 1]);
    return view.little_endian ? static_cast<std::uint16_t>(a | (b << 8U))
                              : static_cast<std::uint16_t>((a << 8U) | b);
}

std::optional<std::uint32_t> read_u32(const ExifView& view, std::size_t offset) noexcept {
    if (offset > view.bytes.size() - view.base || view.bytes.size() - view.base - offset < 4)
        return std::nullopt;
    std::uint32_t value = 0;
    if (view.little_endian) {
        for (unsigned index = 0; index < 4; ++index) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(view.bytes[view.base + offset + index]))
                     << (index * 8U);
        }
    } else {
        for (unsigned index = 0; index < 4; ++index) {
            value = (value << 8U) |
                    std::to_integer<std::uint8_t>(view.bytes[view.base + offset + index]);
        }
    }
    return value;
}

std::optional<std::size_t> orientation_value_offset(const ExifView& view) noexcept {
    const std::optional<std::uint32_t> ifd = read_u32(view, 4);
    if (!ifd)
        return std::nullopt;
    const std::size_t offset = *ifd;
    const std::optional<std::uint16_t> count = read_u16(view, offset);
    if (!count)
        return std::nullopt;
    if (*count > (std::numeric_limits<std::size_t>::max() - offset - 2U) / 12U)
        return std::nullopt;
    for (std::size_t index = 0; index < *count; ++index) {
        const std::size_t entry = offset + 2U + index * 12U;
        const auto tag = read_u16(view, entry);
        const auto type = read_u16(view, entry + 2U);
        const auto values = read_u32(view, entry + 4U);
        if (!tag || !type || !values)
            return std::nullopt;
        if (*tag == 0x0112 && *type == 3 && *values == 1 && read_u16(view, entry + 8U))
            return view.base + entry + 8U;
    }
    return std::nullopt;
}

Result<Image> oriented_image(const Image& source, Orientation orientation, std::stop_token stop) {
    const bool swapped = static_cast<std::uint8_t>(orientation) >= 5;
    const std::uint32_t target_width = swapped ? source.height() : source.width();
    const std::uint32_t target_height = swapped ? source.width() : source.height();
    Result<std::size_t> pixel_bytes = source.format().bytes_per_pixel();
    if (!pixel_bytes)
        return pixel_bytes.error();
    Result<MutableImage> allocated =
        MutableImage::allocate(target_width, target_height, source.format());
    if (!allocated)
        return allocated.error();
    MutableImage target = std::move(allocated).value();
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        if (stop.stop_requested())
            return cancelled_status();
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            std::uint32_t dx = x;
            std::uint32_t dy = y;
            switch (orientation) {
            case Orientation::identity:
                break;
            case Orientation::mirror_horizontal:
                dx = source.width() - 1U - x;
                break;
            case Orientation::rotate_180:
                dx = source.width() - 1U - x;
                dy = source.height() - 1U - y;
                break;
            case Orientation::mirror_vertical:
                dy = source.height() - 1U - y;
                break;
            case Orientation::mirror_horizontal_rotate_270:
                dx = y;
                dy = x;
                break;
            case Orientation::rotate_90:
                dx = source.height() - 1U - y;
                dy = x;
                break;
            case Orientation::mirror_horizontal_rotate_90:
                dx = source.height() - 1U - y;
                dy = source.width() - 1U - x;
                break;
            case Orientation::rotate_270:
                dx = y;
                dy = source.width() - 1U - x;
                break;
            }
            const std::byte* input = source.pixels().data() +
                                     static_cast<std::size_t>(y) * source.row_stride() +
                                     static_cast<std::size_t>(x) * pixel_bytes.value();
            std::byte* output = target.pixels().data() +
                                static_cast<std::size_t>(dy) * target.row_stride() +
                                static_cast<std::size_t>(dx) * pixel_bytes.value();
            std::memcpy(output, input, pixel_bytes.value());
        }
    }
    return std::move(target).freeze();
}

} // namespace

std::optional<Orientation> parse_exif_orientation(std::span<const std::byte> exif) noexcept {
    const std::optional<ExifView> view = tiff_view(exif);
    if (!view)
        return std::nullopt;
    const std::optional<std::size_t> offset = orientation_value_offset(*view);
    if (!offset)
        return std::nullopt;
    const auto a = std::to_integer<std::uint8_t>(exif[*offset]);
    const auto b = std::to_integer<std::uint8_t>(exif[*offset + 1U]);
    const std::uint16_t value = view->little_endian ? static_cast<std::uint16_t>(a | (b << 8U))
                                                    : static_cast<std::uint16_t>((a << 8U) | b);
    if (value < 1 || value > 8)
        return std::nullopt;
    return static_cast<Orientation>(value);
}

bool rewrite_exif_orientation(std::vector<std::byte>* exif, Orientation orientation) noexcept {
    if (!exif)
        return false;
    const std::optional<ExifView> view = tiff_view(*exif);
    if (!view)
        return false;
    const std::optional<std::size_t> offset = orientation_value_offset(*view);
    if (!offset)
        return false;
    const std::uint16_t value = static_cast<std::uint16_t>(orientation);
    (*exif)[*offset] = static_cast<std::byte>(view->little_endian ? value & 0xffU : value >> 8U);
    (*exif)[*offset + 1U] =
        static_cast<std::byte>(view->little_endian ? value >> 8U : value & 0xffU);
    return true;
}

Result<void> apply_orientation(Document* document, Orientation orientation, std::stop_token stop) {
    if (!document) {
        return Status::error(ErrorCode::invalid_argument, "The orientation destination is empty.");
    }
    if (orientation != Orientation::identity) {
        for (Frame& frame : document->frames) {
            Result<Image> image = oriented_image(frame.image, orientation, stop);
            if (!image)
                return image.error();
            frame.image = std::move(image).value();
            frame.x = 0;
            frame.y = 0;
        }
        if (static_cast<std::uint8_t>(orientation) >= 5)
            std::swap(document->canvas_width, document->canvas_height);
    }
    document->metadata.orientation = Orientation::identity;
    (void)rewrite_exif_orientation(&document->metadata.exif, Orientation::identity);
    for (Frame& frame : document->frames) {
        frame.metadata.orientation = Orientation::identity;
        (void)rewrite_exif_orientation(&frame.metadata.exif, Orientation::identity);
    }
    return {};
}

} // namespace snow::image::internal
