#pragma once

#include "popover.h"

#include <QColor>
#include <QFont>
#include <QMargins>
#include <QWidget>

#include <optional>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct PopoverMetrics {
  int titleMinimumWidth = 0;
  int maximumWidth = QWIDGETSIZE_MAX;
  int zIndex = 1030;
  int cornerRadius = 8;
  int borderWidth = 1;
  int arrowSize = 8;
  int popupOffset = 8;
  int sectionSpacing = 8;
  QMargins contentMargins = QMargins(12, 12, 12, 12);
  QFont titleFont;
  QFont textFont;
};

struct PopoverVisualStyle {
  QColor backgroundColor;
  QColor borderColor;
  QColor titleColor;
  QColor textColor;
  PopoverMetrics metrics;
};

struct PopoverStyleOverrides {
  std::optional<QFont> titleFont;
  std::optional<QFont> textFont;
  std::optional<QColor> backgroundColor;
  std::optional<QColor> borderColor;
  std::optional<QColor> titleColor;
  std::optional<QColor> textColor;
  std::optional<QMargins> contentMargins;
  std::optional<int> titleMinimumWidth;
  std::optional<int> maximumWidth;
  std::optional<int> zIndex;
  std::optional<int> cornerRadius;
  std::optional<int> borderWidth;
  std::optional<int> arrowSize;
  std::optional<int> popupOffset;
};

struct PopoverStyleInput {
  AdPopover::Placement placement = AdPopover::Placement::Top;
  bool visible = false;
  bool disabled = false;
  bool arrowVisible = true;
  QFont baseFont;
  PopoverStyleOverrides overrides;
};

PopoverVisualStyle resolvePopoverVisualStyle(const PopoverStyleInput& input,
                                             const adqt::theme::ResolvedTheme& resolvedTheme);
PopoverVisualStyle resolvePopoverVisualStyle(const PopoverStyleInput& input);

}  // namespace adqt::widgets::detail
