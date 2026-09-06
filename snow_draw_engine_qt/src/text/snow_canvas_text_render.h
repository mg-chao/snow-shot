#pragma once

#include <QFont>
#include <QColor>
#include <QRectF>

#include "snow_draw_engine.h"

class QPainter;

namespace snow_canvas_text_render {

void drawContents(QPainter& painter, const SnowSceneDisplayItem& item, const QFont& baseFont,
                  const QRectF& localRect, double zoom);
void drawBackground(QPainter& painter, const SnowSceneDisplayItem& item, const QFont& baseFont,
                    const QRectF& localRect, double zoom);
void drawStroke(QPainter& painter, const SnowSceneDisplayItem& item, const QFont& baseFont,
                const QRectF& localRect, double zoom);
void drawHoverUnderlines(QPainter& painter, const SnowSceneDisplayItem& item, const QFont& baseFont,
                         const QRectF& localRect, double zoom, const QColor& color,
                         double strokeWidth);

} // namespace snow_canvas_text_render
