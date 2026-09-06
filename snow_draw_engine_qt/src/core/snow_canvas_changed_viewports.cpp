#include "snow_canvas_changed_viewports.h"

#include <algorithm>

namespace snow_canvas_changed_viewports {
namespace {

bool containsId(const ViewportIds& viewportIds, std::uint64_t viewportId) {
    return std::find(viewportIds.begin(), viewportIds.end(), viewportId) != viewportIds.end();
}

} // namespace

ViewportIds deduplicate(const ViewportIds& viewportIds) {
    ViewportIds deduplicated;
    deduplicated.reserve(viewportIds.size());
    for (std::uint64_t viewportId : viewportIds) {
        if (!containsId(deduplicated, viewportId)) {
            deduplicated.push_back(viewportId);
        }
    }
    return deduplicated;
}

ViewportIds idsFromList(SnowChangedViewportList changedViewports) {
    if (changedViewports == nullptr) {
        return {};
    }

    const std::uint32_t count = snow_changed_viewports_count(changedViewports);
    ViewportIds viewportIds;
    viewportIds.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint64_t viewportId = 0;
        if (snow_changed_viewports_get(changedViewports, index, &viewportId) == SNOW_OK) {
            viewportIds.push_back(viewportId);
        }
    }
    return deduplicate(viewportIds);
}

} // namespace snow_canvas_changed_viewports
