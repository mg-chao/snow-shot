#pragma once

#include "notification.h"

#include <QColor>
#include <QEasingCurve>
#include <QFont>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct NotificationMetrics {
  int zIndexPopup = 2050;
  int width = 384;
  int paddingHorizontal = 24;
  int paddingVertical = 16;
  int borderRadius = 8;
  int borderWidth = 0;
  int iconSize = 24;
  int iconContentGap = 12;
  int titleDescriptionGap = 8;
  int titleLineHeight = 24;
  int descriptionLineHeight = 22;
  int closeButtonSize = 22;
  qreal focusOutlineWidth = 2.0;
  int marginBottom = 16;
  int edgeMargin = 24;
  int progressHeight = 2;
  int stackOffset = 8;
  int stackGap = 16;
  int motionDurationMs = 300;
  QFont titleFont;
  QFont descriptionFont;
  QEasingCurve motionEasing = QEasingCurve(QEasingCurve::InOutCubic);
};

struct NotificationVisualStyle {
  QColor backgroundColor;
  QColor titleColor;
  QColor descriptionColor;
  QColor iconColor;
  QColor closeColor;
  QColor closeHoverColor;
  QColor closeHoverBackground;
  QColor closeFocusColor;
  QColor progressTrackColor;
  QColor progressColor;
  QColor borderColor;
  NotificationMetrics metrics;
};

NotificationVisualStyle resolveNotificationVisualStyle(
    AdNotification::Type type, const QFont& baseFont,
    const AdNotification::ComponentTokens& componentTokens,
    const adqt::theme::ResolvedTheme& resolvedTheme);

}  // namespace adqt::widgets::detail
