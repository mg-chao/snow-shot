#include "snow_canvas_widget_pointer_flow.h"

namespace snow_canvas_widget_pointer_flow {

PressPlan planPress(const PressRequest& request) {
    PressPlan plan;
    if (!request.hasEvent) {
        return plan;
    }

    plan.shouldFocusWidget = true;
    const bool activeSelectionInteractionPress =
        request.button == Qt::LeftButton && request.pointerOverSelectionInteraction;
    plan.shouldCommitTextEditor = request.textEditorActive && !request.pointerInsideTextEditor &&
                                  !activeSelectionInteractionPress;
    const bool suppressedTextCreateForPress = request.suppressNextTextToolCreate &&
                                              request.canvasTool == SnowCanvasTool::Text &&
                                              request.button == Qt::LeftButton;
    plan.suppressedTextCreateForPress = suppressedTextCreateForPress;
    plan.suppressedTextCreateRestoredSelection =
        suppressedTextCreateForPress && request.restoredSelectionForNextTextToolPress;
    plan.shouldBeginText =
        request.canvasTool == SnowCanvasTool::Text && request.button == Qt::LeftButton &&
        !(request.modifiers & Qt::ShiftModifier) && !activeSelectionInteractionPress;
    plan.shouldBeginSelectedText = (request.canvasTool == SnowCanvasTool::Select ||
                                    request.canvasTool == SnowCanvasTool::SerialNumber) &&
                                   request.button == Qt::LeftButton &&
                                   !(request.modifiers & Qt::ShiftModifier) &&
                                   !plan.shouldCommitTextEditor;
    plan.allowCreateText =
        plan.shouldBeginText && !plan.shouldCommitTextEditor && !suppressedTextCreateForPress;
    plan.shouldAcceptIfTextBeginFails =
        plan.shouldCommitTextEditor && !activeSelectionInteractionPress;
    plan.shouldAcceptSuppressedTextCreate =
        suppressedTextCreateForPress && !request.pointerOverSelectionInteraction;
    plan.dispatchAfterCommitRequiresRestoredSelection =
        plan.shouldCommitTextEditor && activeSelectionInteractionPress;
    plan.shouldDispatchToEngine = !plan.shouldCommitTextEditor || activeSelectionInteractionPress;
    return plan;
}

} // namespace snow_canvas_widget_pointer_flow
