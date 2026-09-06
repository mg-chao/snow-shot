#pragma once

#include <QRect>
#include <QRegion>

class SnowCanvasDisplayCache;

namespace snow_canvas_sync_plan {

struct DirtyRegions {
    QRegion scene;
    QRegion decoration;
    QRegion overlay;
    QRegion visualization;
};

struct Request {
    DirtyRegions display;
    QRegion previousDirtyVisualization;
    QRegion textEditorRegionBefore;
    QRegion textEditorRegionAfter;
    bool textEditorActive = false;
    bool showDirtyRects = false;
};

struct Plan {
    QRegion updateRegion;
    QRegion dirtyVisualizationRegion;
};

DirtyRegions dirtyRegionsForCache(const SnowCanvasDisplayCache& cache, const QRect& clip);
Plan build(const Request& request);

} // namespace snow_canvas_sync_plan
