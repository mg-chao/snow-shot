#pragma once

#include "snow/image/codec.h"
#include "snow/image/export.h"

#include <functional>

namespace snow::image::internal {

using PngDataWriter = std::function<Result<void>(std::span<const std::uint8_t>)>;

unsigned parallel_png_workers(const RasterFrameDescriptor& frame, const EncodeOptions& options);
Result<void> write_parallel_png_data(const RasterSource& source, bool opaque, int level,
                                     unsigned workers, const PngDataWriter& write,
                                     std::stop_token stop);

#if defined(SNOW_IMAGE_TEST_PNG_STRIPES)
// Affects encoding on the calling thread. Zero restores hardware detection.
SNOW_IMAGE_API void set_png_worker_count_for_testing(unsigned workers);
#endif

} // namespace snow::image::internal
