#include "snow_canvas_lifecycle.h"

#include "snow_canvas_display_cache.h"
#include "snow_canvas_state.h"
#include "snow_canvas_viewport.h"

namespace snow_canvas_lifecycle {

bool hasViewport(SnowRuntime runtime, const SnowCanvasViewport& viewport) {
    return runtime != nullptr && viewport.isValid() && viewport.runtime() == runtime;
}

InitializeEngineResult initializeEngine(SnowRuntime runtime, SnowCanvasViewport& viewport,
                                        SnowCanvasDisplayCache& displayCache,
                                        snow_canvas_state::Store& state) {
    InitializeEngineResult result;
    if (runtime == nullptr) {
        viewport.reset();
        return result;
    }

    const SnowEngineConfig config = snow_canvas_viewport::defaultEngineConfig();
    state.applyEngineConfigDefaults(config);
    displayCache.setClearColor(config.clear_color);

    if (!viewport.create(runtime, config)) {
        return result;
    }

    result.hasViewport = true;
    viewport.id(&result.viewportId);
    return result;
}

bool setSurfaceSize(SnowRuntime runtime, const SnowCanvasViewport& viewport, const QSize& size) {
    if (!hasViewport(runtime, viewport)) {
        return false;
    }
    if (size.width() < 0 || size.height() < 0) {
        return false;
    }

    return snow_viewport_set_surface_size(runtime, viewport.get(),
                                          static_cast<std::uint32_t>(size.width()),
                                          static_cast<std::uint32_t>(size.height())) == SNOW_OK;
}

bool setCamera(SnowRuntime runtime, const SnowCanvasViewport& viewport, double centerX,
               double centerY, double zoom) {
    if (!hasViewport(runtime, viewport)) {
        return false;
    }

    return snow_viewport_set_camera(runtime, viewport.get(), centerX, centerY, zoom) == SNOW_OK;
}

} // namespace snow_canvas_lifecycle
