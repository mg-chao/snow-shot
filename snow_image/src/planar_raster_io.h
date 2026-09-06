#pragma once

#include "snow/image/codec.h"
#include "snow/image/raster.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace snow::image::internal {

struct WritablePlaneSet final {
    std::vector<MutableMappedPlane> mappings;
    std::vector<std::vector<std::byte>> owned;
    std::vector<unsigned char*> pointers;
    std::vector<int> strides;
    bool mapped = false;
};

struct ReadablePlaneSet final {
    std::vector<MappedPlane> mappings;
    std::vector<std::vector<std::byte>> owned;
    std::vector<const unsigned char*> pointers;
    std::vector<int> strides;
};

struct ReadablePlaneView final {
    std::span<const std::byte> pixels;
    std::size_t row_stride = 0;
};

[[nodiscard]] Result<WritablePlaneSet> prepare_writable_planes(RasterWriter& writer,
                                                               std::uint32_t frame_index,
                                                               const RasterFrameDescriptor& frame,
                                                               const DecodeLimits& limits,
                                                               std::string_view codec);

[[nodiscard]] Result<void> publish_writable_planes(RasterWriter& writer, std::uint32_t frame_index,
                                                   const RasterFrameDescriptor& frame,
                                                   WritablePlaneSet& planes,
                                                   std::stop_token stop = {});

[[nodiscard]] Result<ReadablePlaneSet> prepare_readable_planes(
    const RasterSource& source, std::uint32_t frame_index, const RasterFrameDescriptor& frame,
    std::stop_token stop = {},
    std::uint64_t maximum_owned_bytes = std::numeric_limits<std::uint64_t>::max(),
    std::string_view codec = {});

[[nodiscard]] Result<void>
publish_readable_plane_views(RasterWriter& writer, std::uint32_t frame_index,
                             const RasterFrameDescriptor& frame,
                             std::span<const ReadablePlaneView> planes, std::stop_token stop = {},
                             bool commit = true, std::string_view codec = {});

} // namespace snow::image::internal
