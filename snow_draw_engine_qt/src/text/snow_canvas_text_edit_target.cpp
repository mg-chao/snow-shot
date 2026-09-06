#include "snow_canvas_text_edit_target.h"

#include "snow_canvas_text.h"

#include <QFont>

#include <cstdint>

namespace snow_canvas_text_edit_target {
namespace {

bool hasViewport(SnowRuntime runtime, SnowViewport viewport) {
    return runtime != nullptr && viewport != nullptr;
}

std::optional<SnowElementId> hitTextElementId(SnowRuntime runtime, SnowViewport viewport,
                                              const QPointF& canvasPoint) {
    SnowElementId hitId{};
    std::uint8_t hit = 0;
    if (snow_viewport_hit_text(runtime, viewport, canvasPoint.x(), canvasPoint.y(), &hitId, &hit) !=
            SNOW_OK ||
        hit == 0) {
        return std::nullopt;
    }
    return hitId;
}

std::optional<SnowTextElementInfo> textElementInfo(SnowRuntime runtime, const SnowElementId& id) {
    SnowTextElementInfo textInfo{};
    if (snow_runtime_get_text_element(runtime, id, &textInfo) != SNOW_OK) {
        return std::nullopt;
    }
    return textInfo;
}

} // namespace

std::optional<SnowTextElementInfo>
resolveTextEditTarget(SnowRuntime runtime, SnowViewport viewport, const QPointF& canvasPoint,
                      const QFont& baseFont, const SnowTextStyle& newTextStyle, bool allowCreate) {
    if (!hasViewport(runtime, viewport)) {
        return std::nullopt;
    }

    if (const std::optional<SnowElementId> hitId =
            hitTextElementId(runtime, viewport, canvasPoint)) {
        return textElementInfo(runtime, *hitId);
    }

    if (!allowCreate) {
        return std::nullopt;
    }

    return snow_canvas_text::newTextInfoAt(canvasPoint, baseFont, newTextStyle);
}

std::optional<SnowTextElementInfo> resolveSelectedTextEditTarget(SnowRuntime runtime,
                                                                 SnowViewport viewport,
                                                                 const QPointF& canvasPoint,
                                                                 bool requireSerialBoundText) {
    if (!hasViewport(runtime, viewport)) {
        return std::nullopt;
    }

    const std::optional<SnowElementId> hitId = hitTextElementId(runtime, viewport, canvasPoint);
    if (!hitId.has_value()) {
        return std::nullopt;
    }

    std::uint8_t selected = 0;
    if (snow_viewport_is_element_selected(runtime, viewport, *hitId, &selected) != SNOW_OK ||
        selected == 0) {
        return std::nullopt;
    }

    std::uint32_t selectedTextCount = 0;
    if (snow_viewport_selected_text_count(runtime, viewport, &selectedTextCount) != SNOW_OK ||
        selectedTextCount != 1) {
        return std::nullopt;
    }

    if (requireSerialBoundText) {
        std::uint8_t bound = 0;
        if (snow_viewport_is_text_bound_to_serial_number(runtime, viewport, *hitId, &bound) !=
                SNOW_OK ||
            bound == 0) {
            return std::nullopt;
        }
    }

    return textElementInfo(runtime, *hitId);
}

} // namespace snow_canvas_text_edit_target
