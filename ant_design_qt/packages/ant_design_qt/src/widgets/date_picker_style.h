#pragma once

#include <QColor>
#include <QFont>

#include "date_picker.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct DatePickerMetrics {
  int controlHeight = 32;
  int popupOffset = 4;
  int popupPadding = 0;
  int panelWidth = 288;
  int presetsWidth = 120;
  int presetsMaxWidth = 200;
  int zIndexPopup = 1050;
  int panelPaddingHorizontal = 18;
  int weekPanelPaddingHorizontal = 12;
  int panelPaddingVertical = 8;
  int headerHeight = 40;
  int footerHeight = 38;
  int footerLineHeight = 38;
  int cellHeight = 24;
  int cellPaddingVertical = 6;
  int dateCellWidth = 36;
  int yearMonthCellWidth = 60;
  int textHeight = 40;
  int monthCellHeight = 66;
  int quarterPanelContentHeight = 56;
  int unitPanelPaddingHorizontal = 8;
  int timeColumnWidth = 56;
  int timeColumnHeight = 224;
  int timeColumnMarginVertical = 4;
  int timePanelPaddingTop = 4;
  int timeCellHeight = 28;
  int borderRadius = 8;
  int cellRadius = 4;
  int borderWidth = 1;
  int focusOutlineWidth = 3;
  QFont font;
  QFont smallFont;
  QFont headerFont;
};

struct DatePickerVisualStyle {
  QColor panelBackground;
  QColor panelBorderColor;
  QColor headerBackground;
  QColor bodyBackground;
  QColor contentBackground;
  QColor itemBackground;
  QColor itemBorderColor;
  QColor footerBackground;
  QColor headerTextColor;
  QColor textColor;
  QColor secondaryTextColor;
  QColor disabledTextColor;
  QColor disabledCellBackground;
  QColor hoverBackground;
  QColor rangeBackground;
  QColor rangeHoverBackground;
  QColor rangeBorderColor;
  QColor selectedBackground;
  QColor selectedTextColor;
  QColor todayBorderColor;
  QColor footerBorderColor;
  QColor linkColor;
  DatePickerMetrics metrics;
};

struct DatePickerStyleInput {
  AdDatePicker::Size size = AdDatePicker::Size::Middle;
  AdDatePicker::Variant variant = AdDatePicker::Variant::Outlined;
  AdDatePicker::Status status = AdDatePicker::Status::None;
  bool disabled = false;
  QFont baseFont;
  AdDatePickerPanel::ComponentTokens componentTokens;
  AdDatePickerPanel::SemanticStyles semanticStyles;
};

DatePickerVisualStyle resolveDatePickerVisualStyle(const DatePickerStyleInput& input,
                                                   const adqt::theme::ResolvedTheme& resolvedTheme);
DatePickerVisualStyle resolveDatePickerVisualStyle(const DatePickerStyleInput& input);

}  // namespace adqt::widgets::detail
