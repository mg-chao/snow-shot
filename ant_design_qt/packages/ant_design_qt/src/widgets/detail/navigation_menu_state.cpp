#include "navigation_menu_state.h"

#include <QAbstractItemModel>
#include <QDebug>

namespace adqt::widgets::detail {

QStringList navigationMenuUniqueStringList(const QStringList& values) {
  QStringList out;
  out.reserve(values.size());
  QSet<QString> seen;
  for (const QString& value : values) {
    const QString normalized = value.trimmed();
    if (normalized.isEmpty() || seen.contains(normalized)) {
      continue;
    }
    seen.insert(normalized);
    out.append(normalized);
  }
  return out;
}

QString navigationMenuStableIdForIndex(const QModelIndex& sourceIndex) {
  if (!sourceIndex.isValid()) {
    return QString();
  }
  QString explicitId = sourceIndex.data(AdNavigationMenu::StableIdRole).toString().trimmed();
  if (!explicitId.isEmpty()) {
    return explicitId;
  }
  QStringList path;
  QModelIndex current = sourceIndex;
  while (current.isValid()) {
    path.prepend(QString::number(current.row()));
    current = current.parent();
  }
  return path.join(QLatin1Char('/'));
}

void NavigationMenuState::rebuildStableIdIndexCache() {
  stableIdIndexCache.clear();
  duplicateStableIds.clear();
  if (!model) {
    return;
  }

  std::function<void(const QModelIndex&)> walk;
  walk = [this, &walk](const QModelIndex& parent) {
    if (!model) {
      return;
    }
    const int rowCount = model->rowCount(parent);
    for (int row = 0; row < rowCount; ++row) {
      const QModelIndex index = model->index(row, 0, parent);
      if (!index.isValid()) {
        continue;
      }
      const QString stableId = navigationMenuStableIdForIndex(index).trimmed();
      if (!stableId.isEmpty()) {
        if (duplicateStableIds.contains(stableId)) {
          // Leave duplicate stable ids unresolved so state restoration never targets an ambiguous
          // row.
        } else if (stableIdIndexCache.contains(stableId)) {
          stableIdIndexCache.remove(stableId);
          duplicateStableIds.insert(stableId);
        } else {
          stableIdIndexCache.insert(stableId, index);
        }
      }
      walk(index);
    }
  };

  walk(QModelIndex());

  if (!duplicateStableIds.isEmpty()) {
    const QStringList duplicates = duplicateStableIds.values();
    qWarning().noquote() << "AdNavigationMenu: duplicate stable ids detected:"
                         << duplicates.join(", ");
  }
}

QModelIndex NavigationMenuState::indexForStableId(const QString& key) const {
  const QString normalized = key.trimmed();
  if (normalized.isEmpty() || duplicateStableIds.contains(normalized)) {
    return QModelIndex();
  }
  const auto it = stableIdIndexCache.constFind(normalized);
  if (it == stableIdIndexCache.constEnd() || !it.value().isValid()) {
    return QModelIndex();
  }
  return it.value();
}

QString NavigationMenuState::stableIdForIndexNormalized(const QModelIndex& index) const {
  return navigationMenuStableIdForIndex(index).trimmed();
}

QStringList NavigationMenuState::filterKnownStableIds(
    const QStringList& keys, const std::function<bool(const QModelIndex&)>& isAccepted,
    const char* context) const {
  QStringList normalized = navigationMenuUniqueStringList(keys);
  if (!model) {
    return normalized;
  }

  QStringList filtered;
  filtered.reserve(normalized.size());
  QStringList unknown;
  for (const QString& key : normalized) {
    const QModelIndex index = indexForStableId(key);
    if (index.isValid() && isAccepted && isAccepted(index)) {
      filtered.append(key);
    } else if (!key.isEmpty()) {
      unknown.append(key);
    }
  }

  if (!unknown.isEmpty() && context) {
    qWarning().noquote() << "AdNavigationMenu:" << context
                         << "ignored unknown or ambiguous stable ids:" << unknown.join(", ");
  }
  return filtered;
}

void NavigationMenuState::normalizeExpandedStableIds(
    QStringList& keys, const std::function<bool(const QModelIndex&)>& isSubmenu) const {
  const QStringList normalized = navigationMenuUniqueStringList(keys);
  keys.clear();
  keys.reserve(normalized.size());
  for (const QString& key : normalized) {
    const QModelIndex index = indexForStableId(key);
    if (index.isValid() && isSubmenu && isSubmenu(index)) {
      keys.append(key);
    }
  }
}

QStringList& NavigationMenuState::visibleExpandedStableIds(AdNavigationMenu::Mode mode,
                                                           bool collapsed) {
  if (mode == AdNavigationMenu::Mode::Inline && collapsed) {
    return collapsedPopupStableIds;
  }
  return expandedStableIds;
}

const QStringList& NavigationMenuState::visibleExpandedStableIds(AdNavigationMenu::Mode mode,
                                                                 bool collapsed) const {
  if (mode == AdNavigationMenu::Mode::Inline && collapsed) {
    return collapsedPopupStableIds;
  }
  return expandedStableIds;
}

bool NavigationMenuState::containsExpanded(const QModelIndex& index, AdNavigationMenu::Mode mode,
                                           bool collapsed) const {
  const QString key = stableIdForIndexNormalized(index);
  return !key.isEmpty() && visibleExpandedStableIds(mode, collapsed).contains(key);
}

}  // namespace adqt::widgets::detail
