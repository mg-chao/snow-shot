#pragma once

#include "../navigation_menu.h"

#include <QPersistentModelIndex>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <vector>

class QWidget;

namespace adqt::widgets::detail {

class AdMenuPopupShell;
class AdMenuTreeView;

struct NavigationMenuPopupLevel {
  QPointer<AdMenuPopupShell> shell;
  QPointer<AdMenuTreeView> view;
  QPointer<QWidget> renderedRoot;
  QPersistentModelIndex submenuIndex;
  QPersistentModelIndex renderedForIndex;
  bool renderedWithCustomPopup = false;
  AdNavigationMenu::ColorScheme popupColorScheme = AdNavigationMenu::ColorScheme::Inherit;
};

struct NavigationMenuPopupState {
  std::vector<std::unique_ptr<NavigationMenuPopupLevel>> levels;
  QTimer hoverOpenTimer;
  QTimer hoverCloseTimer;
};

}  // namespace adqt::widgets::detail
