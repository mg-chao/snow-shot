#pragma once

#include "snow_draw_engine_qt/snow_canvas_types.h"
#include "snow_shot/presentation/screenshotinteractionstate.h"
#include <QLatin1StringView>
#include <array>
#include <optional>

namespace snow_shot::presentation {
struct ScreenshotCanvasActiveToolDescriptor {
    SnowCanvasTool canvasTool;
    ScreenshotActiveTool activeTool;
    QLatin1StringView automationId;
};
#define SNOW_SHOT_CANVAS_TOOL(signal, appTool, canvasTool, shortcut, automation)                   \
    ScreenshotCanvasActiveToolDescriptor{                                                          \
        SnowCanvasTool::canvasTool, ScreenshotActiveTool::appTool, QLatin1StringView(automation)},
inline constexpr std::array screenshotCanvasActiveTools{
#include "snow_shot/presentation/screenshotcanvastools.inc"
};
#undef SNOW_SHOT_CANVAS_TOOL

[[nodiscard]] inline std::optional<ScreenshotActiveTool>
activeToolForCanvasTool(SnowCanvasTool tool) {
    for (const auto& descriptor : screenshotCanvasActiveTools) {
        if (descriptor.canvasTool == tool)
            return descriptor.activeTool;
    }
    return std::nullopt;
}
[[nodiscard]] inline std::optional<SnowCanvasTool>
canvasToolForActiveTool(ScreenshotActiveTool tool) {
    for (const auto& descriptor : screenshotCanvasActiveTools) {
        if (descriptor.activeTool == tool)
            return descriptor.canvasTool;
    }
    return std::nullopt;
}
} // namespace snow_shot::presentation
