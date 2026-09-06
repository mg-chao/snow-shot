#include "snow_shot/presentation/directcapturehistory.h"

#include "snow_draw_engine_qt/snow_canvas_runtime.h"

#include <QUuid>

namespace snow_shot::presentation {
storage::CaptureHistoryDraft directCaptureHistoryDraft(const DirectCaptureRequest& request,
                                                       const DirectCaptureFrame& frame) {
    storage::CaptureHistoryDraft draft;
    if (!frame.isValid() || frame.displays.isEmpty())
        return draft;
    draft.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    draft.createdUtc = request.requestedAt.toUTC();
    draft.selection.shadowColor = QColor(Qt::black);
    // Direct capture owns a complete snapshot without consulting the live editor.
    SnowCanvasRuntime emptyDocument;
    draft.canvasHistory = emptyDocument.serializeDocumentHistory();
    for (const auto& display : frame.displays) {
        if (display.image.isNull() || display.physicalBounds.size() != display.image.size())
            return {};
        draft.canvasBounds = draft.canvasBounds.united(display.physicalBounds);
        draft.displays.push_back(
            {display.stableId, display.name, display.image, display.physicalBounds.topLeft()});
    }
    // The editor uses physical pixel sizes relative to the complete desktop's top-left.
    const QPoint canvasOffset = -draft.canvasBounds.topLeft();
    draft.canvasBounds.translate(canvasOffset);
    draft.selection.rectangle = frame.physicalBounds.translated(canvasOffset);
    for (auto& display : draft.displays) {
        *display.sourceCanvasOrigin += canvasOffset;
    }
    draft.resultImage = frame.image;
    draft.source = request.target == DirectCaptureTarget::FocusedWindow
                       ? storage::CaptureHistorySource::FocusedWindow
                       : storage::CaptureHistorySource::CurrentMonitor;
    return draft;
}
} // namespace snow_shot::presentation
