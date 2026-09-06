#pragma once

#include "snow/image/export.h"
#include "snow/image/codec.h"
#include "snow/image/raster.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace snow::image {

enum class ResourcePhase : std::uint8_t {
    inspect,
    decode,
    transform,
    encode,
    validate,
    preview,
    save,
};

struct ResourceFootprint final {
    std::uint64_t private_memory_bytes = 0;
    std::uint64_t mapped_bytes = 0;
    std::uint64_t gpu_bytes = 0;
    std::uint64_t artifact_bytes = 0;
    std::uint64_t temporary_disk_bytes = 0;

    friend bool operator==(const ResourceFootprint&, const ResourceFootprint&) = default;
};

struct PhaseResourcePlan final {
    ResourcePhase phase = ResourcePhase::inspect;
    ResourceFootprint footprint;
};

struct ResourceBudgets final {
    std::uint64_t private_memory_bytes = std::uint64_t{512} << 20U;
    std::uint64_t mapped_bytes = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t gpu_bytes = std::uint64_t{256} << 20U;
    std::uint64_t artifact_bytes = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t temporary_disk_bytes = std::numeric_limits<std::uint64_t>::max();
};

struct ResourcePlanRequest final {
    DocumentDescriptor source;
    DocumentDescriptor output;
    EncodeOptions encode_options;
    RasterEncodeRoute raster_route = RasterEncodeRoute::materialized;
    ResourceBudgets budgets;
    std::uint64_t expected_artifact_bytes = 0;
    std::uint32_t maximum_texture_size = 16'384;
    std::uint32_t preferred_tile_edge = 2'048;
    std::uint32_t minimum_tile_edge = 256;
    std::uint32_t staging_depth = 3;
    std::uint32_t maximum_cpu_threads = 0;
    double resampling_support = 3.0;
    bool gpu_transform = true;
};

struct ResourcePlan final {
    std::vector<PhaseResourcePlan> phases;
    ResourceFootprint peak;
    std::uint32_t tile_width = 0;
    std::uint32_t tile_height = 0;
    std::uint32_t staging_depth = 0;
    std::uint32_t cpu_threads = 1;
    bool file_backed_source = true;
    bool file_backed_output = true;
    bool streaming_polyphase = false;

    [[nodiscard]] Result<void> fits(const ResourceBudgets& budgets) const;
};

[[nodiscard]] SNOW_IMAGE_API Result<ResourcePlan>
plan_resources(const ResourcePlanRequest& request);

} // namespace snow::image
