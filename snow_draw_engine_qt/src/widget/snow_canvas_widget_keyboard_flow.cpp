#include "snow_canvas_widget_keyboard_flow.h"

#include <QKeyEvent>

namespace snow_canvas_widget_keyboard_flow {
namespace {

KeyEffect effectForShortcut(snow_canvas_event_flow::KeyShortcutAction action) {
    switch (action) {
    case snow_canvas_event_flow::KeyShortcutAction::ToggleDirtyRects:
        return KeyEffect::ToggleDirtyRects;
    case snow_canvas_event_flow::KeyShortcutAction::Swallow:
        return KeyEffect::AcceptOnly;
    case snow_canvas_event_flow::KeyShortcutAction::None:
    default:
        return KeyEffect::DispatchToEngine;
    }
}

} // namespace

KeyPlan planPress(const QKeyEvent* event) {
    KeyPlan plan;
    if (event == nullptr) {
        return plan;
    }
    plan.hasEvent = true;
    plan.effect = effectForShortcut(snow_canvas_event_flow::keyPressShortcutAction(*event));
    return plan;
}

KeyPlan planRelease(const QKeyEvent* event) {
    KeyPlan plan;
    if (event == nullptr) {
        return plan;
    }
    plan.hasEvent = true;
    plan.effect = effectForShortcut(snow_canvas_event_flow::keyReleaseShortcutAction(*event));
    return plan;
}

} // namespace snow_canvas_widget_keyboard_flow
