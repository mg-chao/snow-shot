#include "snow_shot/presentation/screenshotcapturestate.h"
#include "snow_shot/presentation/screenshotdisplaysession.h"
#include "snow_shot/presentation/screenshotgeometry.h"
#include "snow_shot/presentation/screenshotintelligentselectionmodel.h"
#include "snow_shot/presentation/screenshotinteractionstate.h"
#include "snow_shot/presentation/screenshotselectionmodel.h"
#include "snow_shot/presentation/screenshottoolcommandworkflow.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void nonDrawingToolsDisableCanvasInteraction() {
    ScreenshotCaptureState captureState;
    ScreenshotDisplaySession displaySession;
    ScreenshotGeometryMapper geometry;
    ScreenshotInteractionState interaction;
    ScreenshotSelectionModel selection;
    ScreenshotIntelligentSelectionModel intelligentSelection;

    bool canvasInteractionEnabled = true;
    int interactionChanges = 0;
    SnowCanvasTool selectedCanvasTool = SnowCanvasTool::Select;
    int canvasToolChanges = 0;

    ScreenshotToolCommandActions actions;
    actions.selectorReady = []() { return false; };
    actions.startSelectorRefresh = []() {};
    actions.updateSelectorSelectionAt = [](const QPoint&) {};
    actions.clearSelectorSelection = []() {};
    actions.setCanvasInteractionEnabled = [&](bool enabled) {
        canvasInteractionEnabled = enabled;
        ++interactionChanges;
    };
    actions.setCanvasTool = [&](SnowCanvasTool tool) {
        selectedCanvasTool = tool;
        ++canvasToolChanges;
    };
    actions.updateOverlayState = []() {};
    actions.updateOverlayCursors = []() {};
    actions.raiseToolbarForCanvasInteraction = []() {};

    ScreenshotToolCommandWorkflow workflow({
        captureState,
        std::move(actions),
        displaySession,
        geometry,
        interaction,
        selection,
        intelligentSelection,
    });

    workflow.setMoveTool();
    require(!canvasInteractionEnabled && interactionChanges == 1,
            "activating Move must disable canvas interaction");
    require(interaction.activeTool() == ScreenshotActiveTool::Move,
            "activating Move should retain the non-drawing tool state");
    require(canvasToolChanges == 0,
            "activating Move must not replace the retained canvas drawing tool");

    workflow.setShapeTool();
    require(canvasInteractionEnabled && interactionChanges == 2,
            "activating a drawing tool must re-enable canvas interaction");
    require(selectedCanvasTool == SnowCanvasTool::Shape && canvasToolChanges == 1,
            "activating Shape should forward the drawing tool after enabling interaction");

    workflow.setMoveTool();
    require(!canvasInteractionEnabled && interactionChanges == 3,
            "returning to Move must disable canvas interaction again");
}
} // namespace

int main() {
    nonDrawingToolsDisableCanvasInteraction();
    return 0;
}
