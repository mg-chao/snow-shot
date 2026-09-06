#pragma once

#include "select_option_utils.h"

#include <QAbstractListModel>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSortFilterProxyModel>

namespace adqt::widgets::detail {

enum SelectPopupDataRole {
  SelectPopupHeaderRole = Qt::UserRole + 201,
  SelectPopupEmptyRole,
  SelectPopupSelectedRole,
  SelectPopupDisabledRole,
};

struct SelectPopupRow {
  bool header = false;
  bool empty = false;
  AdSelectTypes::Option option;
  QString optionText;
  QString headerText;
  bool selected = false;
};

class SelectCompositeModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  explicit SelectCompositeModel(QObject* parent = nullptr);

  void setPrimaryModel(QAbstractItemModel* model);
  void setOverlayModel(QAbstractItemModel* model);
  void setPrimaryColumn(int column);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex index(int row, int column = 0,
                    const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QMap<int, QVariant> itemData(const QModelIndex& index) const override;

 private:
  QModelIndex mapToUnderlying(const QModelIndex& index) const;
  void reconnectSourceModel(QPointer<QAbstractItemModel>* slot, QAbstractItemModel* model);
  void resetFromSourceChange();

  QPointer<QAbstractItemModel> primaryModel_;
  QPointer<QAbstractItemModel> overlayModel_;
  int primaryColumn_ = 0;
};

class SelectFilterProxyModel final : public QSortFilterProxyModel {
  Q_OBJECT

 public:
  explicit SelectFilterProxyModel(QObject* parent = nullptr);

  void setRoleConfig(const SelectRoleConfig& roles);
  void setSearchEnabled(bool value);
  void setSearchText(const QString& value);
  void setSearchPolicy(adqt::widgets::select::SearchPolicy value);
  void setSearchRoles(const QList<int>& roles);
  void setSearchFilterFields(const QStringList& fields);
  void setFilterPredicate(const AdSelectTypes::FilterPredicate& predicate);
  void setSortComparator(const AdSelectTypes::SortComparator& comparator);

 protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
  bool lessThan(const QModelIndex& sourceLeft, const QModelIndex& sourceRight) const override;

 private:
  void refreshFilter();

  SelectRoleConfig roles_;
  bool searchEnabled_ = false;
  QString searchText_;
  adqt::widgets::select::SearchPolicy searchPolicy_ =
      adqt::widgets::select::SearchPolicy::LocalFilter;
  QList<int> searchRoles_;
  QStringList searchFilterFields_;
  AdSelectTypes::FilterPredicate filterPredicate_;
  AdSelectTypes::SortComparator sortComparator_;
};

class SelectPopupListModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  explicit SelectPopupListModel(QObject* parent = nullptr);

  void setRoleConfig(const SelectRoleConfig& roles);
  void setRows(const QVector<SelectPopupRow>& rows);
  const SelectPopupRow* rowAt(int row) const;

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;

 private:
  SelectRoleConfig roles_;
  QVector<SelectPopupRow> rows_;
};

}  // namespace adqt::widgets::detail
