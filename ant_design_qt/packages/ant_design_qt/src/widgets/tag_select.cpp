#include "tag_select.h"

#include "select.h"

namespace adqt::widgets {

AdTagSelect::AdTagSelect(QWidget* parent) : AdAbstractSelectWidget(parent) {
  setInternalMode(Mode::Tags);

  connect(internalSelect(), &AdSelect::currentValuesChanged, this,
          &AdTagSelect::selectedValuesChanged);
  connect(internalSelect(), &AdSelect::currentItemsChanged, this,
          &AdTagSelect::selectedItemsChanged);
  connect(internalSelect(), &AdSelect::selectionChanged, this, &AdTagSelect::selectionChanged);
  connect(internalSelect(), &AdSelect::maxCountChanged, this,
          &AdTagSelect::maxSelectionCountChanged);
  connect(internalSelect(), &AdSelect::maxTagCountChanged, this,
          &AdTagSelect::maxVisibleTagsChanged);
  connect(internalSelect(), &AdSelect::responsiveMaxTagCountChanged, this,
          &AdTagSelect::responsiveMaxTagCountChanged);
  connect(internalSelect(), &AdSelect::autoClearSearchValueChanged, this,
          &AdTagSelect::autoClearSearchValueChanged);
}

AdTagSelect::~AdTagSelect() = default;

QVariantList AdTagSelect::selectedValues() const { return internalSelect()->currentValues(); }

void AdTagSelect::setSelectedValues(const QVariantList& values) {
  internalSelect()->setCurrentValues(values);
}

QVector<AdTagSelect::SelectionItem> AdTagSelect::selectedItems() const {
  return internalSelect()->currentItems();
}

QModelIndexList AdTagSelect::selectedIndexes() const {
  return internalSelect()->selectedModelIndexes();
}

int AdTagSelect::maxSelectionCount() const { return internalSelect()->maxCount(); }

void AdTagSelect::setMaxSelectionCount(int value) { internalSelect()->setMaxCount(value); }

int AdTagSelect::maxVisibleTags() const { return internalSelect()->maxTagCount(); }

void AdTagSelect::setMaxVisibleTags(int value) { internalSelect()->setMaxTagCount(value); }

bool AdTagSelect::responsiveMaxTagCount() const {
  return internalSelect()->responsiveMaxTagCount();
}

void AdTagSelect::setResponsiveMaxTagCount(bool value) {
  internalSelect()->setResponsiveMaxTagCount(value);
}

QStringList AdTagSelect::tokenSeparators() const { return internalSelect()->tokenSeparators(); }

void AdTagSelect::setTokenSeparators(const QStringList& separators) {
  if (internalSelect()->tokenSeparators() == separators) {
    return;
  }
  internalSelect()->setTokenSeparators(separators);
  emit tokenSeparatorsChanged(internalSelect()->tokenSeparators());
}

bool AdTagSelect::autoClearSearchValue() const { return internalSelect()->autoClearSearchValue(); }

void AdTagSelect::setAutoClearSearchValue(bool value) {
  internalSelect()->setAutoClearSearchValue(value);
}

}  // namespace adqt::widgets
