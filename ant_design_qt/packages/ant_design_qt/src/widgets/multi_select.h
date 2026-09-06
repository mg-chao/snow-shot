#pragma once

#include "abstract_select_widget.h"

#include <QModelIndexList>
#include <QVariantList>

namespace adqt::widgets {

class AdMultiSelect final : public AdAbstractSelectWidget {
  Q_OBJECT

  Q_PROPERTY(QVariantList selectedValues READ selectedValues WRITE setSelectedValues NOTIFY
                 selectedValuesChanged)
  Q_PROPERTY(int maxSelectionCount READ maxSelectionCount WRITE setMaxSelectionCount NOTIFY
                 maxSelectionCountChanged)
  Q_PROPERTY(
      int maxVisibleTags READ maxVisibleTags WRITE setMaxVisibleTags NOTIFY maxVisibleTagsChanged)
  Q_PROPERTY(bool responsiveMaxTagCount READ responsiveMaxTagCount WRITE setResponsiveMaxTagCount
                 NOTIFY responsiveMaxTagCountChanged)

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

  explicit AdMultiSelect(QWidget* parent = nullptr);
  ~AdMultiSelect() override;

  QVariantList selectedValues() const;
  void setSelectedValues(const QVariantList& values);

  QVector<SelectionItem> selectedItems() const;
  QModelIndexList selectedIndexes() const;

  int maxSelectionCount() const;
  void setMaxSelectionCount(int value);

  int maxVisibleTags() const;
  void setMaxVisibleTags(int value);

  bool responsiveMaxTagCount() const;
  void setResponsiveMaxTagCount(bool value);

 signals:
  void selectedValuesChanged(const QVariantList& values);
  void selectedItemsChanged(const QVector<adqt::widgets::select::SelectionItem>& items);
  void selectionChanged(const QVector<adqt::widgets::select::SelectionItem>& items);
  void maxSelectionCountChanged(int value);
  void maxVisibleTagsChanged(int value);
  void responsiveMaxTagCountChanged(bool value);
};

}  // namespace adqt::widgets
