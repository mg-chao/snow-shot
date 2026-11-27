#include "png_parallel.h"
#include "codec_registry.h"

#include <snow/memory/pixel_array.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <future>
#include <thread>

namespace snow::image::internal {
namespace {
using Bytes = snow::memory::PixelArray<std::uint8_t>;
constexpr std::size_t kMaxStripeBytes = 1U << 20;
constexpr std::size_t kInFlightInputBytes = 8U << 20;
constexpr std::size_t kDictionaryBytes = 32768;

#if defined(SNOW_IMAGE_TEST_PNG_STRIPES)
thread_local unsigned png_test_workers = 0;
#endif

std::uint8_t paeth(int left, int up, int corner) {
    const int a = std::abs(up - corner);
    const int b = std::abs(left - corner);
    const int c = std::abs(left + up - 2 * corner);
    return static_cast<std::uint8_t>(a <= b && a <= c ? left : (b <= c ? up : corner));
}

unsigned magnitude(int difference) {
    const auto byte = static_cast<std::uint8_t>(difference);
    return std::min<unsigned>(byte, 256U - byte);
}

// Use the PNG adaptive filter's sum of signed byte magnitudes. Ties prefer
// the first predictor, including None, just as in the sequential writer.
void filter_row(std::span<const std::uint8_t> row, std::span<const std::uint8_t> prev,
                unsigned channels, std::span<std::uint8_t> out) {
    if (std::equal(row.begin(), row.end(), prev.begin())) {
        out[0] = 2;
        std::fill(out.begin() + 1, out.end(), std::uint8_t{0});
        return;
    }
    std::array<unsigned, 5> costs{};
    for (std::size_t x = 0; x < row.size(); ++x) {
        const int value = row[x], up = prev[x];
        const int left = x >= channels ? row[x - channels] : 0;
        const int corner = x >= channels ? prev[x - channels] : 0;
        costs[0] += magnitude(value);
        costs[1] += magnitude(value - left);
        costs[2] += magnitude(value - up);
        costs[3] += magnitude(value - (left + up) / 2);
        costs[4] += magnitude(value - paeth(left, up, corner));
    }
    const auto filter =
        static_cast<std::uint8_t>(std::min_element(costs.begin(), costs.end()) - costs.begin());
    out[0] = filter;
    auto dst = out.subspan(1);
    for (std::size_t x = 0; x < row.size(); ++x) {
        const int left = x >= channels ? row[x - channels] : 0;
        const int up = prev[x];
        int predictor = 0;
        switch (filter) {
        case 1:
            predictor = left;
            break;
        case 2:
            predictor = up;
            break;
        case 3:
            predictor = (left + up) / 2;
            break;
        case 4:
            predictor = paeth(left, up, x >= channels ? prev[x - channels] : 0);
            break;
        }
        dst[x] = static_cast<std::uint8_t>(row[x] - predictor);
    }
}

struct Stripe {
    Bytes compressed;
    uLong checksum = 1;
    uInt length = 0;
};

Result<Bytes> filter_stripe(Bytes raw, std::uint32_t width, unsigned channels,
                            std::stop_token stop) {
    const auto input_stride = std::size_t(width) * 4;
    const auto stride = std::size_t(width) * channels;
    const auto rows = raw.size() / input_stride;
    Bytes previous(stride), row(stride), filtered(rows * (stride + 1));
    for (std::size_t y = 0; y < rows; ++y) {
        if (stop.stop_requested())
            return cancelled_status();
        const auto* input = raw.data() + y * input_stride;
        if (channels == 4)
            std::copy_n(input, stride, row.data());
        else
            for (std::size_t x = 0; x < width; ++x)
                std::copy_n(input + 4 * x, 3, row.data() + 3 * x);
        filter_row(row, previous, channels,
                   std::span(filtered).subspan(y * (stride + 1), stride + 1));
        row.swap(previous);
    }
    return filtered;
}

Result<Stripe> compress_stripe(Bytes raw, std::size_t prefix_rows, std::uint32_t width,
                               unsigned channels, int level, bool last, std::stop_token stop) {
    // Release raw, unpacked and predecessor rows before allocating DEFLATE's
    // window and output buffer. Peak scratch stays bounded at higher concurrency.
    auto prepared = filter_stripe(std::move(raw), width, channels, stop);
    if (!prepared)
        return prepared.error();
    auto& filtered = prepared.value();
    const auto stride = std::size_t(width) * channels;
    const auto offset = prefix_rows * (stride + 1);
    auto body = std::span(filtered).subspan(offset);
    Stripe result;
    result.length = static_cast<uInt>(body.size());
    result.checksum = adler32(1, body.data(), result.length);
    z_stream stream{};
    if (deflateInit2(&stream, level, Z_DEFLATED, -15, 8, Z_FILTERED) != Z_OK)
        return Status::error(ErrorCode::encode_failed,
                             "Could not initialize PNG stripe compression.", "zlib-ng");
    struct EndStream {
        z_stream* stream;
        ~EndStream() {
            deflateEnd(stream);
        }
    } end{&stream};
    if (prefix_rows) {
        // The first prefix row provides the Up/Paeth predecessor. Every
        // subsequent dictionary byte uses exactly the same global row filter.
        const auto dict = std::min(kDictionaryBytes, offset - (stride + 1));
        if (dict && deflateSetDictionary(&stream, filtered.data() + offset - dict,
                                         static_cast<uInt>(dict)) != Z_OK)
            return Status::error(ErrorCode::encode_failed,
                                 "Could not set the PNG stripe dictionary.", "zlib-ng");
    }
    result.compressed.resize(deflateBound(&stream, result.length) + 64);
    stream.next_in = body.data();
    stream.avail_in = result.length;
    stream.next_out = result.compressed.data();
    stream.avail_out = static_cast<uInt>(result.compressed.size());
    // Z_SYNC_FLUSH ends each nonfinal piece at a byte boundary in the one
    // continuous raw DEFLATE stream read by the PNG decoder.
    const int status = deflate(&stream, last ? Z_FINISH : Z_SYNC_FLUSH);
    if (status != (last ? Z_STREAM_END : Z_OK) || stream.avail_in || !stream.avail_out)
        return Status::error(ErrorCode::encode_failed, "Could not finish PNG stripe compression.",
                             "zlib-ng");
    result.compressed.resize(stream.total_out);
    return result;
}
} // namespace

#if defined(SNOW_IMAGE_TEST_PNG_STRIPES)
void set_png_worker_count_for_testing(unsigned workers) {
    png_test_workers = workers;
}
#endif

unsigned parallel_png_workers(const RasterFrameDescriptor& frame, const EncodeOptions& options) {
    if (frame.layout.planes.front().format != kRgba8 || options.interlaced ||
        options.compression_level <= 0 || frame.width > 32768 ||
        std::uint64_t(frame.width) * frame.height < (4U << 20))
        return 0;
#if defined(SNOW_IMAGE_TEST_PNG_STRIPES)
    if (png_test_workers != 0)
        return std::min(png_test_workers, 12U);
#endif
    return std::min(std::thread::hardware_concurrency(), 12U);
}

Result<void> write_parallel_png_data(const RasterSource& source, bool opaque, int level,
                                     unsigned workers, const PngDataWriter& write,
                                     std::stop_token stop) {
    const auto& frame = source.descriptor().frames.front();
    const auto input_stride = std::size_t(frame.width) * 4;
    const auto channels = opaque ? 3U : 4U;
    const auto filtered_stride = std::size_t(frame.width) * channels + 1;
    const auto stripe_bytes = std::min(kMaxStripeBytes, kInFlightInputBytes / workers);
    const auto rows_per_stripe = static_cast<std::uint32_t>(stripe_bytes / input_stride);
    const auto history_rows = (kDictionaryBytes + filtered_stride - 1) / filtered_stride + 1;
    // A PNG IDAT sequence contains exactly one RFC 1950 wrapper. All worker
    // streams are raw. Its Adler-32 covers the concatenated filtered rows.
    const std::uint8_t flags = level <= 1 ? 0x01 : level <= 5 ? 0x5e : level == 6 ? 0x9c : 0xda;
    const std::array<std::uint8_t, 2> header{0x78, flags};
    auto status = write(header);
    if (!status)
        return status;
    uLong checksum = 1;
    std::deque<std::future<Result<Stripe>>> pending;
    Bytes history;
    std::uint32_t next = 0;
    while (next < frame.height || !pending.empty()) {
        while (next < frame.height && pending.size() < workers) {
            if (stop.stop_requested())
                return cancelled_status();
            const auto count = std::min(rows_per_stripe, frame.height - next);
            const auto prefix = history.size();
            Bytes raw = std::move(history);
            raw.resize(prefix + count * input_stride);
            auto read =
                source.read_rows(0, 0, next, count, input_stride,
                                 std::as_writable_bytes(std::span(raw).subspan(prefix)), stop);
            if (!read)
                return read;
            next += count;
            if (next < frame.height) {
                const auto tail = std::min(raw.size(), history_rows * input_stride);
                history.assign(raw.end() - static_cast<std::ptrdiff_t>(tail), raw.end());
            }
            pending.push_back(std::async(std::launch::async | std::launch::deferred,
                                         compress_stripe, std::move(raw), prefix / input_stride,
                                         frame.width, channels, level, next == frame.height, stop));
        }
        auto compressed = pending.front().get();
        pending.pop_front();
        if (!compressed)
            return compressed.error();
        if (stop.stop_requested())
            return cancelled_status();
        const auto& stripe = compressed.value();
        checksum = adler32_combine(checksum, stripe.checksum, static_cast<z_off_t>(stripe.length));
        status = write(stripe.compressed);
        if (!status)
            return status;
    }
    const std::array<std::uint8_t, 4> trailer{
        static_cast<std::uint8_t>(checksum >> 24), static_cast<std::uint8_t>(checksum >> 16),
        static_cast<std::uint8_t>(checksum >> 8), static_cast<std::uint8_t>(checksum)};
    return write(trailer);
}
} // namespace snow::image::internal
