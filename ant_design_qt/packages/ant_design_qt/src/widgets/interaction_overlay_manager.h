#pragma once

#include <QColor>
#include <QRectF>
#include <QtGlobal>

class QWidget;

namespace adqt::widgets {

struct InteractionWaveRequest {
  const QWidget* owner = nullptr;
  QRectF baseRectInWindow;
  qreal topLeft = 0.0;
  qreal topRight = 0.0;
  qreal bottomRight = 0.0;
  qreal bottomLeft = 0.0;
  QColor color;
  qreal strokeWidthScale = 1.0;
};

struct InteractionFocusRequest {
  const QWidget* owner = nullptr;
  QRectF baseRectInWindow;
  qreal topLeft = 0.0;
  qreal topRight = 0.0;
  qreal bottomRight = 0.0;
  qreal bottomLeft = 0.0;
  QColor color;
  qreal strokeWidth = 0.0;
  qreal offset = 0.0;
};

void triggerInteractionWave(const InteractionWaveRequest& request);
void stopInteractionWaveForOwner(const QWidget* owner);
void triggerInteractionFocus(const InteractionFocusRequest& request);
void stopInteractionFocusForOwner(const QWidget* owner);

}  // namespace adqt::widgets
