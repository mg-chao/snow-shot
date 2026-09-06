#include "snow_canvas_event_flow.h"

#include <QKeyEvent>

namespace snow_canvas_event_flow {

bool shouldAcceptInputResult(bool processed, const SnowInteractionOutput& output) {
    return processed && output.consumed != 0;
}

SnowInputEvent focusLostInput() {
    SnowInputEvent input{};
    input.kind = SNOW_INPUT_EVENT_FOCUS_LOST;
    return input;
}

KeyShortcutAction keyPressShortcutAction(const QKeyEvent& event) {
    if (event.isAutoRepeat()) {
        return KeyShortcutAction::None;
    }
    if (event.key() == Qt::Key_F4) {
        return KeyShortcutAction::ToggleDirtyRects;
    }
    return KeyShortcutAction::None;
}

KeyShortcutAction keyReleaseShortcutAction(const QKeyEvent& event) {
    if (event.isAutoRepeat()) {
        return KeyShortcutAction::None;
    }
    if (event.key() == Qt::Key_F4) {
        return KeyShortcutAction::Swallow;
    }
    return KeyShortcutAction::None;
}

} // namespace snow_canvas_event_flow
