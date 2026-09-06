#include "form_style.h"

#include "theme/theme.h"

#include <algorithm>

namespace adqt::widgets::detail {

namespace {

QColor toColor(const QColor& value, const QColor& fallback) {
  return value.isValid() ? value : fallback;
}

QColor paletteColor(const QPalette& palette, QPalette::ColorRole role, QPalette::ColorGroup group,
                    const QColor& fallback) {
  const QColor color = palette.color(group, role);
  return color.isValid() ? color : fallback;
}

int controlHeightForSize(const adqt::theme::ThemeMetrics& metrics, AdForm::ControlSize size) {
  switch (size) {
    case AdForm::ControlSize::Large:
      return std::max(28, qRound(metrics.controlHeightLG));
    case AdForm::ControlSize::Small:
      return std::max(22, qRound(metrics.controlHeightSM));
    case AdForm::ControlSize::Medium:
    default:
      return std::max(24, qRound(metrics.controlHeight));
  }
}

}  // namespace

FormVisualStyle resolveFormVisualStyle(const FormStyleInput& input,
                                       const adqt::theme::ResolvedTheme& resolved) {
  const adqt::theme::ThemeColors& colors = resolved.theme.palette;
  const adqt::theme::ThemeMetrics& metrics = resolved.theme.metrics;

  FormVisualStyle style;
  style.labelColor = toColor(colors.colorText, QColor("#141414"));
  style.requiredMarkColor = toColor(colors.colorError, QColor("#ff4d4f"));
  style.optionalColor = toColor(colors.colorTextTertiary, QColor("#8c8c8c"));
  style.messageColor = toColor(colors.colorTextTertiary, QColor("#8c8c8c"));
  style.errorColor = toColor(colors.colorError, QColor("#ff4d4f"));
  style.warningColor = toColor(colors.colorWarning, QColor("#faad14"));
  style.successColor = toColor(colors.colorSuccess, QColor("#52c41a"));
  style.validatingColor = toColor(colors.colorPrimary, QColor("#1677ff"));
  style.disabledColor = toColor(colors.colorTextDisabled, QColor("#bfbfbf"));

  style.metrics.labelHeight = controlHeightForSize(metrics, input.controlSize);
  style.metrics.controlMinHeight = style.metrics.labelHeight;
  style.metrics.itemMarginBottom = std::max(12, qRound(metrics.sizeLG));
  style.metrics.inlineItemMarginBottom = 0;
  style.metrics.inlineItemGap = std::max(8, qRound(metrics.size));
  style.metrics.verticalLabelPaddingBottom = std::max(4, qRound(metrics.sizeXS));
  style.metrics.requiredMarkGap = std::max(2, qRound(metrics.sizeXXS));
  style.metrics.optionalMarkGap = std::max(2, qRound(metrics.sizeXXS));
  style.metrics.colonMarginInlineStart = std::max(1, qRound(metrics.sizeXXS / 2.0));
  style.metrics.colonMarginInlineEnd = std::max(4, qRound(metrics.sizeXS));
  style.metrics.feedbackIconSize = std::max(12, qRound(metrics.fontSize));
  style.metrics.feedbackIconGap = std::max(4, qRound(metrics.sizeXS));
  style.metrics.messageMinHeight = std::max(18, qRound(metrics.controlHeightSM));
  style.metrics.messageLineHeight = std::max(16, qRound(metrics.fontHeight));

  style.metrics.labelFont = input.baseFont;
  style.metrics.labelFont.setPixelSize(std::max(12, qRound(metrics.fontSize)));
  style.metrics.labelFont.setWeight(QFont::Normal);

  style.metrics.messageFont = input.baseFont;
  style.metrics.messageFont.setPixelSize(std::max(12, qRound(metrics.fontSize)));
  style.metrics.messageFont.setWeight(QFont::Normal);

  if (!input.enabled) {
    style.labelColor = style.disabledColor;
    style.optionalColor = style.disabledColor;
    style.messageColor = style.disabledColor;
    style.errorColor = style.disabledColor;
    style.warningColor = style.disabledColor;
    style.successColor = style.disabledColor;
    style.validatingColor = style.disabledColor;
  }

  if (input.hasPaletteOverride) {
    const QPalette::ColorGroup group = input.enabled ? input.paletteGroup : QPalette::Disabled;
    style.labelColor = paletteColor(input.palette, QPalette::WindowText, group, style.labelColor);
    style.requiredMarkColor =
        paletteColor(input.palette, QPalette::Highlight, group, style.requiredMarkColor);
    style.optionalColor = paletteColor(input.palette, QPalette::Mid, group, style.optionalColor);
    style.messageColor = paletteColor(input.palette, QPalette::Text, group, style.messageColor);
    style.errorColor = paletteColor(input.palette, QPalette::BrightText, group, style.errorColor);
    style.warningColor =
        paletteColor(input.palette, QPalette::ToolTipText, group, style.warningColor);
    style.successColor =
        paletteColor(input.palette, QPalette::Highlight, group, style.successColor);
    style.validatingColor =
        paletteColor(input.palette, QPalette::Highlight, group, style.validatingColor);
  }

  return style;
}

FormVisualStyle resolveFormVisualStyle(const FormStyleInput& input) {
  const adqt::theme::ResolvedTheme resolved =
      adqt::theme::makeResolvedTheme(adqt::theme::makeTheme());
  return resolveFormVisualStyle(input, resolved);
}

}  // namespace adqt::widgets::detail
