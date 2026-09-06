#pragma once

#include "snow/image/document.h"
#include "snow/image/io.h"
#include "snow/image/raster.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace snow::image {

struct DecodeLimits final {
    std::uint32_t maximum_width = 1U << 20U;
    std::uint32_t maximum_height = 1U << 20U;
    std::uint64_t maximum_pixels = std::uint64_t{1} << 32U;
    std::uint32_t maximum_frames = 10'000;
    std::uint64_t maximum_metadata_bytes = std::uint64_t{64} << 20U;
    std::uint64_t maximum_owned_output_bytes = std::uint64_t{1} << 30U;
    std::uint64_t maximum_working_bytes = std::uint64_t{512} << 20U;
    std::uint64_t maximum_input_bytes = std::uint64_t{8} << 30U;
    std::uint64_t maximum_deep_samples = std::uint64_t{1} << 30U;
};

enum class OrientationPolicy : std::uint8_t { preserve, apply };
enum class RasterLayoutPolicy : std::uint8_t { packed, native };

struct DecodeOptions final {
    DecodeLimits limits;
    OrientationPolicy orientation = OrientationPolicy::preserve;
    RasterLayoutPolicy raster_layout = RasterLayoutPolicy::packed;
    std::optional<PixelFormat> output_format;
    std::optional<std::uint32_t> frame_index;
    std::optional<std::uint32_t> maximum_extent;
    bool preserve_metadata = true;
};

struct EncodeOptions final {
    Format format = Format::unknown;
    int quality = 90;
    int effort = 7;
    int lossless_effort = 6;
    bool lossless = false;
    bool preserve_metadata = true;
    bool progressive = false;
    bool interlaced = false;
    int compression_level = 6;
    // JPEG only. Absence selects the codec's quality-aware automatic mode.
    // Grayscale input always resolves to ChromaSubsampling::none.
    std::optional<ChromaSubsampling> chroma_subsampling;
    // Callers may provide a pixel-verified document-wide classification to
    // avoid repeating a full alpha scan inside encoders.
    std::optional<AlphaContent> verified_alpha_content;
};

enum class RasterEncodeRoute : std::uint8_t {
    materialized,
    native,
};

// Canonicalizes codec options against the advertised encoder contract. This
// is suitable for resource plans, transport messages, and cache keys as well
// as the final encode call.
[[nodiscard]] SNOW_IMAGE_API Result<EncodeOptions>
normalize_encode_options(const EncoderInfo& encoder, const EncodeOptions& options);
[[nodiscard]] SNOW_IMAGE_API Result<ChromaSubsampling>
resolve_jpeg_chroma_subsampling(const EncodeOptions& normalized_options, bool grayscale);

enum class PixelRoundTrip : std::uint8_t { exact, codec_artifact };

struct EncodedFrameExtent final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const EncodedFrameExtent&, const EncodedFrameExtent&) = default;
};

// Describes the bytes finalized by the native encoder.  The receipt is built
// only after the encoder has completed and the common output wrapper has
// flushed the sink, so callers do not need to reopen the artifact to learn
// its structure.
struct EncodedArtifactReceipt final {
    Format format = Format::unknown;
    DocumentKind document_kind = DocumentKind::raster;
    std::uint32_t canvas_width = 0;
    std::uint32_t canvas_height = 0;
    std::uint32_t emitted_frame_count = 0;
    std::vector<EncodedFrameExtent> emitted_frame_extents;
    // Present for JPEG artifacts. ChromaSubsampling::none denotes grayscale.
    std::optional<ChromaSubsampling> jpeg_chroma_subsampling;
    bool encoder_finalized_and_sink_flushed = false;

    friend bool operator==(const EncodedArtifactReceipt&, const EncodedArtifactReceipt&) = default;
};

struct EncodeResult final {
    std::uint64_t bytes_written = 0;
    PixelRoundTrip round_trip = PixelRoundTrip::codec_artifact;
    EncodedArtifactReceipt receipt;
};

[[nodiscard]] SNOW_IMAGE_API std::string_view compression_backend_version(Format format) noexcept;

class SNOW_IMAGE_API PixelSink {
  public:
    virtual ~PixelSink() = default;
    [[nodiscard]] virtual Result<void> begin(const DocumentInfo& document) = 0;
    [[nodiscard]] virtual Result<void> begin_frame(std::uint32_t frame_index,
                                                   const FrameInfo& frame) = 0;
    // A sink may expose stable writable storage for the complete frame so codecs
    // with buffer-based APIs can decode without an intermediate owning image.
    // An empty span means that row callbacks must be used instead.
    [[nodiscard]] virtual std::span<std::byte>
    frame_storage(std::uint32_t frame_index, std::size_t row_stride, std::size_t byte_size) {
        (void)frame_index;
        (void)row_stride;
        (void)byte_size;
        return {};
    }
    [[nodiscard]] virtual Result<void> write_rows(std::uint32_t first_row, std::uint32_t row_count,
                                                  std::size_t row_stride,
                                                  std::span<const std::byte> pixels) = 0;
    [[nodiscard]] virtual Result<void> end_frame(std::uint32_t frame_index) = 0;
    [[nodiscard]] virtual Result<void> end() = 0;
};

} // namespace snow::image
