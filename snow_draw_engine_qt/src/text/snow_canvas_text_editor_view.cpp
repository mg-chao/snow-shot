#include "snow_canvas_text_editor_view.h"

#include "snow_canvas_render_geometry.h"
#include "snow_canvas_text_edit_geometry.h"
#include "snow_canvas_text_editor_overlay.h"
#include "snow_canvas_text_layout.h"

#include <QPainter>

namespace snow_canvas_text_editor_view {

QPointF canvasToViewPoint(const SceneDisplayInfo& sceneInfo, const QPointF& canvasPosition) {
    return snow_canvas_render_geometry::canvasToView(
        sceneInfo.camera_center_x, sceneInfo.camera_center_y, sceneInfo.camera_zoom,
        sceneInfo.surface_width, sceneInfo.surface_height, canvasPosition.x(), canvasPosition.y());
}

bool itemContainsViewPosition(const SnowSceneDisplayItem& item, const SceneDisplayInfo& sceneInfo,
                              const QPointF& viewPosition) {
    return snow_canvas_text_edit_geometry::viewPointInsideTextItem(
        item, canvasToViewPoint(sceneInfo, QPointF(item.center_x, item.center_y)),
        sceneInfo.camera_zoom, viewPosition);
}

QRegion previewRegion(const SnowSceneDisplayItem& item, const SceneDisplayInfo& sceneInfo,
                      int paddingPx) {
    return QRegion(snow_canvas_render_geometry::alignedRectForBounds(
        snow_canvas_render_geometry::sceneItemBounds(sceneInfo, item), paddingPx));
}

QRectF cursorRect(const SnowSceneDisplayItem& item, const QFont& baseFont,
                  const SceneDisplayInfo& sceneInfo, const QString& text, int cursorPosition) {
    return snow_canvas_text_edit_geometry::cursorRectForTextPosition(
        item, baseFont, canvasToViewPoint(sceneInfo, QPointF(item.center_x, item.center_y)),
        sceneInfo.camera_zoom, text, cursorPosition);
}

QRegion caretRegion(const SnowSceneDisplayItem& item, const SnowCanvasTextDraft& draft,
                    const QFont& baseFont, const SceneDisplayInfo& sceneInfo, int paddingPx) {
    const QRectF caretBounds =
        cursorRect(item, baseFont, sceneInfo, draft.displayText(), draft.displayCursorPosition());
    if (caretBounds.isEmpty()) {
        return {};
    }
    return QRegion(snow_canvas_render_geometry::alignedRectForBounds(caretBounds, paddingPx));
}

QRegion editingRegion(const SnowSceneDisplayItem& item, const SnowCanvasTextDraft& draft,
                      const QFont& baseFont, const SceneDisplayInfo& sceneInfo) {
    QRegion region = previewRegion(item, sceneInfo, 2);
    region += caretRegion(item, draft, baseFont, sceneInfo);
    return region;
}

int cursorPositionForViewPosition(const SnowSceneDisplayItem& item, const QFont& baseFont,
                                  const SceneDisplayInfo& sceneInfo, const QPointF& viewPosition,
                                  const QString& text) {
    return snow_canvas_text_edit_geometry::cursorPositionForViewPoint(
        item, baseFont, canvasToViewPoint(sceneInfo, QPointF(item.center_x, item.center_y)),
        sceneInfo.camera_zoom, viewPosition, text);
}

int movedCursorPosition(const SnowSceneDisplayItem& item, const QFont& baseFont,
                        const SceneDisplayInfo& sceneInfo, const QString& text, int cursorPosition,
                        QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode) {
    return snow_canvas_text_edit_geometry::movedCursorPosition(
        item, baseFont, sceneInfo.camera_zoom, text, cursorPosition, operation, mode);
}

QFont inputMethodFont(const SnowSceneDisplayItem& item, const QFont& baseFont,
                      const SceneDisplayInfo& sceneInfo) {
    return snow_canvas_text_layout::fontForItem(baseFont, item,
                                                qMax(0.0001, sceneInfo.camera_zoom));
}

void renderOverlay(QPainter& painter, const SnowSceneDisplayItem& item,
                   const SnowCanvasTextDraft& draft, const QFont& baseFont,
                   const SceneDisplayInfo& sceneInfo, bool caretVisible) {
    const QString renderedText = draft.displayText();
    const QPointF centerView = canvasToViewPoint(sceneInfo, QPointF(item.center_x, item.center_y));
    if (draft.hasSelection() && !draft.hasPreedit()) {
        snow_canvas_text_editor_overlay::renderSelection(
            painter, item, renderedText, draft.selectionStart(), draft.selectionEnd(), baseFont,
            centerView, sceneInfo.camera_zoom);
        snow_canvas_text_editor_overlay::renderSelectedText(
            painter, item, renderedText, draft.selectionStart(), draft.selectionEnd(), baseFont,
            centerView, sceneInfo.camera_zoom);
    }
    if (draft.hasPreedit()) {
        snow_canvas_text_editor_overlay::renderPreeditUnderline(
            painter, item, renderedText, draft.preeditStart(), draft.preeditLength(), baseFont,
            centerView, sceneInfo.camera_zoom);
    }
    if (caretVisible) {
        snow_canvas_text_editor_overlay::renderCaret(painter, item, renderedText,
                                                     draft.displayCursorPosition(), baseFont,
                                                     centerView, sceneInfo.camera_zoom);
    }
}

} // namespace snow_canvas_text_editor_view
