#include "popover_style.h"

#include "theme/theme.h"

#include <algorithm>

namespace adqt::widgets::detail {

namespace {

QColor toColor(const QColor& value, const QColor& fallback) {
  return value.isValid() ? value : fallback;
}

}  // namespace

PopoverVisualStyle resolvePopoverVisualStyle(const PopoverStyleInput& input,
                                             const adqt::theme::ResolvedTheme& resolved) {
  const adqt::theme::AdThemePalette& colors = resolved.theme.palette;
  const adqt::theme::AdThemeMetrics& themeMetrics = resolved.theme.metrics;

  PopoverVisualStyle style;
  style.backgroundColor = toColor(colors.colorBgElevated, QColor("#ffffff"));
  style.borderColor = toColor(colors.colorBorderSecondary, QColor("#f0f0f0"));
  style.titleColor = toColor(colors.colorText, QColor("#141414"));
  style.textColor = toColor(colors.colorText, QColor("#141414"));

  style.metrics.titleMinimumWidth = 0;
  style.metrics.maximumWidth = QWIDGETSIZE_MAX;
  style.metrics.zIndex = static_cast<int>(std::round(themeMetrics.popupZIndexBase + 30.0));
  style.metrics.cornerRadius = std::max(0, qRound(themeMetrics.borderRadiusLG));
  style.metrics.borderWidth = std::max(0, qRound(themeMetrics.lineWidth));
  style.metrics.arrowSize = std::max(6, qRound(themeMetrics.popupArrowSize / 2.0));
  style.metrics.popupOffset = std::max(2, qRound(themeMetrics.sizeXXS));
  style.metrics.sectionSpacing = std::max(0, qRound(themeMetrics.sizeXS));
  style.metrics.contentMargins = QMargins(12, 12, 12, 12);
  style.metrics.titleFont = input.baseFont;
  style.metrics.textFont = input.baseFont;
  style.metrics.titleFont.setPixelSize(std::max(12, qRound(themeMetrics.fontSize)));
  style.metrics.titleFont.setWeight(QFont::DemiBold);
  style.metrics.textFont.setPixelSize(std::max(12, qRound(themeMetrics.fontSize)));
  style.metrics.textFont.setWeight(QFont::Normal);

  const PopoverStyleOverrides& overrides = input.overrides;
  if (overrides.titleFont.has_value()) {
    style.metrics.titleFont = overrides.titleFont.value();
  }
  if (overrides.textFont.has_value()) {
    style.metrics.textFont = overrides.textFont.value();
  }
  if (overrides.backgroundColor.has_value()) {
    style.backgroundColor = toColor(overrides.backgroundColor.value(), style.backgroundColor);
  }
  if (overrides.borderColor.has_value()) {
    style.borderColor = toColor(overrides.borderColor.value(), style.borderColor);
  }
  if (overrides.titleColor.has_value()) {
    style.titleColor = toColor(overrides.titleColor.value(), style.titleColor);
  }
  if (overrides.textColor.has_value()) {
    style.textColor = toColor(overrides.textColor.value(), style.textColor);
  }
  if (overrides.contentMargins.has_value()) {
    style.metrics.contentMargins = overrides.contentMargins.value();
  }
  if (overrides.titleMinimumWidth.has_value()) {
    style.metrics.titleMinimumWidth = std::max(0, overrides.titleMinimumWidth.value());
  }
  if (overrides.maximumWidth.has_value()) {
    style.metrics.maximumWidth = std::max(1, overrides.maximumWidth.value());
  }
  if (overrides.zIndex.has_value()) {
    style.metrics.zIndex = std::max(0, overrides.zIndex.value());
  }
  if (overrides.cornerRadius.has_value()) {
    style.metrics.cornerRadius = std::max(0, overrides.cornerRadius.value());
  }
  if (overrides.borderWidth.has_value()) {
    style.metrics.borderWidth = std::max(0, overrides.borderWidth.value());
  }
  if (overrides.arrowSize.has_value()) {
    style.metrics.arrowSize = std::max(0, overrides.arrowSize.value());
  }
  if (overrides.popupOffset.has_value()) {
    style.metrics.popupOffset = std::max(0, overrides.popupOffset.value());
  }

  if (input.disabled) {
    const QColor disabledText = toColor(colors.colorTextQuaternary, QColor("#bfbfbf"));
    style.titleColor = disabledText;
    style.textColor = disabledText;
  }

  return style;
}

PopoverVisualStyle resolvePopoverVisualStyle(const PopoverStyleInput& input) {
  const adqt::theme::ResolvedTheme resolved =
      adqt::theme::makeResolvedTheme(adqt::theme::makeTheme());
  return resolvePopoverVisualStyle(input, resolved);
}

}  // namespace adqt::widgets::detail
