#pragma once

#include <cstdint>

#include "snow_draw_engine.h"

class SnowCanvasViewport final {
  public:
    SnowCanvasViewport() = default;
    ~SnowCanvasViewport();

    SnowCanvasViewport(const SnowCanvasViewport&) = delete;
    SnowCanvasViewport& operator=(const SnowCanvasViewport&) = delete;

    SnowCanvasViewport(SnowCanvasViewport&& other) noexcept;
    SnowCanvasViewport& operator=(SnowCanvasViewport&& other) noexcept;

    bool create(SnowRuntime runtime, const SnowEngineConfig& config);
    void reset();

    SnowViewport get() const;
    SnowRuntime runtime() const;
    bool isValid() const;
    bool id(std::uint64_t* outId) const;

  private:
    SnowRuntime m_runtime = nullptr;
    SnowViewport m_viewport = nullptr;
};

namespace snow_canvas_viewport {

SnowEngineConfig defaultEngineConfig();

} // namespace snow_canvas_viewport
