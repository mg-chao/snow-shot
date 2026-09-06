#pragma once

#include "navigation_menu.h"

#include <QColor>
#include <QFont>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct MenuStateStyle {
  QColor text;
  QColor background;
};

struct MenuMetrics {
  int itemHeight = 40;
  int horizontalLineHeight = 46;
  int itemPaddingInline = 16;
  int itemMarginInline = 4;
  int itemMarginBlock = 4;
  int rootPaddingBlockStart = 0;
  int itemBorderRadius = 8;
  int horizontalItemBorderRadius = 0;
  int subMenuItemBorderRadius = 6;
  int popupBorderRadius = 8;
  int indentation = 24;

  int iconSize = 16;
  int collapsedIconSize = 16;
  int iconMarginInlineEnd = 10;
  int activeBarWidth = 0;
  int activeBarHeight = 2;

  int borderWidth = 1;
  int dividerMarginBlock = 1;
  int groupTitleFontSize = 14;
  int groupTitleLineHeight = 22;
  int groupTitleHorizontalPadding = 16;
  int groupTitleVerticalPadding = 8;
  int popupPlacementGap = 8;
  int horizontalSpacing = 0;
  QFont font;
};

struct MenuVisualStyle {
  QColor menuBackground;
  QColor borderColor;
  QColor dividerColor;
  QColor groupTitleColor;
  QColor subMenuItemSelectedColor;
  QColor subMenuBackground;
  QColor popupBackground;
  QColor popupBorderColor;

  MenuStateStyle normal;
  MenuStateStyle hover;
  MenuStateStyle active;
  MenuStateStyle selected;
  MenuStateStyle disabled;
  MenuStateStyle danger;
  MenuStateStyle dangerHover;
  MenuStateStyle dangerActive;
  MenuStateStyle dangerSelected;

  MenuStateStyle horizontalNormal;
  MenuStateStyle horizontalHover;
  MenuStateStyle horizontalActive;
  MenuStateStyle horizontalSelected;

  MenuMetrics metrics;
};

struct MenuStyleInput {
  AdNavigationMenu::Mode mode = AdNavigationMenu::Mode::Vertical;
  AdNavigationMenu::ColorScheme colorScheme = AdNavigationMenu::ColorScheme::Light;
  bool collapsed = false;
  QFont baseFont;
  AdNavigationMenu::ComponentTokens componentTokens;
  AdNavigationMenu::SemanticStyles semanticStyles;
};

MenuVisualStyle resolveMenuVisualStyle(const MenuStyleInput& input,
                                       const adqt::theme::ResolvedTheme& resolvedTheme);
MenuVisualStyle resolveMenuVisualStyle(const MenuStyleInput& input);

}  // namespace adqt::widgets::detail
