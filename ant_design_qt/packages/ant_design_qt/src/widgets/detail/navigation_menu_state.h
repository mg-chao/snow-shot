#pragma once

#include "../navigation_menu.h"

#include <QHash>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include <functional>

class QAbstractItemModel;
class QItemSelectionModel;

namespace adqt::widgets::detail {

QString navigationMenuStableIdForIndex(const QModelIndex& sourceIndex);
QStringList navigationMenuUniqueStringList(const QStringList& values);

class NavigationMenuState final {
 public:
  QPointer<QAbstractItemModel> model;
  QPointer<QItemSelectionModel> selectionModel;
  bool ownsSelectionModel = false;
  bool focusVisible = false;

  QStringList inlineExpandedCacheStableIds;
  QHash<QString, QPersistentModelIndex> stableIdIndexCache;
  QSet<QString> duplicateStableIds;
  QStringList expandedStableIds;
  QStringList collapsedPopupStableIds;

  QPersistentModelIndex pendingHoverIndex;
  QPersistentModelIndex hoveredIndex;

  void rebuildStableIdIndexCache();
  QModelIndex indexForStableId(const QString& key) const;
  QString stableIdForIndexNormalized(const QModelIndex& index) const;

  QStringList filterKnownStableIds(const QStringList& keys,
                                   const std::function<bool(const QModelIndex&)>& isAccepted,
                                   const char* context) const;

  void normalizeExpandedStableIds(QStringList& keys,
                                  const std::function<bool(const QModelIndex&)>& isSubmenu) const;

  QStringList& visibleExpandedStableIds(AdNavigationMenu::Mode mode, bool collapsed);
  const QStringList& visibleExpandedStableIds(AdNavigationMenu::Mode mode, bool collapsed) const;
  bool containsExpanded(const QModelIndex& index, AdNavigationMenu::Mode mode,
                        bool collapsed) const;
};

}  // namespace adqt::widgets::detail
