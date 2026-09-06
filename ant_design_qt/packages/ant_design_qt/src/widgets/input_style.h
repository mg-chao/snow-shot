#pragma once

#include <QColor>
#include <QFont>

#include "input_line_edit.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct InputMetrics {
  int height = 32;
  int borderRadius = 6;
  int borderWidth = 1;
  int horizontalPadding = 11;
  int verticalPadding = 4;
  int textLineHeight = 22;
  int affixPadding = 4;
  int affixItemGap = 8;
  int affixIconSize = 14;
  int clearIconSize = 12;
  int multilineAffixTopInset = 8;
  int multilineInlineStartCompensation = 2;
  int countTopMargin = 4;
  int countHeight = 20;
  qreal focusOutlineWidth = 3.0;
  qreal focusOutlineOffset = 0.0;
  QFont font;
};

struct InputVisualStyle {
  QColor selectorBg;
  QColor selectorHoverBg;
  QColor selectorActiveBg;
  QColor selectorBorderColor;
  QColor selectorHoverBorderColor;
  QColor selectorActiveBorderColor;
  QColor selectorFocusOutlineColor;
  QColor selectorTextColor;
  QColor placeholderColor;
  QColor clearColor;
  QColor clearHoverColor;
  QColor clearActiveColor;
  QColor prefixColor;
  QColor suffixColor;
  QColor suffixActionColor;
  QColor suffixActionHoverColor;
  QColor countColor;
  QColor disabledTextColor;
  QColor disabledBg;
  QColor disabledBorderColor;
  bool underlined = false;
  InputMetrics metrics;
};

struct InputStyleInput {
  AdLineEdit::ControlSize controlSize = AdLineEdit::ControlSize::Medium;
  AdLineEdit::Variant variant = AdLineEdit::Variant::Outlined;
  AdLineEdit::Status status = AdLineEdit::Status::None;
  bool disabled = false;
  bool focused = false;
  bool hovered = false;
  bool multiline = false;
  QFont baseFont;
};

using TextControlMetrics = InputMetrics;
using TextControlVisualStyle = InputVisualStyle;
using TextControlStyleInput = InputStyleInput;

InputVisualStyle resolveInputVisualStyle(const InputStyleInput& input,
                                         const adqt::theme::ResolvedTheme& resolvedTheme);
InputVisualStyle resolveInputVisualStyle(const InputStyleInput& input);
TextControlVisualStyle resolveTextControlVisualStyle(
    const TextControlStyleInput& input, const adqt::theme::ResolvedTheme& resolvedTheme);
TextControlVisualStyle resolveTextControlVisualStyle(const TextControlStyleInput& input);

}  // namespace adqt::widgets::detail
