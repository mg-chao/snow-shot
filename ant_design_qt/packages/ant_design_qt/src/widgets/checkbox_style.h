#pragma once

#include "checkbox.h"

#include <QColor>
#include <QFont>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct CheckboxStateStyle {
  QColor borderColor;
  QColor backgroundColor;
  QColor markColor;
  QColor labelColor;
};

struct CheckboxMetrics {
  int checkboxSize = 16;
  int borderWidth = 1;
  int borderRadius = 2;
  int markWidth = 2;
  int labelPaddingInlineStart = 8;
  int labelPaddingInlineEnd = 8;
  int textLineHeight = 22;
  int wrapperMarginInlineEnd = 8;
  QFont font;
  QColor focusOutlineColor;
  qreal focusOutlineWidth = 3.0;
  qreal focusOutlineOffset = 1.0;
  QColor waveColor;
};

struct CheckboxVisualStyle {
  CheckboxStateStyle normal;
  CheckboxStateStyle hover;
  CheckboxStateStyle checked;
  CheckboxStateStyle checkedHover;
  CheckboxStateStyle indeterminate;
  CheckboxStateStyle indeterminateHover;
  CheckboxStateStyle disabled;
  CheckboxStateStyle checkedDisabled;
  CheckboxStateStyle indeterminateDisabled;
  CheckboxMetrics metrics;
};

struct CheckboxStyleInput {
  bool checked = false;
  bool indeterminate = false;
  bool hovered = false;
  bool pressed = false;
  bool focused = false;
  QFont baseFont;
  AdCheckbox::ComponentTokens componentTokens;
};

CheckboxVisualStyle resolveCheckboxVisualStyle(const CheckboxStyleInput& input,
                                               const adqt::theme::ResolvedTheme& resolvedTheme);
CheckboxVisualStyle resolveCheckboxVisualStyle(const CheckboxStyleInput& input);

}  // namespace adqt::widgets::detail
