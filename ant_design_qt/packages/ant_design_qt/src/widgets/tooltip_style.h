#pragma once

#include "tooltip.h"

#include <QColor>
#include <QFont>
#include <QMargins>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct TooltipMetrics {
  int popupMaximumWidth = 250;
  int popupMinimumHeight = 32;
  int borderRadius = 8;
  int borderWidth = 0;
  int arrowSize = 8;
  int popupOffset = 8;
  QMargins padding = QMargins(8, 4, 8, 4);
  QFont textFont;
};

struct TooltipVisualStyle {
  QColor surfaceBackground;
  QColor surfaceBorderColor;
  QColor contentColor;
  QColor arrowBackground;
  QColor arrowBorderColor;
  TooltipMetrics metrics;
};

struct TooltipStyleInput {
  AdTooltip::Placement placement = AdTooltip::Placement::Top;
  bool visible = false;
  bool disabled = false;
  bool arrowVisible = true;
  QFont baseFont;
  AdTooltip::ComponentTokens componentTokens;
  AdTooltip::SemanticStyles semanticStyles;
};

TooltipVisualStyle resolveTooltipVisualStyle(const TooltipStyleInput& input,
                                             const adqt::theme::ResolvedTheme& resolvedTheme);
TooltipVisualStyle resolveTooltipVisualStyle(const TooltipStyleInput& input);

}  // namespace adqt::widgets::detail
