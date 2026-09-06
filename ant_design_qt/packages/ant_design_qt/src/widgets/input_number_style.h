#pragma once

#include <QColor>
#include <QFont>

#include "input_number.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct InputNumberMetrics {
  int height = 32;
  int width = 90;
  int borderRadius = 6;
  int borderWidth = 1;
  int horizontalPadding = 11;
  int affixPadding = 4;
  int affixItemGap = 8;
  int iconSize = 12;
  int handleIconSize = 7;
  int splitIconSize = 14;
  int inputFontSize = 14;
  int handleWidth = 22;
  int handleVisibleWidth = 0;
  qreal focusOutlineWidth = 3.0;
  qreal focusOutlineOffset = 0.0;
  QFont font;
};

struct InputNumberVisualStyle {
  QColor selectorBg;
  QColor selectorHoverBg;
  QColor selectorActiveBg;
  QColor selectorBorderColor;
  QColor selectorHoverBorderColor;
  QColor selectorActiveBorderColor;
  QColor selectorFocusOutlineColor;
  QColor selectorTextColor;
  QColor placeholderColor;
  QColor prefixColor;
  QColor suffixColor;
  QColor handleBg;
  QColor handleActiveBg;
  QColor handleBorderColor;
  QColor handleHoverColor;
  QColor handleIconColor;
  QColor outOfRangeTextColor;
  QColor disabledTextColor;
  QColor disabledBg;
  QColor disabledBorderColor;
  bool underlined = false;
  InputNumberMetrics metrics;
};

struct InputNumberStyleInput {
  AdInputNumber::ControlSize controlSize = AdInputNumber::ControlSize::Medium;
  AdInputNumber::Variant variant = AdInputNumber::Variant::Outlined;
  AdInputNumber::Status status = AdInputNumber::Status::None;
  AdInputNumber::StepButtonLayout stepButtonLayout = AdInputNumber::StepButtonLayout::Compact;
  AdInputNumber::ValueMode valueMode = AdInputNumber::ValueMode::Number;
  bool disabled = false;
  bool readOnly = false;
  bool focused = false;
  bool hovered = false;
  bool stepButtonsVisible = true;
  bool outOfRange = false;
  QFont baseFont;
  AdInputNumber::AppearanceOverrides appearanceOverrides;
};

InputNumberVisualStyle resolveInputNumberVisualStyle(
    const InputNumberStyleInput& input, const adqt::theme::ResolvedTheme& resolvedTheme);
InputNumberVisualStyle resolveInputNumberVisualStyle(const InputNumberStyleInput& input);

}  // namespace adqt::widgets::detail
