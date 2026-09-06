#include "segmented_style.h"

#include <algorithm>

#include "theme/theme.h"

namespace adqt::widgets::detail {

namespace {

QColor colorOr(const std::optional<QColor>& value, const QColor& fallback) {
  return value && value->isValid() ? *value : fallback;
}

int intOr(const std::optional<int>& value, int fallback, int minimum = 0) {
  return std::max(minimum, value.value_or(fallback));
}

}  // namespace

SegmentedAppearance resolveSegmentedAppearance(const AdSegmented* segmented,
                                               const AdSegmented::ComponentTokens& tokens) {
  const adqt::theme::ThemeMapToken theme =
      adqt::theme::ThemeManager::instance().resolveTheme(segmented);

  SegmentedAppearance result;
  result.itemColor = colorOr(tokens.colors.itemColor, theme.colorTextSecondary);
  result.itemHoverColor = colorOr(tokens.colors.itemHoverColor, theme.colorText);
  result.itemHoverBackground = colorOr(tokens.colors.itemHoverBackground, theme.colorFillSecondary);
  result.itemActiveBackground = colorOr(tokens.colors.itemActiveBackground, theme.colorFill);
  result.itemSelectedBackground =
      colorOr(tokens.colors.itemSelectedBackground, theme.colorBgElevated);
  result.itemSelectedColor = colorOr(tokens.colors.itemSelectedColor, theme.colorText);
  result.itemDisabledColor = colorOr(tokens.colors.itemDisabledColor, theme.colorTextDisabled);
  result.trackBackground = colorOr(tokens.colors.trackBackground, theme.colorFillSecondary);
  result.focusOutline = colorOr(tokens.colors.focusOutline, theme.colorPrimaryBorder);
  result.thumbShadow = colorOr(
      tokens.colors.thumbShadow,
      theme.scheme == adqt::theme::ThemeScheme::Dark ? QColor(0, 0, 0, 90) : QColor(0, 0, 0, 20));

  int controlHeight = qRound(theme.controlHeight);
  int fontPixels = qRound(theme.fontSize);
  int itemRadius = qRound(theme.borderRadiusSM);
  if (segmented->controlSize() == AdSegmented::ControlSize::Small) {
    controlHeight = qRound(theme.controlHeightSM);
    itemRadius = qRound(theme.borderRadiusXS);
  } else if (segmented->controlSize() == AdSegmented::ControlSize::Large) {
    controlHeight = qRound(theme.controlHeightLG);
    fontPixels = qRound(theme.fontSizeLG);
    itemRadius = qRound(theme.borderRadius);
  }

  result.metrics.controlHeight = std::max(20, controlHeight);
  result.metrics.trackPadding = intOr(tokens.metrics.trackPadding, qRound(theme.lineWidthBold));
  const int controlPadding = segmented->controlSize() == AdSegmented::ControlSize::Small ? 8 : 12;
  const int horizontalPadding = std::max(0, controlPadding - result.metrics.trackPadding);
  result.metrics.horizontalPadding =
      intOr(segmented->controlSize() == AdSegmented::ControlSize::Small
                ? tokens.metrics.smallHorizontalPadding
                : tokens.metrics.horizontalPadding,
            horizontalPadding);
  result.metrics.borderRadius = intOr(
      tokens.metrics.borderRadius,
      qRound(segmented->controlSize() == AdSegmented::ControlSize::Large   ? theme.borderRadiusLG
             : segmented->controlSize() == AdSegmented::ControlSize::Small ? theme.borderRadiusSM
                                                                           : theme.borderRadius));
  result.metrics.itemBorderRadius = std::max(0, itemRadius);
  result.metrics.iconSize = intOr(tokens.metrics.iconSize, fontPixels, 8);
  result.metrics.iconGap = intOr(tokens.metrics.iconGap, qRound(theme.sizeXS / 2.0));
  result.metrics.focusOutlineWidth =
      intOr(tokens.metrics.focusOutlineWidth, std::max(2, qRound(theme.lineWidthBold)), 1);
  result.metrics.focusOutlineOffset = intOr(tokens.metrics.focusOutlineOffset, 1);
  result.metrics.thumbShadowOffsetY = intOr(tokens.metrics.thumbShadowOffsetY, 1);
  result.metrics.animationDurationMs = theme.motion ? std::max(0, theme.motionDurationMid) : 0;
  result.metrics.font = segmented->font();
  result.metrics.font.setPixelSize(std::max(10, fontPixels));
  return result;
}

}  // namespace adqt::widgets::detail
