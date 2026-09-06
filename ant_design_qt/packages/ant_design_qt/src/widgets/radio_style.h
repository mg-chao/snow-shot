#pragma once

#include "radio.h"

#include <QColor>
#include <QFont>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct RadioDotStateStyle {
  QColor borderColor;
  QColor backgroundColor;
  QColor dotColor;
  QColor labelColor;
};

struct RadioButtonStateStyle {
  QColor textColor;
  QColor backgroundColor;
  QColor borderColor;
};

struct RadioMetrics {
  int radioSize = 16;
  int dotSize = 8;
  int borderWidth = 1;
  int labelPaddingInlineStart = 8;
  int labelPaddingInlineEnd = 8;
  int textLineHeight = 22;
  int wrapperMarginInlineEnd = 8;
  int buttonHeight = 32;
  int buttonPaddingInline = 12;
  int buttonBorderRadius = 6;
  QFont font;
  QColor focusOutlineColor;
  qreal focusOutlineWidth = 3.0;
  qreal focusOutlineOffset = 1.0;
  QColor waveColor;
};

struct RadioVisualStyle {
  RadioDotStateStyle normal;
  RadioDotStateStyle hover;
  RadioDotStateStyle active;
  RadioDotStateStyle checked;
  RadioDotStateStyle checkedHover;
  RadioDotStateStyle disabled;
  RadioDotStateStyle checkedDisabled;
  RadioMetrics metrics;
};

struct RadioButtonVisualStyle {
  RadioButtonStateStyle normal;
  RadioButtonStateStyle hover;
  RadioButtonStateStyle active;
  RadioButtonStateStyle checked;
  RadioButtonStateStyle checkedHover;
  RadioButtonStateStyle checkedActive;
  RadioButtonStateStyle disabled;
  RadioButtonStateStyle checkedDisabled;
  RadioMetrics metrics;
};

struct RadioStyleInput {
  AdRadio::ControlSize controlSize = AdRadio::ControlSize::Medium;
  bool checked = false;
  bool hovered = false;
  bool pressed = false;
  bool focused = false;
  QFont baseFont;
  AdRadio::ComponentTokens componentTokens;
};

struct RadioButtonStyleInput {
  AdRadio::ControlSize controlSize = AdRadio::ControlSize::Medium;
  AdRadio::ButtonStyle buttonStyle = AdRadio::ButtonStyle::Outline;
  bool checked = false;
  bool hovered = false;
  bool pressed = false;
  bool focused = false;
  QFont baseFont;
  AdRadio::ComponentTokens componentTokens;
};

RadioVisualStyle resolveRadioVisualStyle(const RadioStyleInput& input,
                                         const adqt::theme::ResolvedTheme& resolvedTheme);
RadioVisualStyle resolveRadioVisualStyle(const RadioStyleInput& input);
RadioButtonVisualStyle resolveRadioButtonVisualStyle(
    const RadioButtonStyleInput& input, const adqt::theme::ResolvedTheme& resolvedTheme);
RadioButtonVisualStyle resolveRadioButtonVisualStyle(const RadioButtonStyleInput& input);

}  // namespace adqt::widgets::detail
