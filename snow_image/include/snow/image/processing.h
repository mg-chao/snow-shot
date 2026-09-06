#pragma once

#include "snow/image/document.h"
#include "snow/image/export.h"

#include <cstdint>
#include <array>
#include <limits>
#include <optional>
#include <stop_token>

namespace snow::image {

class PixelSink;

enum class AnimationPolicy : std::uint8_t { first_frame, preserve };

enum class ResamplingMethod : std::uint8_t {
    nearest,
    linear,
    lanczos3,
};

inline constexpr std::uint64_t kDefaultResizeWorkerCacheBytes = std::uint64_t{16} << 20U;
inline constexpr std::uint64_t kDefaultResizeMaximumCachedRows = 2'048;

struct ResizeOptions final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ResamplingMethod method = ResamplingMethod::lanczos3;
    bool premultiply_alpha = true;
    bool linear_rgb = true;
    // Zero selects a hardware- and working-memory-aware thread count.
    std::uint32_t maximum_threads = 0;
    // A larger target-major row cache is replaced by an exact source-major
    // accumulator. UINT64_MAX permits an unbounded cache for controlled uses.
    std::uint64_t maximum_worker_cache_bytes = kDefaultResizeWorkerCacheBytes;
};

struct PaletteOptions final {
    std::uint16_t maximum_colors = 256;
    float dithering = 1.0F;
};

struct TransformOptions final {
    std::optional<ResizeOptions> resize;
    std::optional<PaletteOptions> palette;
    AnimationPolicy animation_policy = AnimationPolicy::preserve;
};

SNOW_IMAGE_API Result<Document> transform(const Document& document, const TransformOptions& options,
                                          std::stop_token stop = {});

SNOW_IMAGE_API Result<void> transform_to_sink(const Document& document,
                                              const TransformOptions& options, PixelSink& sink,
                                              std::stop_token stop = {});

SNOW_IMAGE_API Result<Document> flatten_animation(const Document& document,
                                                  std::stop_token stop = {});

[[nodiscard]] SNOW_IMAGE_API Result<AlphaContent> classify_alpha(ImageView image,
                                                                 std::stop_token stop = {});

[[nodiscard]] SNOW_IMAGE_API Result<AlphaContent> classify_alpha(const Image& image,
                                                                 std::stop_token stop = {});

SNOW_IMAGE_API Result<Document> composite_alpha(const Document& document, std::uint8_t red,
                                                std::uint8_t green, std::uint8_t blue,
                                                bool linear_rgb = true, std::stop_token stop = {});

struct SdrConversionOptions final {
    bool tone_map_hdr = true;
    AlphaContent verified_alpha_content = AlphaContent::non_opaque;
    std::optional<std::array<std::uint8_t, 3>> background;
    PixelFormat output_format = kRgba8;
};

// Converts color, tone maps, and optionally composites in one pixel pass. The
// alpha classification is trusted as pixel-verified and is never rescanned.
SNOW_IMAGE_API Result<Document> convert_to_sdr_srgb(const Document& document,
                                                    const SdrConversionOptions& options,
                                                    std::stop_token stop = {});

SNOW_IMAGE_API Result<Document>
convert_to_sdr_srgb(const Document& document, bool tone_map_hdr = true, std::stop_token stop = {});

SNOW_IMAGE_API Result<Document> convert_to_hdr_rec2020_pq16(const Document& document,
                                                            std::stop_token stop = {});

} // namespace snow::image
