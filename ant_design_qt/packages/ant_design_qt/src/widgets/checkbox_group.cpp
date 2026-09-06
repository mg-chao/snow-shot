#include "checkbox_group.h"

#include <QBoxLayout>
#include <QScopedValueRollback>

#include <algorithm>
#include <utility>

namespace adqt::widgets {

AdCheckboxGroup::AdCheckboxGroup(QObject* parent) : QButtonGroup(parent) {
  setExclusive(false);
  connect(this, &QButtonGroup::buttonToggled, this, [this](QAbstractButton*, bool) {
    if (!syncing_) {
      emitValuesIfChanged();
    }
  });
}

AdCheckboxGroup::~AdCheckboxGroup() {
  const QList<AdCheckbox*> current = checkboxes();
  for (AdCheckbox* checkbox : current) {
    if (checkbox) {
      detachCheckbox(checkbox);
      if (!enabled_ && enabledBeforeGroupDisable_.contains(checkbox)) {
        checkbox->setEnabled(enabledBeforeGroupDisable_.value(checkbox));
      }
      checkbox->setGroup(nullptr);
    }
  }
}

void AdCheckboxGroup::addButton(QAbstractButton* button, int id) {
  AdCheckbox* checkbox = qobject_cast<AdCheckbox*>(button);
  if (!checkbox) {
    return;
  }
  addCheckbox(checkbox, checkbox->value());
  if (id != -1) {
    QButtonGroup::setId(checkbox, id);
  }
}

void AdCheckboxGroup::removeButton(QAbstractButton* button) {
  removeCheckbox(qobject_cast<AdCheckbox*>(button));
}

QVariant AdCheckboxGroup::effectiveValue(AdCheckbox* checkbox, const QVariant& requested) const {
  if (requested.isValid()) {
    return requested;
  }
  if (checkbox && checkbox->value().isValid()) {
    return checkbox->value();
  }
  return checkbox ? QVariant(checkbox->text()) : QVariant();
}

void AdCheckboxGroup::addCheckbox(AdCheckbox* checkbox, const QVariant& value) {
  if (!checkbox) {
    return;
  }
  if (checkbox->group_ && checkbox->group_ != this) {
    checkbox->group_->removeCheckbox(checkbox);
  }
  if (!buttons().contains(checkbox)) {
    QButtonGroup::addButton(checkbox);
  }
  if (std::none_of(order_.cbegin(), order_.cend(),
                   [checkbox](const QPointer<AdCheckbox>& item) { return item == checkbox; })) {
    order_.append(checkbox);
    attachCheckbox(checkbox);
  }
  const QVariant resolvedValue = effectiveValue(checkbox, value);
  optionValues_.insert(checkbox, resolvedValue);
  if (checkbox->value() != resolvedValue) {
    checkbox->setValue(resolvedValue);
  }
  checkbox->setGroup(this);
  if (!enabled_) {
    enabledBeforeGroupDisable_.insert(checkbox, checkbox->isEnabled());
    checkbox->setEnabled(false);
  }
  refreshLayoutSpacing();
  emitValuesIfChanged();
}

void AdCheckboxGroup::removeCheckbox(AdCheckbox* checkbox) {
  if (!checkbox || !buttons().contains(checkbox)) {
    return;
  }
  QButtonGroup::removeButton(checkbox);
  detachCheckbox(checkbox);
  optionValues_.remove(checkbox);
  if (!enabled_ && enabledBeforeGroupDisable_.contains(checkbox)) {
    checkbox->setEnabled(enabledBeforeGroupDisable_.take(checkbox));
  } else {
    enabledBeforeGroupDisable_.remove(checkbox);
  }
  order_.erase(std::remove_if(order_.begin(), order_.end(),
                              [checkbox](const QPointer<AdCheckbox>& item) {
                                return item.isNull() || item.data() == checkbox;
                              }),
               order_.end());
  checkbox->setGroup(nullptr);
  refreshLayoutSpacing();
  emitValuesIfChanged();
}

QList<AdCheckbox*> AdCheckboxGroup::checkboxes() const {
  QList<AdCheckbox*> result;
  result.reserve(order_.size());
  for (const QPointer<AdCheckbox>& checkbox : order_) {
    if (checkbox && buttons().contains(checkbox)) {
      result.append(checkbox);
    }
  }
  return result;
}

QVariant AdCheckboxGroup::value(const AdCheckbox* checkbox) const {
  return optionValues_.value(const_cast<AdCheckbox*>(checkbox));
}

void AdCheckboxGroup::setValue(AdCheckbox* checkbox, const QVariant& value) {
  if (!checkbox || !buttons().contains(checkbox)) {
    return;
  }
  if (optionValues_.value(checkbox) == value) {
    return;
  }
  optionValues_.insert(checkbox, value);
  if (checkbox->value() != value) {
    checkbox->setValue(value);
  }
  emitValuesIfChanged();
}

QVariantList AdCheckboxGroup::values() const {
  QVariantList result;
  for (AdCheckbox* checkbox : checkboxes()) {
    if (checkbox->checkState() == Qt::Checked) {
      result.append(optionValues_.value(checkbox));
    }
  }
  return result;
}

void AdCheckboxGroup::setValues(const QVariantList& values) {
  {
    QScopedValueRollback<bool> guard(syncing_, true);
    for (AdCheckbox* checkbox : checkboxes()) {
      const bool shouldCheck = values.contains(optionValues_.value(checkbox));
      const Qt::CheckState nextState = shouldCheck ? Qt::Checked : Qt::Unchecked;
      if (checkbox->checkState() != nextState) {
        checkbox->setCheckState(nextState);
      }
    }
  }
  emitValuesIfChanged();
}

void AdCheckboxGroup::clear() { setValues({}); }

bool AdCheckboxGroup::isEnabled() const { return enabled_; }

void AdCheckboxGroup::setEnabled(bool value) {
  if (enabled_ == value) {
    return;
  }
  enabled_ = value;
  for (AdCheckbox* checkbox : checkboxes()) {
    if (!enabled_) {
      enabledBeforeGroupDisable_.insert(checkbox, checkbox->isEnabled());
      checkbox->setEnabled(false);
    } else if (enabledBeforeGroupDisable_.contains(checkbox)) {
      checkbox->setEnabled(enabledBeforeGroupDisable_.take(checkbox));
    }
  }
  refreshCheckboxes(false);
  emit enabledChanged(enabled_);
}

AdCheckboxGroup::ComponentTokens AdCheckboxGroup::componentTokens() const {
  return componentTokens_;
}

void AdCheckboxGroup::setComponentTokens(const ComponentTokens& value) {
  if (AdCheckbox::componentTokensEqual(componentTokens_, value)) {
    return;
  }
  componentTokens_ = value;
  refreshCheckboxes();
  emit componentTokensChanged();
}

void AdCheckboxGroup::resetComponentTokens() { setComponentTokens({}); }

void AdCheckboxGroup::setComponentTokenResolver(ComponentTokenResolver resolver) {
  if (!componentTokenResolver_ && !resolver) {
    return;
  }
  componentTokenResolver_ = std::move(resolver);
  refreshCheckboxes();
  emit componentTokensChanged();
}

void AdCheckboxGroup::resetComponentTokenResolver() { setComponentTokenResolver({}); }

QBoxLayout* AdCheckboxGroup::managedLayout() const { return managedLayout_; }

void AdCheckboxGroup::setManagedLayout(QBoxLayout* layout) {
  if (managedLayout_ == layout) {
    return;
  }
  managedLayout_ = layout;
  refreshLayoutSpacing();
}

void AdCheckboxGroup::refreshCheckboxes(bool geometryChanged) {
  for (AdCheckbox* checkbox : checkboxes()) {
    if (!enabled_ && checkbox->hasFocus()) {
      checkbox->clearFocus();
    }
    checkbox->refreshAfterPropertyChange(geometryChanged);
  }
  refreshLayoutSpacing();
}

void AdCheckboxGroup::refreshLayoutSpacing() {
  if (!managedLayout_) {
    return;
  }
  int spacing = 0;
  const QList<AdCheckbox*> current = checkboxes();
  if (!current.isEmpty()) {
    spacing = std::max(0, current.first()->horizontalSpacingHint());
  }
  managedLayout_->setSpacing(spacing);
  managedLayout_->invalidate();
}

void AdCheckboxGroup::handleValuePropertyChanged(AdCheckbox* checkbox) {
  if (!checkbox || !buttons().contains(checkbox)) {
    return;
  }
  setValue(checkbox, checkbox->value());
}

void AdCheckboxGroup::emitValuesIfChanged() {
  const QVariantList current = values();
  if (lastValues_ == current) {
    return;
  }
  lastValues_ = current;
  emit valuesChanged(lastValues_);
}

void AdCheckboxGroup::attachCheckbox(AdCheckbox* checkbox) {
  if (!checkbox || checkboxConnections_.contains(checkbox)) {
    return;
  }
  QList<QMetaObject::Connection> connections;
  connections.append(connect(checkbox, &AdCheckbox::valueChanged, this,
                             [this, checkbox]() { handleValuePropertyChanged(checkbox); }));
  connections.append(connect(checkbox, &QObject::destroyed, this, [this, checkbox]() {
    checkboxConnections_.remove(checkbox);
    optionValues_.remove(checkbox);
    enabledBeforeGroupDisable_.remove(checkbox);
    order_.erase(std::remove_if(order_.begin(), order_.end(),
                                [checkbox](const QPointer<AdCheckbox>& item) {
                                  return item.isNull() || item.data() == checkbox;
                                }),
                 order_.end());
    refreshLayoutSpacing();
    emitValuesIfChanged();
  }));
  checkboxConnections_.insert(checkbox, connections);
}

void AdCheckboxGroup::detachCheckbox(AdCheckbox* checkbox) {
  const QList<QMetaObject::Connection> connections = checkboxConnections_.take(checkbox);
  for (const QMetaObject::Connection& connection : connections) {
    disconnect(connection);
  }
}

}  // namespace adqt::widgets
