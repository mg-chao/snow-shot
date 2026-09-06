#include "snow_canvas_sync_plan.h"

#include "snow_canvas_display_cache.h"

namespace snow_canvas_sync_plan {

DirtyRegions dirtyRegionsForCache(const SnowCanvasDisplayCache& cache, const QRect& clip) {
    DirtyRegions regions;
    regions.scene = snow_canvas_display::dirtyRectsToRegion(cache.sceneDirtyRects(),
                                                            cache.sceneDirtyRectCount(), clip);
    regions.decoration = snow_canvas_display::dirtyRectsToRegion(
        cache.decorationDirtyRects(), cache.decorationDirtyRectCount(), clip);
    regions.overlay = snow_canvas_display::dirtyRectsToRegion(cache.overlayDirtyRects(),
                                                              cache.overlayDirtyRectCount(), clip);
    regions.visualization = regions.scene + regions.decoration + regions.overlay;
    return regions;
}

Plan build(const Request& request) {
    Plan plan;
    plan.dirtyVisualizationRegion = request.display.visualization;
    plan.updateRegion += request.display.scene;
    plan.updateRegion += request.display.decoration;
    plan.updateRegion += request.display.overlay;

    if (request.textEditorActive) {
        plan.updateRegion += request.textEditorRegionBefore;
        plan.updateRegion += request.textEditorRegionAfter;
    }

    if (request.showDirtyRects) {
        plan.updateRegion += request.previousDirtyVisualization;
        plan.updateRegion += plan.dirtyVisualizationRegion;
    }

    return plan;
}

} // namespace snow_canvas_sync_plan
