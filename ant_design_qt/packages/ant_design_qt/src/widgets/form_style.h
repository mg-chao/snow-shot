#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

#include "form.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct FormMetrics {
  int labelHeight = 32;
  int controlMinHeight = 32;
  int itemMarginBottom = 24;
  int inlineItemMarginBottom = 0;
  int inlineItemGap = 16;
  int verticalLabelPaddingBottom = 8;
  int requiredMarkGap = 4;
  int optionalMarkGap = 4;
  int colonMarginInlineStart = 2;
  int colonMarginInlineEnd = 8;
  int feedbackIconSize = 14;
  int feedbackIconGap = 8;
  int messageMinHeight = 24;
  int messageLineHeight = 22;
  QFont labelFont;
  QFont messageFont;
};

struct FormVisualStyle {
  QColor labelColor;
  QColor requiredMarkColor;
  QColor optionalColor;
  QColor messageColor;
  QColor errorColor;
  QColor warningColor;
  QColor successColor;
  QColor validatingColor;
  QColor disabledColor;
  FormMetrics metrics;
};

struct FormStyleInput {
  AdForm::ControlSize controlSize = AdForm::ControlSize::Medium;
  bool enabled = true;
  bool hasPaletteOverride = false;
  QPalette::ColorGroup paletteGroup = QPalette::Active;
  QFont baseFont;
  QPalette palette;
};

FormVisualStyle resolveFormVisualStyle(const FormStyleInput& input,
                                       const adqt::theme::ResolvedTheme& resolvedTheme);
FormVisualStyle resolveFormVisualStyle(const FormStyleInput& input);

}  // namespace adqt::widgets::detail
