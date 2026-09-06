#pragma once

#include <QLocale>
#include <QVariant>

#include <functional>

namespace adqt::widgets::detail {

class InputNumberValueModel {
 public:
  struct Config {
    QLocale locale;
    QVariant minimum;
    QVariant maximum;
    QVariant singleStep = QVariant(1.0);
    int decimals = -1;
    bool exactMode = false;
    bool permissiveRange = false;
  };

  InputNumberValueModel() = default;
  explicit InputNumberValueModel(Config config);

  const Config& config() const;
  void setConfig(const Config& config);

  QVariant normalizeBoundaryValue(const QVariant& value) const;
  QVariant normalizeInputValue(const QVariant& value) const;
  QVariant normalizeSingleStepValue(const QVariant& value) const;
  QVariant parseInputText(const QString& text, const std::function<QString(const QString&)>& parser,
                          bool userTyping) const;

  QVariant clamp(const QVariant& value) const;
  QVariant applyDecimals(const QVariant& value) const;
  QVariant committedValue(const QVariant& value) const;
  QVariant steppedValue(const QVariant& currentValue, int steps) const;

  QString canonicalText(const QVariant& value) const;
  QString defaultDisplayText(const QVariant& value, bool userTyping = false) const;

  bool equals(const QVariant& lhs, const QVariant& rhs) const;
  bool isOutOfRange(const QVariant& value) const;
  bool isStepDisabled(const QVariant& value, bool up) const;

  bool lessThan(const QVariant& lhs, const QVariant& rhs) const;
  bool greaterThan(const QVariant& lhs, const QVariant& rhs) const;

  static bool isIntermediateText(const QString& text, const QLocale& locale);

 private:
  Config config_;
};

}  // namespace adqt::widgets::detail
