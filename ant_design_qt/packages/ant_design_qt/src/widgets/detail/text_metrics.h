#pragma once

#include <QFont>
#include <QFontMetricsF>
#include <QString>

#include <algorithm>
#include <cmath>

namespace adqt::widgets::detail {

inline qreal singleLineTextAdvance(const QFont& font, const QString& text) {
  return text.isEmpty() ? 0.0 : std::max<qreal>(0.0, QFontMetricsF(font).horizontalAdvance(text));
}

inline int singleLineTextAdvanceWidth(const QFont& font, const QString& text) {
  return std::max(0, static_cast<int>(std::ceil(singleLineTextAdvance(font, text))));
}

inline int singleLineTextWidth(const QFont& font, const QString& text) {
  if (text.isEmpty()) {
    return 0;
  }
  const QFontMetricsF metrics(font);
  const qreal width = std::max(metrics.horizontalAdvance(text), metrics.boundingRect(text).width());
  return std::max(0, static_cast<int>(std::ceil(width)));
}

inline QString elidedSingleLineText(const QFont& font, const QString& text, qreal availableWidth) {
  if (text.isEmpty() || availableWidth <= 0.0) {
    return QString();
  }
  const QFontMetricsF metrics(font);
  if (metrics.horizontalAdvance(text) <= availableWidth) {
    return text;
  }
  return metrics.elidedText(text, Qt::ElideRight, availableWidth);
}

}  // namespace adqt::widgets::detail
