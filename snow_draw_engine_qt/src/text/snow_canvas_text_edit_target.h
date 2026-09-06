#pragma once

#include "snow_draw_engine.h"

#include <QPointF>

#include <optional>

class QFont;

namespace snow_canvas_text_edit_target {

std::optional<SnowTextElementInfo>
resolveTextEditTarget(SnowRuntime runtime, SnowViewport viewport, const QPointF& canvasPoint,
                      const QFont& baseFont, const SnowTextStyle& newTextStyle, bool allowCreate);

std::optional<SnowTextElementInfo> resolveSelectedTextEditTarget(SnowRuntime runtime,
                                                                 SnowViewport viewport,
                                                                 const QPointF& canvasPoint,
                                                                 bool requireSerialBoundText);

} // namespace snow_canvas_text_edit_target
