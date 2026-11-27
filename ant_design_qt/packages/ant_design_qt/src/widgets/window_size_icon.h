// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QPainter>
#include <QPainterPath>

namespace adqt::widgets {

// Paint in physical pixels so caption strokes stay crisp at fractional DPI.
inline void paintWindowSizeIcon(QPainter& painter, int extent, int stroke, int offset,
                                bool maximized) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(Qt::NoBrush);
  const qreal inset = stroke / 2.0;
  const qreal side = extent - stroke;
  if (maximized) {
    QPainterPath rear;
    rear.addRoundedRect(QRectF(offset + inset, inset, side - offset, side - offset), 1, 1);
    painter.save();
    QPainterPath exposed;
    exposed.setFillRule(Qt::WindingFill);
    exposed.addRect(QRectF(0, 0, extent, offset));
    exposed.addRect(QRectF(extent - offset, 0, offset, extent));
    painter.setClipPath(exposed, Qt::IntersectClip);
    painter.drawPath(rear);
    painter.restore();
    painter.drawRoundedRect(QRectF(inset, offset + inset, side - offset, side - offset), 1, 1);
  } else {
    painter.drawRoundedRect(QRectF(inset, inset, side, side), 1, 1);
  }
  painter.restore();
}

}  // namespace adqt::widgets
