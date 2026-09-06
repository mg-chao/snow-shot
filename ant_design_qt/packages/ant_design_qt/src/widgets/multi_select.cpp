#include "multi_select.h"

#include "select.h"

namespace adqt::widgets {

AdMultiSelect::AdMultiSelect(QWidget* parent) : AdAbstractSelectWidget(parent) {
  setInternalMode(Mode::Multiple);

  connect(internalSelect(), &AdSelect::currentValuesChanged, this,
          &AdMultiSelect::selectedValuesChanged);
  connect(internalSelect(), &AdSelect::currentItemsChanged, this,
          &AdMultiSelect::selectedItemsChanged);
  connect(internalSelect(), &AdSelect::selectionChanged, this, &AdMultiSelect::selectionChanged);
  connect(internalSelect(), &AdSelect::maxCountChanged, this,
          &AdMultiSelect::maxSelectionCountChanged);
  connect(internalSelect(), &AdSelect::maxTagCountChanged, this,
          &AdMultiSelect::maxVisibleTagsChanged);
  connect(internalSelect(), &AdSelect::responsiveMaxTagCountChanged, this,
          &AdMultiSelect::responsiveMaxTagCountChanged);
}

AdMultiSelect::~AdMultiSelect() = default;

QVariantList AdMultiSelect::selectedValues() const { return internalSelect()->currentValues(); }

void AdMultiSelect::setSelectedValues(const QVariantList& values) {
  internalSelect()->setCurrentValues(values);
}

QVector<AdMultiSelect::SelectionItem> AdMultiSelect::selectedItems() const {
  return internalSelect()->currentItems();
}

QModelIndexList AdMultiSelect::selectedIndexes() const {
  return internalSelect()->selectedModelIndexes();
}

int AdMultiSelect::maxSelectionCount() const { return internalSelect()->maxCount(); }

void AdMultiSelect::setMaxSelectionCount(int value) { internalSelect()->setMaxCount(value); }

int AdMultiSelect::maxVisibleTags() const { return internalSelect()->maxTagCount(); }

void AdMultiSelect::setMaxVisibleTags(int value) { internalSelect()->setMaxTagCount(value); }

bool AdMultiSelect::responsiveMaxTagCount() const {
  return internalSelect()->responsiveMaxTagCount();
}

void AdMultiSelect::setResponsiveMaxTagCount(bool value) {
  internalSelect()->setResponsiveMaxTagCount(value);
}

}  // namespace adqt::widgets
