#pragma once

#include <QSize>

#include <cstdint>

#include "snow_draw_engine.h"

class SnowCanvasDisplayCache;
class SnowCanvasViewport;

namespace snow_canvas_state {
class Store;
}

namespace snow_canvas_lifecycle {

struct InitializeEngineResult {
    bool hasViewport = false;
    std::uint64_t viewportId = 0;
};

bool hasViewport(SnowRuntime runtime, const SnowCanvasViewport& viewport);
InitializeEngineResult initializeEngine(SnowRuntime runtime, SnowCanvasViewport& viewport,
                                        SnowCanvasDisplayCache& displayCache,
                                        snow_canvas_state::Store& state);
bool setSurfaceSize(SnowRuntime runtime, const SnowCanvasViewport& viewport, const QSize& size);
bool setCamera(SnowRuntime runtime, const SnowCanvasViewport& viewport, double centerX,
               double centerY, double zoom);

} // namespace snow_canvas_lifecycle
