#include "combo_box.h"

#include "select.h"

namespace adqt::widgets {

AdComboBox::AdComboBox(QWidget* parent) : AdAbstractSelectWidget(parent) {
  setInternalMode(Mode::Single);

  connect(internalSelect(), &AdSelect::searchEnabledChanged, this, &AdComboBox::searchableChanged);
  connect(internalSelect(), &AdSelect::searchEnabledChanged, this, &AdComboBox::editableChanged);

  const auto forwardCurrentSignals = [this]() { emitCurrentSignals(); };
  connect(internalSelect(), &AdSelect::currentValueChanged, this, forwardCurrentSignals);
  connect(internalSelect(), &AdSelect::currentModelIndexChanged, this, forwardCurrentSignals);
  connect(internalSelect(), &AdSelect::currentItemsChanged, this,
          [this](const QVector<AdSelect::SelectionItem>&) { emitCurrentSignals(); });

  lastSnapshot_ = currentSnapshot();
  hasSnapshot_ = true;
}

AdComboBox::~AdComboBox() = default;

int AdComboBox::currentIndex() const { return internalSelect()->currentIndex(); }

void AdComboBox::setCurrentIndex(int index) { internalSelect()->setCurrentIndex(index); }

QString AdComboBox::currentText() const { return internalSelect()->currentText(); }

QVariant AdComboBox::currentValue() const { return internalSelect()->currentValue(); }

void AdComboBox::setCurrentValue(const QVariant& value) {
  internalSelect()->setCurrentValue(value);
}

AdComboBox::SelectionItem AdComboBox::currentItem() const {
  const QVector<SelectionItem> items = internalSelect()->currentItems();
  return items.isEmpty() ? SelectionItem() : items.constFirst();
}

QModelIndex AdComboBox::currentModelIndex() const { return internalSelect()->currentModelIndex(); }

QVariant AdComboBox::currentData(int role) const { return internalSelect()->currentData(role); }

void AdComboBox::setCurrentData(const QVariant& value, int role) {
  internalSelect()->setCurrentData(value, role);
}

bool AdComboBox::searchable() const { return searchEnabled(); }

void AdComboBox::setSearchable(bool value) { setSearchEnabled(value); }

bool AdComboBox::editable() const { return searchable(); }

void AdComboBox::setEditable(bool value) { setSearchable(value); }

AdComboBox::CurrentSnapshot AdComboBox::currentSnapshot() const {
  CurrentSnapshot snapshot;
  snapshot.index = currentIndex();
  snapshot.text = currentText();
  snapshot.value = currentValue();
  snapshot.item = currentItem();
  snapshot.modelIndex = currentModelIndex();
  snapshot.data = currentData();
  return snapshot;
}

void AdComboBox::emitCurrentSignals() {
  const CurrentSnapshot snapshot = currentSnapshot();
  const CurrentSnapshot previous = hasSnapshot_ ? lastSnapshot_ : CurrentSnapshot();
  hasSnapshot_ = true;
  lastSnapshot_ = snapshot;

  if (previous.value != snapshot.value) {
    emit currentValueChanged(snapshot.value);
  }
  if (previous.index != snapshot.index) {
    emit currentIndexChanged(snapshot.index);
  }
  if (previous.text != snapshot.text) {
    emit currentTextChanged(snapshot.text);
  }
  if (previous.modelIndex != snapshot.modelIndex) {
    emit currentModelIndexChanged(snapshot.modelIndex);
  }
  if (previous.item.value != snapshot.item.value || previous.item.label != snapshot.item.label) {
    emit currentItemChanged(snapshot.item);
  }
  if (previous.data != snapshot.data) {
    emit currentDataChanged(snapshot.data);
  }
}

}  // namespace adqt::widgets
