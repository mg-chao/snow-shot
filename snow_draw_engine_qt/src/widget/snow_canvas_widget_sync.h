#pragma once

#include "snow_canvas_ffi_handles.h"
#include "snow_canvas_state.h"

#include <QFont>
#include <QRect>
#include <QRegion>

class SnowCanvasTextEditorSession;
class SnowCanvasWidgetDisplayState;

namespace snow_canvas_widget_sync {

struct Request {
    SnowCanvasWidgetDisplayState* displayState = nullptr;
    SnowCanvasTextEditorSession* textEditorSession = nullptr;
    SnowRuntime runtime = nullptr;
    SnowViewport viewport = nullptr;
    bool hasViewport = false;
    QRect widgetRect;
    QFont font;
    bool showDirtyRects = false;
    bool emitStateSignals = true;
    QRegion extraRepaintRegion;
};

struct Result {
    QRegion repaintRegion;
    snow_canvas_state::Changes stateChanges;
    bool displaySyncAttempted = false;
    bool stateRefreshed = false;
    bool shouldEmitStateSignals = false;
};

Result syncAfterEngineMutation(const Request& request);

} // namespace snow_canvas_widget_sync
