#pragma once

#include <QFont>
#include <QPointF>
#include <QString>

#include "snow_draw_engine.h"

class QPainter;

namespace snow_canvas_text_editor_overlay {

void renderSelection(QPainter& painter, const SnowSceneDisplayItem& item, const QString& text,
                     int selectionStart, int selectionEnd, const QFont& baseFont,
                     const QPointF& centerView, double zoom);
void renderSelectedText(QPainter& painter, const SnowSceneDisplayItem& item, const QString& text,
                        int selectionStart, int selectionEnd, const QFont& baseFont,
                        const QPointF& centerView, double zoom);
void renderCaret(QPainter& painter, const SnowSceneDisplayItem& item, const QString& text,
                 int cursorPosition, const QFont& baseFont, const QPointF& centerView, double zoom);
void renderPreeditUnderline(QPainter& painter, const SnowSceneDisplayItem& item,
                            const QString& text, int preeditStart, int preeditLength,
                            const QFont& baseFont, const QPointF& centerView, double zoom);

} // namespace snow_canvas_text_editor_overlay
