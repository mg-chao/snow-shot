#include "select_selection_controller.h"

#include "select_option_utils.h"

namespace adqt::widgets::detail {

void SelectSelectionController::setMode(AdSelect::Mode value) {
  mode_ = value;
  setCurrentValues(values_);
}

void SelectSelectionController::setMaxCount(int value) {
  maxCount_ = value;
  setCurrentValues(values_);
}

QVariant SelectSelectionController::currentValue() const {
  return values_.isEmpty() ? QVariant() : values_.constFirst();
}

int SelectSelectionController::indexOf(const QVariant& value) const {
  const QString key = selectValueKey(value);
  return key.isEmpty() ? -1 : static_cast<int>(keys_.indexOf(key));
}

bool SelectSelectionController::contains(const QVariant& value) const {
  return indexOf(value) >= 0;
}

bool SelectSelectionController::setCurrentValue(const QVariant& value) {
  return setCurrentValues(value.isValid() && !value.isNull() ? QVariantList{value}
                                                             : QVariantList());
}

bool SelectSelectionController::setCurrentValues(const QVariantList& values) {
  const QVariantList normalizedValues = normalizeValues(values);
  QStringList normalizedKeys;
  normalizedKeys.reserve(normalizedValues.size());
  for (const QVariant& value : normalizedValues) {
    normalizedKeys.append(selectValueKey(value));
  }

  if (values_ == normalizedValues && keys_ == normalizedKeys) {
    return false;
  }

  values_ = normalizedValues;
  keys_ = normalizedKeys;
  return true;
}

bool SelectSelectionController::appendValue(const QVariant& value) {
  QVariantList next = values_;
  next.append(value);
  return setCurrentValues(next);
}

bool SelectSelectionController::removeValue(const QVariant& value) {
  const int index = indexOf(value);
  if (index < 0) {
    return false;
  }

  QVariantList next = values_;
  next.removeAt(index);
  return setCurrentValues(next);
}

bool SelectSelectionController::removeLast(QVariant* removedValue) {
  if (values_.isEmpty()) {
    return false;
  }

  QVariantList next = values_;
  const QVariant removed = next.takeLast();
  const bool changed = setCurrentValues(next);
  if (changed && removedValue) {
    *removedValue = removed;
  }
  return changed;
}

bool SelectSelectionController::clear() { return setCurrentValues({}); }

QVariantList SelectSelectionController::normalizeValues(const QVariantList& values) const {
  QVariantList out;
  QStringList seenKeys;
  out.reserve(values.size());
  for (const QVariant& value : values) {
    const QVariant normalized = normalizeSelectValue(value);
    if (!normalized.isValid() || normalized.isNull()) {
      continue;
    }
    const QString key = selectValueKey(normalized);
    if (key.isEmpty() || seenKeys.contains(key)) {
      continue;
    }
    seenKeys.append(key);
    out.append(normalized);
  }

  if (mode_ == AdSelect::Mode::Single && out.size() > 1) {
    out = {out.constFirst()};
  }
  if (maxCount_ > 0 && out.size() > maxCount_) {
    out = out.mid(0, maxCount_);
  }
  return out;
}

}  // namespace adqt::widgets::detail
