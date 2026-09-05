#include "snow_shot/presentation/directcapturehistory.h"

#include "snow_draw_engine_qt/snow_canvas_runtime.h"

#include <QUuid>

namespace snow_shot::presentation {
storage::CaptureHistoryDraft directCaptureHistoryDraft(const DirectCaptureRequest& request,
                                                       const DirectCaptureFrame& frame) {
    storage::CaptureHistoryDraft draft;
    if (!frame.isValid())
        return draft;
    draft.contentKind = storage::CaptureHistoryContentKind::Image;
    draft.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    draft.createdUtc = request.requestedAt.toUTC();
    draft.canvasBounds = frame.image.rect();
    draft.selection.rectangle = frame.image.rect();
    draft.selection.shadowColor = QColor(Qt::black);
    // The storage format accepts drawing history; this isolated empty document
    // keeps image records readable without consulting a live screenshot editor.
    SnowCanvasRuntime emptyDocument;
    draft.canvasHistory = emptyDocument.serializeDocumentHistory();
    draft.displays.push_back({frame.identity, frame.identity, frame.image});
    draft.resultImage = frame.image;
    draft.source = request.target == DirectCaptureTarget::FocusedWindow
                       ? storage::CaptureHistorySource::FocusedWindow
                       : storage::CaptureHistorySource::CurrentMonitor;
    return draft;
}
} // namespace snow_shot::presentation
