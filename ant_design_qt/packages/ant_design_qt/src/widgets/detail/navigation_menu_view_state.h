#pragma once

class QStackedLayout;

namespace adqt::widgets::detail {

class AdMenuBarProxyModel;
class AdMenuBarView;
class AdMenuTreeView;

struct NavigationMenuViewState {
  QStackedLayout* rootLayout = nullptr;
  AdMenuTreeView* inlineView = nullptr;
  AdMenuTreeView* verticalView = nullptr;
  AdMenuBarProxyModel* barProxyModel = nullptr;
  AdMenuBarView* barView = nullptr;
};

}  // namespace adqt::widgets::detail
