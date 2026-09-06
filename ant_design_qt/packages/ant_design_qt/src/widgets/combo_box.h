#pragma once

#include "abstract_select_widget.h"

#include <QModelIndex>
#include <QVariant>

namespace adqt::widgets {

class AdComboBox final : public AdAbstractSelectWidget {
  Q_OBJECT

  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
  Q_PROPERTY(QString currentText READ currentText NOTIFY currentTextChanged)
  Q_PROPERTY(
      QVariant currentValue READ currentValue WRITE setCurrentValue NOTIFY currentValueChanged)
  Q_PROPERTY(bool searchable READ searchable WRITE setSearchable NOTIFY searchableChanged)
  Q_PROPERTY(bool editable READ editable WRITE setEditable NOTIFY editableChanged)

 public:
  using AdAbstractSelectWidget::ColorTokens;
  using AdAbstractSelectWidget::ComponentTokens;
  using AdAbstractSelectWidget::ControlSize;
  using AdAbstractSelectWidget::Item;
  using AdAbstractSelectWidget::ItemDataRole;
  using AdAbstractSelectWidget::MetricTokens;
  using AdAbstractSelectWidget::Option;
  using AdAbstractSelectWidget::Placement;
  using AdAbstractSelectWidget::PopupLayerMode;
  using AdAbstractSelectWidget::PopupWidthMode;
  using AdAbstractSelectWidget::RoleConfig;
  using AdAbstractSelectWidget::SearchPolicy;
  using AdAbstractSelectWidget::SelectionItem;
  using AdAbstractSelectWidget::SemanticSlotStyle;
  using AdAbstractSelectWidget::SemanticStyleResolver;
  using AdAbstractSelectWidget::SemanticStyles;
  using AdAbstractSelectWidget::Status;
  using AdAbstractSelectWidget::StyleContext;
  using AdAbstractSelectWidget::Variant;

  explicit AdComboBox(QWidget* parent = nullptr);
  ~AdComboBox() override;

  int currentIndex() const;
  void setCurrentIndex(int index);

  QString currentText() const;

  QVariant currentValue() const;
  void setCurrentValue(const QVariant& value);

  SelectionItem currentItem() const;
  QModelIndex currentModelIndex() const;

  QVariant currentData(int role = AdAbstractSelectWidget::DefaultValueRole) const;
  void setCurrentData(const QVariant& value, int role = AdAbstractSelectWidget::DefaultValueRole);

  bool searchable() const;
  void setSearchable(bool value);

  bool editable() const;
  void setEditable(bool value);

 signals:
  void currentIndexChanged(int index);
  void currentTextChanged(const QString& text);
  void currentValueChanged(const QVariant& value);
  void currentItemChanged(const adqt::widgets::select::SelectionItem& item);
  void currentModelIndexChanged(const QModelIndex& index);
  void currentDataChanged(const QVariant& value);
  void searchableChanged(bool value);
  void editableChanged(bool value);

 private:
  struct CurrentSnapshot {
    int index = -1;
    QString text;
    QVariant value;
    SelectionItem item;
    QModelIndex modelIndex;
    QVariant data;
  };

  CurrentSnapshot currentSnapshot() const;
  void emitCurrentSignals();

  CurrentSnapshot lastSnapshot_;
  bool hasSnapshot_ = false;
};

}  // namespace adqt::widgets
