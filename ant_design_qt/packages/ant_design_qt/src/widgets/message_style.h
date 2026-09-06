#pragma once

#include "message.h"

#include <QColor>
#include <QEasingCurve>
#include <QFont>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct MessageMetrics {
  int zIndexPopup = 2010;
  int contentPaddingHorizontal = 12;
  int contentPaddingVertical = 9;
  int borderRadius = 8;
  int borderWidth = 0;
  int iconSize = 16;
  int iconContentGap = 8;
  int contentLineHeight = 22;
  int noticePadding = 8;
  int motionDurationMs = 300;
  int spinnerCycleMs = 1000;
  QFont contentFont;
  QEasingCurve motionEasing = QEasingCurve(QEasingCurve::InOutCubic);
};

struct MessageVisualStyle {
  QColor contentBackground;
  QColor contentTextColor;
  QColor iconColor;
  QColor borderColor;
  MessageMetrics metrics;
};

MessageVisualStyle resolveMessageVisualStyle(AdMessage::Type type, const QFont& baseFont,
                                             const AdMessage::ComponentTokens& componentTokens,
                                             const adqt::theme::ResolvedTheme& resolvedTheme);

}  // namespace adqt::widgets::detail
