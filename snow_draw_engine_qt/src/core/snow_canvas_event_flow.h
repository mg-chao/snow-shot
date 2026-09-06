#pragma once

#include "snow_draw_engine.h"

class QKeyEvent;

namespace snow_canvas_event_flow {

enum class KeyShortcutAction {
    None,
    ToggleDirtyRects,
    Swallow,
};

bool shouldAcceptInputResult(bool processed, const SnowInteractionOutput& output);
SnowInputEvent focusLostInput();
KeyShortcutAction keyPressShortcutAction(const QKeyEvent& event);
KeyShortcutAction keyReleaseShortcutAction(const QKeyEvent& event);

} // namespace snow_canvas_event_flow
