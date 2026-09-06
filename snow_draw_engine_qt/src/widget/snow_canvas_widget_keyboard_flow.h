#pragma once

#include "snow_canvas_event_flow.h"

class QKeyEvent;

namespace snow_canvas_widget_keyboard_flow {

enum class KeyEffect {
    None,
    ToggleDirtyRects,
    AcceptOnly,
    DispatchToEngine,
};

struct KeyPlan {
    bool hasEvent = false;
    KeyEffect effect = KeyEffect::None;
};

KeyPlan planPress(const QKeyEvent* event);
KeyPlan planRelease(const QKeyEvent* event);

} // namespace snow_canvas_widget_keyboard_flow
