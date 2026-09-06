#include "select_models.h"

#include <QAbstractItemModel>
#include <QtGlobal>

namespace adqt::widgets::detail {

namespace {

QModelIndex underlyingIndexForRow(QAbstractItemModel* model, int row, int column) {
  return model ? model->index(row, column) : QModelIndex();
}

QString optionFieldValue(const AdSelectTypes::Option& option, const QString& field) {
  if (field == QStringLiteral("label")) {
    const QString label = option.label.trimmed();
    return label.isEmpty() ? option.value.toString().trimmed() : label;
  }
  if (field == QStringLiteral("value")) {
    return option.value.toString().trimmed();
  }
  return option.metadata.value(field).toString();
}

}  // namespace

SelectCompositeModel::SelectCompositeModel(QObject* parent) : QAbstractListModel(parent) {}

void SelectCompositeModel::setPrimaryModel(QAbstractItemModel* model) {
  reconnectSourceModel(&primaryModel_, model);
}

void SelectCompositeModel::setOverlayModel(QAbstractItemModel* model) {
  reconnectSourceModel(&overlayModel_, model);
}

void SelectCompositeModel::setPrimaryColumn(int column) {
  const int normalized = qMax(0, column);
  if (primaryColumn_ == normalized) {
    return;
  }
  primaryColumn_ = normalized;
  resetFromSourceChange();
}

int SelectCompositeModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  const int primaryRows = primaryModel_ ? primaryModel_->rowCount() : 0;
  const int overlayRows = overlayModel_ ? overlayModel_->rowCount() : 0;
  return primaryRows + overlayRows;
}

QModelIndex SelectCompositeModel::index(int row, int column, const QModelIndex& parent) const {
  if (parent.isValid() || column != 0 || row < 0 || row >= rowCount()) {
    return QModelIndex();
  }
  return createIndex(row, column);
}

QVariant SelectCompositeModel::data(const QModelIndex& index, int role) const {
  const QModelIndex underlying = mapToUnderlying(index);
  return underlying.isValid() ? underlying.data(role) : QVariant();
}

Qt::ItemFlags SelectCompositeModel::flags(const QModelIndex& index) const {
  const QModelIndex underlying = mapToUnderlying(index);
  return underlying.isValid() ? underlying.model()->flags(underlying) : Qt::NoItemFlags;
}

QMap<int, QVariant> SelectCompositeModel::itemData(const QModelIndex& index) const {
  const QModelIndex underlying = mapToUnderlying(index);
  if (!underlying.isValid() || !underlying.model()) {
    return {};
  }
  return underlying.model()->itemData(underlying);
}

QModelIndex SelectCompositeModel::mapToUnderlying(const QModelIndex& index) const {
  if (!index.isValid()) {
    return QModelIndex();
  }

  const int row = index.row();
  const int primaryRows = primaryModel_ ? primaryModel_->rowCount() : 0;
  if (row < primaryRows) {
    return underlyingIndexForRow(primaryModel_, row, primaryColumn_);
  }
  return underlyingIndexForRow(overlayModel_, row - primaryRows, 0);
}

void SelectCompositeModel::reconnectSourceModel(QPointer<QAbstractItemModel>* slot,
                                                QAbstractItemModel* model) {
  if (*slot == model) {
    return;
  }
  if (*slot) {
    disconnect(*slot, nullptr, this, nullptr);
  }
  *slot = model;
  if (*slot) {
    connect(*slot, &QObject::destroyed, this, [this, slot]() {
      *slot = nullptr;
      resetFromSourceChange();
    });
    connect(*slot, &QAbstractItemModel::modelReset, this, [this]() { resetFromSourceChange(); });
    connect(*slot, &QAbstractItemModel::layoutChanged, this,
            [this](const QList<QPersistentModelIndex>&, QAbstractItemModel::LayoutChangeHint) {
              resetFromSourceChange();
            });
    connect(*slot, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, int, int) { resetFromSourceChange(); });
    connect(*slot, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, int, int) { resetFromSourceChange(); });
    connect(
        *slot, &QAbstractItemModel::rowsMoved, this,
        [this](const QModelIndex&, int, int, const QModelIndex&, int) { resetFromSourceChange(); });
    connect(*slot,
            qOverload<const QModelIndex&, const QModelIndex&, const QList<int>&>(
                &QAbstractItemModel::dataChanged),
            this, [this](const QModelIndex&, const QModelIndex&, const QList<int>&) {
              resetFromSourceChange();
            });
  }
  resetFromSourceChange();
}

void SelectCompositeModel::resetFromSourceChange() {
  beginResetModel();
  endResetModel();
}

SelectFilterProxyModel::SelectFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  setSortCaseSensitivity(Qt::CaseInsensitive);
}

void SelectFilterProxyModel::setRoleConfig(const SelectRoleConfig& roles) {
  roles_ = roles;
  refreshFilter();
}

void SelectFilterProxyModel::setSearchEnabled(bool value) {
  if (searchEnabled_ == value) {
    return;
  }
  searchEnabled_ = value;
  refreshFilter();
}

void SelectFilterProxyModel::setSearchText(const QString& value) {
  if (searchText_ == value) {
    return;
  }
  searchText_ = value;
  refreshFilter();
}

void SelectFilterProxyModel::setSearchPolicy(adqt::widgets::select::SearchPolicy value) {
  if (searchPolicy_ == value) {
    return;
  }
  searchPolicy_ = value;
  refreshFilter();
}

void SelectFilterProxyModel::setSearchRoles(const QList<int>& roles) {
  if (searchRoles_ == roles) {
    return;
  }
  searchRoles_ = roles;
  refreshFilter();
}

void SelectFilterProxyModel::setSearchFilterFields(const QStringList& fields) {
  if (searchFilterFields_ == fields) {
    return;
  }
  searchFilterFields_ = fields;
  refreshFilter();
}

void SelectFilterProxyModel::setFilterPredicate(const AdSelectTypes::FilterPredicate& predicate) {
  filterPredicate_ = predicate;
  refreshFilter();
}

void SelectFilterProxyModel::setSortComparator(const AdSelectTypes::SortComparator& comparator) {
  sortComparator_ = comparator;
  refreshFilter();
}

bool SelectFilterProxyModel::filterAcceptsRow(int sourceRow,
                                              const QModelIndex& sourceParent) const {
  if (!sourceModel()) {
    return false;
  }

  const QString term = searchText_.trimmed();
  if (!searchEnabled_ || term.isEmpty()) {
    return true;
  }
  if (searchPolicy_ == adqt::widgets::select::SearchPolicy::External) {
    return true;
  }

  const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
  if (!index.isValid()) {
    return false;
  }

  const AdSelectTypes::Option option = materializeSelectOption(index, roles_);
  if (filterPredicate_) {
    return filterPredicate_(term, option);
  }

  if (!searchFilterFields_.isEmpty()) {
    return std::any_of(searchFilterFields_.cbegin(), searchFilterFields_.cend(),
                       [&option, &term](const QString& field) {
                         return optionFieldValue(option, field).contains(term, Qt::CaseInsensitive);
                       });
  }

  const QList<int> roles =
      searchRoles_.isEmpty() ? QList<int>{roles_.labelRole, roles_.valueRole} : searchRoles_;
  return std::any_of(roles.cbegin(), roles.cend(), [this, &index, &term](int role) {
    return sourceModel()->data(index, role).toString().contains(term, Qt::CaseInsensitive);
  });
}

bool SelectFilterProxyModel::lessThan(const QModelIndex& sourceLeft,
                                      const QModelIndex& sourceRight) const {
  if (!sortComparator_) {
    // rc-select preserves the source order unless filterSort is provided.
    if (sourceLeft.row() != sourceRight.row()) {
      return sourceLeft.row() < sourceRight.row();
    }
    return sourceLeft.column() < sourceRight.column();
  }

  const AdSelectTypes::Option lhsOption = materializeSelectOption(sourceLeft, roles_);
  const AdSelectTypes::Option rhsOption = materializeSelectOption(sourceRight, roles_);
  const bool lhsBefore = sortComparator_(lhsOption, rhsOption);
  const bool rhsBefore = sortComparator_(rhsOption, lhsOption);
  if (lhsBefore == rhsBefore) {
    return sourceLeft.row() < sourceRight.row();
  }
  return lhsBefore;
}

void SelectFilterProxyModel::refreshFilter() {
  invalidate();
  sort(0);
}

SelectPopupListModel::SelectPopupListModel(QObject* parent) : QAbstractListModel(parent) {}

void SelectPopupListModel::setRoleConfig(const SelectRoleConfig& roles) { roles_ = roles; }

void SelectPopupListModel::setRows(const QVector<SelectPopupRow>& rows) {
  beginResetModel();
  rows_ = rows;
  endResetModel();
}

const SelectPopupRow* SelectPopupListModel::rowAt(int row) const {
  return (row >= 0 && row < rows_.size()) ? &rows_.at(row) : nullptr;
}

int SelectPopupListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant SelectPopupListModel::data(const QModelIndex& index, int role) const {
  const SelectPopupRow* row = rowAt(index.row());
  if (!row) {
    return QVariant();
  }

  if (role == SelectPopupHeaderRole) {
    return row->header;
  }
  if (role == SelectPopupEmptyRole) {
    return row->empty;
  }
  if (role == SelectPopupSelectedRole) {
    return row->selected;
  }
  if (role == SelectPopupDisabledRole) {
    return row->option.disabled;
  }

  if (row->empty) {
    if (role == Qt::DisplayRole) {
      return row->headerText;
    }
    return QVariant();
  }

  if (row->header) {
    if (role == Qt::DisplayRole) {
      return row->headerText;
    }
    return QVariant();
  }

  if (role == Qt::DisplayRole) {
    return row->optionText;
  }
  if (role == Qt::UserRole) {
    return row->option.value;
  }
  if (role == roles_.valueRole) {
    return row->option.value;
  }
  if (role == roles_.labelRole) {
    return row->option.label;
  }
  if (role == roles_.tagTextRole) {
    const QString tagText = optionTagTextFromMetadata(row->option);
    return tagText.isEmpty() ? row->option.label : tagText;
  }
  if (role == roles_.selectedTextRole) {
    const QString selectedText = optionSelectedTextFromMetadata(row->option);
    return selectedText.isEmpty() ? row->option.label : selectedText;
  }
  if (role == roles_.groupRole) {
    return row->option.group;
  }
  if (role >= Qt::UserRole + 1) {
    const QVariant metadataValue = row->option.metadata.value(syntheticRoleFieldName(role));
    if (metadataValue.isValid()) {
      return metadataValue;
    }
  }

  return QVariant();
}

Qt::ItemFlags SelectPopupListModel::flags(const QModelIndex& index) const {
  const SelectPopupRow* row = rowAt(index.row());
  if (!row) {
    return Qt::NoItemFlags;
  }
  if (row->header || row->empty) {
    return Qt::NoItemFlags;
  }

  Qt::ItemFlags flags = Qt::ItemIsSelectable;
  if (!row->option.disabled) {
    flags |= Qt::ItemIsEnabled;
  }
  return flags;
}

}  // namespace adqt::widgets::detail
