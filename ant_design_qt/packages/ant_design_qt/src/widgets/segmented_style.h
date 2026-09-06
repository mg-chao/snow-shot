#pragma once

#include <QColor>
#include <QFont>

#include "segmented.h"

namespace adqt::widgets::detail {

struct SegmentedMetrics {
  int controlHeight = 32;
  int trackPadding = 2;
  int horizontalPadding = 11;
  int borderRadius = 6;
  int itemBorderRadius = 4;
  int iconSize = 14;
  int iconGap = 4;
  int focusOutlineWidth = 2;
  int focusOutlineOffset = 1;
  int thumbShadowOffsetY = 1;
  int animationDurationMs = 300;
  QFont font;
};

struct SegmentedAppearance {
  QColor itemColor;
  QColor itemHoverColor;
  QColor itemHoverBackground;
  QColor itemActiveBackground;
  QColor itemSelectedBackground;
  QColor itemSelectedColor;
  QColor itemDisabledColor;
  QColor trackBackground;
  QColor focusOutline;
  QColor thumbShadow;
  SegmentedMetrics metrics;
};

SegmentedAppearance resolveSegmentedAppearance(const AdSegmented* segmented,
                                               const AdSegmented::ComponentTokens& tokens);

}  // namespace adqt::widgets::detail
