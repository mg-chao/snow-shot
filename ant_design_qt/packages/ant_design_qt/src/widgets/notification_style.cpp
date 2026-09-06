#include "notification_style.h"

#include "theme/theme.h"

#include <algorithm>

namespace adqt::widgets::detail {

namespace {

QColor validOr(const QColor& value, const QColor& fallback) {
  return value.isValid() ? value : fallback;
}

}  // namespace

NotificationVisualStyle resolveNotificationVisualStyle(
    AdNotification::Type type, const QFont& baseFont, const AdNotification::ComponentTokens& tokens,
    const adqt::theme::ResolvedTheme& resolved) {
  const adqt::theme::AdThemePalette& colors = resolved.theme.palette;
  const adqt::theme::AdThemeMetrics& metrics = resolved.theme.metrics;
  const adqt::theme::AdThemeMotion& motion = resolved.theme.motion;

  NotificationVisualStyle style;
  style.backgroundColor = validOr(colors.colorBgElevated, QColor("#ffffff"));
  style.titleColor = validOr(colors.colorText, QColor("#000000e0"));
  style.descriptionColor = validOr(colors.colorText, QColor("#000000e0"));
  style.iconColor = validOr(colors.colorInfo, QColor("#1677ff"));
  style.closeColor = validOr(colors.colorTextTertiary, QColor("#00000073"));
  style.closeHoverColor = validOr(colors.colorText, QColor("#000000e0"));
  style.closeHoverBackground = validOr(colors.colorFillTertiary, QColor("#0000000f"));
  style.closeFocusColor = validOr(colors.colorPrimaryBorder, QColor("#91caff"));
  style.progressTrackColor = QColor(0, 0, 0, 10);
  style.progressColor = validOr(colors.colorPrimary, QColor("#1677ff"));
  style.borderColor = QColor(Qt::transparent);
  if (tokens.backgroundColor.has_value() && tokens.backgroundColor->isValid()) {
    style.backgroundColor = tokens.backgroundColor.value();
  }

  switch (type) {
    case AdNotification::Type::Success:
      style.iconColor = validOr(colors.colorSuccess, QColor("#52c41a"));
      if (tokens.successBackgroundColor.has_value() && tokens.successBackgroundColor->isValid()) {
        style.backgroundColor = tokens.successBackgroundColor.value();
      }
      break;
    case AdNotification::Type::Warning:
      style.iconColor = validOr(colors.colorWarning, QColor("#faad14"));
      if (tokens.warningBackgroundColor.has_value() && tokens.warningBackgroundColor->isValid()) {
        style.backgroundColor = tokens.warningBackgroundColor.value();
      }
      break;
    case AdNotification::Type::Error:
      style.iconColor = validOr(colors.colorError, QColor("#ff4d4f"));
      if (tokens.errorBackgroundColor.has_value() && tokens.errorBackgroundColor->isValid()) {
        style.backgroundColor = tokens.errorBackgroundColor.value();
      }
      break;
    case AdNotification::Type::Info:
      if (tokens.infoBackgroundColor.has_value() && tokens.infoBackgroundColor->isValid()) {
        style.backgroundColor = tokens.infoBackgroundColor.value();
      }
      break;
    case AdNotification::Type::None:
      break;
  }

  style.metrics.zIndexPopup = std::max(0, metrics.popupZIndexBase + 1050);
  style.metrics.width = 384;
  style.metrics.paddingHorizontal = std::max(0, qRound(metrics.sizeLG));
  style.metrics.paddingVertical = std::max(0, qRound(metrics.size));
  style.metrics.borderRadius = std::max(0, qRound(metrics.borderRadiusLG));
  style.metrics.iconSize = std::max(16, qRound(metrics.fontSizeLG * metrics.lineHeightLG));
  style.metrics.iconContentGap = std::max(0, qRound(metrics.sizeSM));
  style.metrics.titleDescriptionGap = std::max(0, qRound(metrics.sizeXS));
  style.metrics.titleLineHeight = std::max(1, qRound(metrics.fontSizeLG * metrics.lineHeightLG));
  style.metrics.descriptionLineHeight = std::max(1, qRound(metrics.fontSize * metrics.lineHeight));
  style.metrics.closeButtonSize = std::max(18, qRound(metrics.controlHeightLG * 0.55));
  style.metrics.focusOutlineWidth = std::max<qreal>(1.0, metrics.lineWidth * 2.0);
  style.metrics.marginBottom = std::max(0, qRound(metrics.size));
  style.metrics.edgeMargin = std::max(0, qRound(metrics.sizeLG));
  style.metrics.stackOffset = std::max(0, qRound(metrics.sizeXS));
  style.metrics.stackGap = std::max(0, qRound(metrics.size));
  style.metrics.motionDurationMs = motion.motion ? std::max(0, motion.motionDurationMid) : 0;
  style.metrics.motionEasing = motion.motionEaseInOutCirc;
  style.metrics.titleFont = baseFont;
  style.metrics.titleFont.setPixelSize(std::max(12, qRound(metrics.fontSizeLG)));
  style.metrics.titleFont.setWeight(QFont::Normal);
  style.metrics.descriptionFont = baseFont;
  style.metrics.descriptionFont.setPixelSize(std::max(12, qRound(metrics.fontSize)));
  style.metrics.descriptionFont.setWeight(QFont::Normal);

#define ADQT_NOTIFICATION_INT_TOKEN(name)                  \
  if (tokens.name.has_value()) {                           \
    style.metrics.name = std::max(0, tokens.name.value()); \
  }
  ADQT_NOTIFICATION_INT_TOKEN(zIndexPopup)
  ADQT_NOTIFICATION_INT_TOKEN(width)
  ADQT_NOTIFICATION_INT_TOKEN(paddingHorizontal)
  ADQT_NOTIFICATION_INT_TOKEN(paddingVertical)
  ADQT_NOTIFICATION_INT_TOKEN(borderRadius)
  ADQT_NOTIFICATION_INT_TOKEN(iconSize)
  ADQT_NOTIFICATION_INT_TOKEN(iconContentGap)
  ADQT_NOTIFICATION_INT_TOKEN(titleDescriptionGap)
  ADQT_NOTIFICATION_INT_TOKEN(marginBottom)
  ADQT_NOTIFICATION_INT_TOKEN(edgeMargin)
  ADQT_NOTIFICATION_INT_TOKEN(progressHeight)
  ADQT_NOTIFICATION_INT_TOKEN(stackOffset)
  ADQT_NOTIFICATION_INT_TOKEN(stackGap)
#undef ADQT_NOTIFICATION_INT_TOKEN

  if (tokens.progressColor.has_value() && tokens.progressColor->isValid()) {
    style.progressColor = tokens.progressColor.value();
  }
  return style;
}

}  // namespace adqt::widgets::detail
