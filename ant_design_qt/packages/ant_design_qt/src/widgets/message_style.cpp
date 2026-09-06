#include "message_style.h"

#include "theme/theme.h"

#include <algorithm>

namespace adqt::widgets::detail {

namespace {

QColor validOr(const QColor& value, const QColor& fallback) {
  return value.isValid() ? value : fallback;
}

}  // namespace

MessageVisualStyle resolveMessageVisualStyle(AdMessage::Type type, const QFont& baseFont,
                                             const AdMessage::ComponentTokens& componentTokens,
                                             const adqt::theme::ResolvedTheme& resolved) {
  const adqt::theme::AdThemePalette& colors = resolved.theme.palette;
  const adqt::theme::AdThemeMetrics& metrics = resolved.theme.metrics;
  const adqt::theme::AdThemeMotion& motion = resolved.theme.motion;

  MessageVisualStyle style;
  style.contentBackground = validOr(colors.colorBgElevated, QColor("#ffffff"));
  style.contentTextColor = validOr(colors.colorText, QColor("#141414"));
  style.iconColor = validOr(colors.colorInfo, QColor("#1677ff"));
  style.borderColor = QColor(Qt::transparent);

  switch (type) {
    case AdMessage::Type::Success:
      style.iconColor = validOr(colors.colorSuccess, QColor("#52c41a"));
      break;
    case AdMessage::Type::Warning:
      style.iconColor = validOr(colors.colorWarning, QColor("#faad14"));
      break;
    case AdMessage::Type::Error:
      style.iconColor = validOr(colors.colorError, QColor("#ff4d4f"));
      break;
    case AdMessage::Type::Info:
    case AdMessage::Type::Loading:
      style.iconColor = validOr(colors.colorInfo, QColor("#1677ff"));
      break;
    case AdMessage::Type::None:
      break;
  }

  style.metrics.zIndexPopup = std::max(0, metrics.popupZIndexBase + 1010);
  style.metrics.contentPaddingHorizontal = std::max(0, qRound(metrics.sizeSM));
  const qreal contentLineHeight = metrics.fontSize * metrics.lineHeight;
  style.metrics.contentPaddingVertical =
      std::max(0, qRound((metrics.controlHeightLG - contentLineHeight) / 2.0));
  style.metrics.borderRadius = std::max(0, qRound(metrics.borderRadiusLG));
  style.metrics.iconSize = std::max(12, qRound(metrics.fontSizeLG));
  style.metrics.iconContentGap = std::max(0, qRound(metrics.sizeXS));
  style.metrics.contentLineHeight = std::max(1, qRound(contentLineHeight));
  style.metrics.noticePadding = std::max(0, qRound(metrics.sizeXS));
  style.metrics.motionDurationMs = motion.motion ? std::max(0, motion.motionDurationSlow) : 0;
  style.metrics.spinnerCycleMs = std::max(1, motion.timingSpinnerCycleMs);
  style.metrics.motionEasing = motion.motionEaseInOutCirc;
  style.metrics.contentFont = baseFont;
  style.metrics.contentFont.setPixelSize(std::max(12, qRound(metrics.fontSize)));
  style.metrics.contentFont.setWeight(QFont::Normal);

  if (componentTokens.zIndexPopup.has_value()) {
    style.metrics.zIndexPopup = std::max(0, componentTokens.zIndexPopup.value());
  }
  if (componentTokens.contentBg.has_value() && componentTokens.contentBg->isValid()) {
    style.contentBackground = componentTokens.contentBg.value();
  }
  if (componentTokens.contentPaddingHorizontal.has_value()) {
    style.metrics.contentPaddingHorizontal =
        std::max(0, componentTokens.contentPaddingHorizontal.value());
  }
  if (componentTokens.contentPaddingVertical.has_value()) {
    style.metrics.contentPaddingVertical =
        std::max(0, componentTokens.contentPaddingVertical.value());
  }
  if (componentTokens.borderRadius.has_value()) {
    style.metrics.borderRadius = std::max(0, componentTokens.borderRadius.value());
  }
  if (componentTokens.iconSize.has_value()) {
    style.metrics.iconSize = std::max(1, componentTokens.iconSize.value());
  }
  if (componentTokens.iconContentGap.has_value()) {
    style.metrics.iconContentGap = std::max(0, componentTokens.iconContentGap.value());
  }

  return style;
}

}  // namespace adqt::widgets::detail
