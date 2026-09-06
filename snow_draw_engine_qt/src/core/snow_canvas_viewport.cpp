#include "snow_canvas_viewport.h"

namespace snow_canvas_viewport {

SnowEngineConfig defaultEngineConfig() {
    SnowEngineConfig config{};
    config.min_zoom = 0.1;
    config.max_zoom = 8.0;
    config.zoom_focus = SNOW_ZOOM_FOCUS_POINTER;
    config.wheel_zoom_sensitivity = 0.001;
    config.clear_color = SnowColorRgba8{255, 255, 255, 255};
    config.snap.enabled = 0;
    config.snap.enable_point_snaps = 1;
    config.snap.enable_gap_snaps = 1;
    config.snap.show_guides = 1;
    config.snap.show_gap_size = 0;
    config.snap.distance = 8.0;
    config.snap.line_color = SnowColorRgba8{255, 107, 107, 255};
    config.snap.line_width = 1.0;
    config.snap.marker_size = 8.0;
    config.snap.gap_dash_length = 4.0;
    config.snap.gap_dash_gap = 4.0;
    config.grid.enabled = 0;
    config.grid.size = 20.0;
    config.enable_pointer_capture = 1;
    return config;
}

} // namespace snow_canvas_viewport

SnowCanvasViewport::~SnowCanvasViewport() {
    reset();
}

SnowCanvasViewport::SnowCanvasViewport(SnowCanvasViewport&& other) noexcept
    : m_runtime(other.m_runtime), m_viewport(other.m_viewport) {
    other.m_runtime = nullptr;
    other.m_viewport = nullptr;
}

SnowCanvasViewport& SnowCanvasViewport::operator=(SnowCanvasViewport&& other) noexcept {
    if (this != &other) {
        reset();
        m_runtime = other.m_runtime;
        m_viewport = other.m_viewport;
        other.m_runtime = nullptr;
        other.m_viewport = nullptr;
    }
    return *this;
}

bool SnowCanvasViewport::create(SnowRuntime runtime, const SnowEngineConfig& config) {
    reset();
    if (runtime == nullptr) {
        return false;
    }

    SnowViewport viewport = nullptr;
    if (snow_viewport_create(runtime, &config, &viewport) != SNOW_OK) {
        return false;
    }

    m_runtime = runtime;
    m_viewport = viewport;
    return true;
}

void SnowCanvasViewport::reset() {
    if (m_runtime != nullptr && m_viewport != nullptr) {
        snow_viewport_destroy(m_runtime, m_viewport);
    }
    m_runtime = nullptr;
    m_viewport = nullptr;
}

SnowViewport SnowCanvasViewport::get() const {
    return m_viewport;
}

SnowRuntime SnowCanvasViewport::runtime() const {
    return m_runtime;
}

bool SnowCanvasViewport::isValid() const {
    return m_runtime != nullptr && m_viewport != nullptr;
}

bool SnowCanvasViewport::id(std::uint64_t* outId) const {
    return outId != nullptr && m_viewport != nullptr &&
           snow_viewport_get_id(m_viewport, outId) == SNOW_OK;
}
