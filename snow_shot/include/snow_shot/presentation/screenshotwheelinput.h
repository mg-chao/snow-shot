#pragma once

#include "snow_draw_engine_qt/snow_canvas_wheel_input.h"

#include <QWheelEvent>

namespace snow_shot::presentation {

inline bool usesPreciseWheelDelta(const QWheelEvent& event) {
    return snow_canvas_wheel::usesPreciseDelta(event);
}

inline int pinnedWheelAngleDelta(const QWheelEvent& event) {
    const QPoint angle = event.angleDelta();
#ifdef Q_OS_WIN
    // Qt reports Alt+vertical mouse wheel on X. Shift+wheel can also be
    // delivered as horizontal scrolling. Keep pixel-based trackpad input on Y.
    if (event.pixelDelta().isNull() && angle.y() == 0 &&
        event.modifiers().testAnyFlags(Qt::AltModifier | Qt::ShiftModifier))
        return angle.x();
#endif
    return angle.y();
}

// The canvas library owns discrete adjustment thresholds and gesture boundaries.
using WheelStepAccumulator = snow_canvas_wheel::StepAccumulator;

} // namespace snow_shot::presentation
