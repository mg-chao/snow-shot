#pragma once

#include "snow_canvas_display_cache.h"
#include "snow_canvas_text_draft.h"
#include "snow_draw_engine.h"

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QRegion>
#include <QTextCursor>

class QPainter;

namespace snow_canvas_text_editor_view {

QPointF canvasToViewPoint(const SceneDisplayInfo& sceneInfo, const QPointF& canvasPosition);
bool itemContainsViewPosition(const SnowSceneDisplayItem& item, const SceneDisplayInfo& sceneInfo,
                              const QPointF& viewPosition);
QRegion previewRegion(const SnowSceneDisplayItem& item, const SceneDisplayInfo& sceneInfo,
                      int paddingPx = 2);
QRectF cursorRect(const SnowSceneDisplayItem& item, const QFont& baseFont,
                  const SceneDisplayInfo& sceneInfo, const QString& text, int cursorPosition);
QRegion caretRegion(const SnowSceneDisplayItem& item, const SnowCanvasTextDraft& draft,
                    const QFont& baseFont, const SceneDisplayInfo& sceneInfo, int paddingPx = 3);
QRegion editingRegion(const SnowSceneDisplayItem& item, const SnowCanvasTextDraft& draft,
                      const QFont& baseFont, const SceneDisplayInfo& sceneInfo);
int cursorPositionForViewPosition(const SnowSceneDisplayItem& item, const QFont& baseFont,
                                  const SceneDisplayInfo& sceneInfo, const QPointF& viewPosition,
                                  const QString& text);
int movedCursorPosition(const SnowSceneDisplayItem& item, const QFont& baseFont,
                        const SceneDisplayInfo& sceneInfo, const QString& text, int cursorPosition,
                        QTextCursor::MoveOperation operation,
                        QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
QFont inputMethodFont(const SnowSceneDisplayItem& item, const QFont& baseFont,
                      const SceneDisplayInfo& sceneInfo);
void renderOverlay(QPainter& painter, const SnowSceneDisplayItem& item,
                   const SnowCanvasTextDraft& draft, const QFont& baseFont,
                   const SceneDisplayInfo& sceneInfo, bool caretVisible = true);

} // namespace snow_canvas_text_editor_view
