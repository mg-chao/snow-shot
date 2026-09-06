#pragma once

#include "snow_draw_engine.h"

#include <cstdint>
#include <vector>

namespace snow_canvas_changed_viewports {

using ViewportIds = std::vector<std::uint64_t>;

ViewportIds deduplicate(const ViewportIds& viewportIds);
ViewportIds idsFromList(SnowChangedViewportList changedViewports);

} // namespace snow_canvas_changed_viewports
