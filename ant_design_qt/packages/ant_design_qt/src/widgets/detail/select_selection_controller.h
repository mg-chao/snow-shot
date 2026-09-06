#pragma once

#include "../select.h"

#include <QVariantList>

namespace adqt::widgets::detail {

class SelectSelectionController final {
 public:
  void setMode(AdSelect::Mode value);
  void setMaxCount(int value);

  AdSelect::Mode mode() const { return mode_; }
  int maxCount() const { return maxCount_; }

  QVariant currentValue() const;
  QVariantList currentValues() const { return values_; }
  QStringList currentKeys() const { return keys_; }

  int indexOf(const QVariant& value) const;
  bool contains(const QVariant& value) const;
  bool setCurrentValue(const QVariant& value);
  bool setCurrentValues(const QVariantList& values);
  bool appendValue(const QVariant& value);
  bool removeValue(const QVariant& value);
  bool removeLast(QVariant* removedValue = nullptr);
  bool clear();

 private:
  QVariantList normalizeValues(const QVariantList& values) const;

  AdSelect::Mode mode_ = AdSelect::Mode::Single;
  int maxCount_ = -1;
  QVariantList values_;
  QStringList keys_;
};

}  // namespace adqt::widgets::detail
