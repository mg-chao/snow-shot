#pragma once

#include <QColor>
#include <QFont>

#include "color_picker.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct ColorPickerMetrics {
  int controlHeightXS = 16;
  int controlHeightSM = 24;
  int controlHeight = 32;
  int controlHeightLG = 40;
  int triggerMinWidth = 32;
  int triggerRadiusSM = 4;
  int triggerRadius = 6;
  int triggerRadiusLG = 8;
  int triggerPadding = 3;
  int triggerTextGap = 8;
  int triggerTextMarginEnd = 5;
  int triggerTextFontSizeLG = 16;
  int triggerTextLineHeightSM = 16;
  int swatchSizeSM = 16;
  int swatchSize = 24;
  int swatchSizeLG = 32;
  int swatchRadiusSM = 2;
  int swatchRadius = 4;
  int swatchRadiusLG = 6;
  int panelWidth = 234;
  int panelPadding = 8;
  int panelSpacing = 8;
  int presetSwatchSize = 24;
  int inputHeight = 26;
  int saturationPanelHeight = 160;
  int saturationPanelRadius = 4;
  int sliderHeight = 8;
  int marginXS = 8;
  int marginSM = 12;
  int sliderControlSize = 8;
  int sliderHandleSize = 8;
  int sliderHandleSizeHover = 8;
  int sliderHandleLineWidth = 2;
  int sliderHandleLineWidthHover = 2;
  int sliderMarginMain = 6;
  int sliderMarginCross = 2;
  int sliderVisualHeight = 12;
  int gradientHandleSize = 6;
  int gradientHandleSizeHover = 8;
  int previewSwatchSize = 20;
  int previewSwatchRadius = 4;
  int alphaInputWidth = 44;
  qreal borderWidth = 1.0;
  qreal focusOutlineWidth = 3.0;
  qreal focusOutlineOffset = 0.0;
  QFont font;
};

struct ColorPickerVisualStyle {
  QColor triggerBackground;
  QColor triggerBackgroundDisabled;
  QColor triggerBorder;
  QColor triggerBorderHover;
  QColor triggerBorderActive;
  QColor triggerFocusOutline;
  QColor triggerText;
  QColor triggerTextDisabled;
  QColor panelBackground;
  QColor panelBorder;
  QColor panelText;
  QColor swatchBorder;
  QColor presetBorder;
  QColor presetBorderHover;
  QColor presetText;
  QColor presetArrow;
  QColor presetEmptyText;
  QColor segmentedBackground;
  QColor segmentedItemBackground;
  QColor segmentedItemHoverBackground;
  QColor segmentedText;
  QColor segmentedTextDisabled;
  QColor segmentedTextChecked;
  QColor clearButtonSlash;
  QColor channelHandleBorder;
  QColor invalidSwatchFill;
  QColor transparentCellA;
  QColor transparentCellB;
  ColorPickerMetrics metrics;
};

struct ColorPickerStyleInput {
  AdColorPicker::Size size = AdColorPicker::Size::Middle;
  bool open = false;
  bool disabled = false;
  bool showText = false;
  bool cleared = false;
  QFont baseFont;
  AdColorPicker::ComponentTokens componentTokens;
};

ColorPickerVisualStyle resolveColorPickerVisualStyle(
    const ColorPickerStyleInput& input, const adqt::theme::ResolvedTheme& resolvedTheme);
ColorPickerVisualStyle resolveColorPickerVisualStyle(const ColorPickerStyleInput& input);

}  // namespace adqt::widgets::detail
