#include "input_number_value_model.h"

#include <QMetaType>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace adqt::widgets::detail {

namespace {

struct DecimalValue {
  bool valid = false;
  bool empty = true;
  bool negative = false;
  QString integer = QStringLiteral("0");
  QString decimal;
};

QString trimLeadingZeros(QString digits) {
  qsizetype index = 0;
  while (index + 1 < digits.size() && digits.at(index) == QLatin1Char('0')) {
    ++index;
  }
  if (index > 0) {
    digits.remove(0, index);
  }
  return digits.isEmpty() ? QStringLiteral("0") : digits;
}

QString trimTrailingZeros(QString digits) {
  while (!digits.isEmpty() && digits.endsWith(QLatin1Char('0'))) {
    digits.chop(1);
  }
  return digits;
}

bool isDigitString(const QString& text) {
  return std::all_of(text.cbegin(), text.cend(), [](QChar ch) { return ch.isDigit(); });
}

QString scientificToDecimalString(QString text) {
  text = text.trimmed();
  const qsizetype expIndex = text.indexOf(QRegularExpression(QStringLiteral("[eE]")));
  if (expIndex < 0) {
    return text;
  }

  QString mantissa = text.left(expIndex);
  bool expOk = false;
  const int exponent = text.mid(expIndex + 1).toInt(&expOk);
  if (!expOk) {
    return text;
  }

  QString sign;
  if (mantissa.startsWith(QLatin1Char('-')) || mantissa.startsWith(QLatin1Char('+'))) {
    sign = mantissa.left(1);
    mantissa.remove(0, 1);
  }

  const qsizetype dotIndex = mantissa.indexOf(QLatin1Char('.'));
  QString integerPart = dotIndex >= 0 ? mantissa.left(dotIndex) : mantissa;
  QString decimalPart = dotIndex >= 0 ? mantissa.mid(dotIndex + 1) : QString();
  if (integerPart.isEmpty()) {
    integerPart = QStringLiteral("0");
  }

  QString digits = integerPart + decimalPart;
  if (digits.isEmpty() || !isDigitString(digits)) {
    return text;
  }

  qsizetype decimalPoint = integerPart.size() + exponent;
  if (decimalPoint <= 0) {
    digits.prepend(QString(-decimalPoint + 1, QLatin1Char('0')));
    decimalPoint = 1;
  } else if (decimalPoint >= digits.size()) {
    digits.append(QString(decimalPoint - digits.size(), QLatin1Char('0')));
  }

  QString result = digits;
  if (decimalPoint < result.size()) {
    result.insert(decimalPoint, QLatin1Char('.'));
  }

  return sign + result;
}

QString doubleToPlainString(double value) {
  if (!std::isfinite(value)) {
    return QString();
  }

  std::ostringstream stream;
  stream.setf(std::ios::fmtflags(0), std::ios::floatfield);
  stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return scientificToDecimalString(QString::fromStdString(stream.str()));
}

QString normalizeLocaleNumberText(QString text, const QLocale& locale) {
  text = text.trimmed();
  if (text.isEmpty()) {
    return text;
  }

  const QString decimalPointText = locale.decimalPoint();
  const QString groupSeparatorText = locale.groupSeparator();
  const QChar decimalPoint = decimalPointText.isEmpty() ? QLatin1Char('.') : decimalPointText.at(0);
  const QChar groupSeparator = groupSeparatorText.isEmpty() ? QChar() : groupSeparatorText.at(0);
  const QString positiveSignText = locale.positiveSign();
  const QString negativeSignText = locale.negativeSign();
  const QChar plusSign = positiveSignText.isEmpty() ? QLatin1Char('+') : positiveSignText.at(0);
  const QChar minusSign = negativeSignText.isEmpty() ? QLatin1Char('-') : negativeSignText.at(0);

  QString normalized;
  normalized.reserve(text.size());
  for (const QChar ch : text) {
    if (!groupSeparator.isNull() && ch == groupSeparator) {
      continue;
    }
    if (decimalPoint != QLatin1Char('.') && ch == decimalPoint) {
      normalized.append(QLatin1Char('.'));
      continue;
    }
    if (plusSign != QLatin1Char('+') && ch == plusSign) {
      normalized.append(QLatin1Char('+'));
      continue;
    }
    if (minusSign != QLatin1Char('-') && ch == minusSign) {
      normalized.append(QLatin1Char('-'));
      continue;
    }
    normalized.append(ch);
  }
  return normalized;
}

DecimalValue parseCanonicalDecimalString(QString text) {
  DecimalValue value;
  text = text.trimmed();
  if (text.isEmpty()) {
    return value;
  }

  if (text.startsWith(QLatin1Char('+'))) {
    text.remove(0, 1);
  }

  if (text.startsWith(QLatin1Char('-'))) {
    value.negative = true;
    text.remove(0, 1);
  }

  const qsizetype dotIndex = text.indexOf(QLatin1Char('.'));
  if (dotIndex >= 0 && text.indexOf(QLatin1Char('.'), dotIndex + 1) >= 0) {
    return value;
  }

  const QString rawIntegerPart = dotIndex >= 0 ? text.left(dotIndex) : text;
  QString integerPart = rawIntegerPart;
  QString decimalPart = dotIndex >= 0 ? text.mid(dotIndex + 1) : QString();

  if (rawIntegerPart.isEmpty() && decimalPart.isEmpty()) {
    return value;
  }

  if (integerPart.isEmpty()) {
    integerPart = QStringLiteral("0");
  }

  if ((!integerPart.isEmpty() && !isDigitString(integerPart)) ||
      (!decimalPart.isEmpty() && !isDigitString(decimalPart))) {
    return value;
  }

  integerPart = trimLeadingZeros(integerPart);
  decimalPart = trimTrailingZeros(decimalPart);
  if (integerPart == QStringLiteral("0") && decimalPart.isEmpty()) {
    value.negative = false;
  }

  value.valid = true;
  value.empty = false;
  value.integer = integerPart;
  value.decimal = decimalPart;
  return value;
}

QString decimalToCanonicalString(const DecimalValue& value) {
  if (!value.valid || value.empty) {
    return QString();
  }
  return QStringLiteral("%1%2%3")
      .arg(value.negative ? QStringLiteral("-") : QString())
      .arg(value.integer)
      .arg(value.decimal.isEmpty() ? QString() : QStringLiteral(".%1").arg(value.decimal));
}

DecimalValue parseDecimalVariant(const QVariant& value, const QLocale& locale) {
  if (!value.isValid()) {
    return DecimalValue();
  }

  switch (value.userType()) {
    case QMetaType::Double:
    case QMetaType::Float:
      return parseCanonicalDecimalString(doubleToPlainString(value.toDouble()));
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
      return parseCanonicalDecimalString(value.toString());
    default:
      break;
  }

  QString text = normalizeLocaleNumberText(value.toString(), locale);
  if (text.contains(QRegularExpression(QStringLiteral("[eE]")))) {
    text = scientificToDecimalString(text);
  }
  return parseCanonicalDecimalString(text);
}

int comparePositiveDigits(const QString& lhs, const QString& rhs) {
  const QString left = trimLeadingZeros(lhs);
  const QString right = trimLeadingZeros(rhs);
  if (left.size() != right.size()) {
    return left.size() < right.size() ? -1 : 1;
  }
  if (left == right) {
    return 0;
  }
  return left < right ? -1 : 1;
}

QString addPositiveDigits(const QString& lhs, const QString& rhs) {
  QString result;
  result.reserve(std::max(lhs.size(), rhs.size()) + 1);

  qsizetype leftIndex = lhs.size() - 1;
  qsizetype rightIndex = rhs.size() - 1;
  int carry = 0;
  while (leftIndex >= 0 || rightIndex >= 0 || carry > 0) {
    int sum = carry;
    if (leftIndex >= 0) {
      sum += lhs.at(leftIndex).digitValue();
      --leftIndex;
    }
    if (rightIndex >= 0) {
      sum += rhs.at(rightIndex).digitValue();
      --rightIndex;
    }
    result.prepend(QChar(QLatin1Char('0').unicode() + (sum % 10)));
    carry = sum / 10;
  }

  return trimLeadingZeros(result);
}

QString subtractPositiveDigits(const QString& lhs, const QString& rhs) {
  QString result;
  result.reserve(lhs.size());

  qsizetype leftIndex = lhs.size() - 1;
  qsizetype rightIndex = rhs.size() - 1;
  int borrow = 0;
  while (leftIndex >= 0) {
    int value = lhs.at(leftIndex).digitValue() - borrow;
    if (rightIndex >= 0) {
      value -= rhs.at(rightIndex).digitValue();
      --rightIndex;
    }
    if (value < 0) {
      value += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }
    result.prepend(QChar(QLatin1Char('0').unicode() + value));
    --leftIndex;
  }

  return trimLeadingZeros(result);
}

QString scaledDigits(const DecimalValue& value, qsizetype scale) {
  QString digits = value.integer + value.decimal;
  if (scale > value.decimal.size()) {
    digits.append(QString(scale - value.decimal.size(), QLatin1Char('0')));
  }
  return trimLeadingZeros(digits);
}

QString scaledDigitsToCanonicalString(bool negative, QString digits, qsizetype scale) {
  digits = trimLeadingZeros(digits);
  if (scale > 0) {
    if (digits.size() <= scale) {
      digits.prepend(QString(scale - digits.size() + 1, QLatin1Char('0')));
    }
    digits.insert(digits.size() - scale, QLatin1Char('.'));
  }

  DecimalValue value = parseCanonicalDecimalString(digits);
  if (!value.valid) {
    return QString();
  }
  value.negative = negative && !(value.integer == QStringLiteral("0") && value.decimal.isEmpty());
  return decimalToCanonicalString(value);
}

QString scaledDigitsToFixedString(bool negative, QString digits, qsizetype scale) {
  digits = trimLeadingZeros(digits);
  if (scale > 0) {
    if (digits.size() <= scale) {
      digits.prepend(QString(scale - digits.size() + 1, QLatin1Char('0')));
    }
    digits.insert(digits.size() - scale, QLatin1Char('.'));
  }

  if (negative && !(digits == QStringLiteral("0") || digits == QStringLiteral("0.0"))) {
    digits.prepend(QLatin1Char('-'));
  }
  return digits;
}

int compareDecimalValues(const DecimalValue& lhs, const DecimalValue& rhs) {
  if (!lhs.valid || lhs.empty) {
    return (!rhs.valid || rhs.empty) ? 0 : -1;
  }
  if (!rhs.valid || rhs.empty) {
    return 1;
  }
  if (lhs.negative != rhs.negative) {
    return lhs.negative ? -1 : 1;
  }

  const qsizetype scale = std::max(lhs.decimal.size(), rhs.decimal.size());
  const QString leftDigits = scaledDigits(lhs, scale);
  const QString rightDigits = scaledDigits(rhs, scale);
  const int cmp = comparePositiveDigits(leftDigits, rightDigits);
  return lhs.negative ? -cmp : cmp;
}

QString addDecimalStrings(const QString& lhs, const QString& rhs) {
  const DecimalValue left = parseCanonicalDecimalString(lhs);
  const DecimalValue right = parseCanonicalDecimalString(rhs);
  if (!left.valid || !right.valid) {
    return QString();
  }

  const qsizetype scale = std::max(left.decimal.size(), right.decimal.size());
  const QString leftDigits = scaledDigits(left, scale);
  const QString rightDigits = scaledDigits(right, scale);

  bool negative = false;
  QString digits;
  if (left.negative == right.negative) {
    negative = left.negative;
    digits = addPositiveDigits(leftDigits, rightDigits);
  } else {
    const int cmp = comparePositiveDigits(leftDigits, rightDigits);
    if (cmp >= 0) {
      negative = left.negative;
      digits = subtractPositiveDigits(leftDigits, rightDigits);
    } else {
      negative = right.negative;
      digits = subtractPositiveDigits(rightDigits, leftDigits);
    }
  }

  return scaledDigitsToCanonicalString(negative, digits, scale);
}

QString fixedDecimalString(const QString& text, int decimals, bool cutOnly = false) {
  if (text.trimmed().isEmpty()) {
    return QString();
  }

  const DecimalValue value = parseCanonicalDecimalString(text);
  if (!value.valid) {
    return QString();
  }

  if (decimals < 0) {
    return decimalToCanonicalString(value);
  }

  QString kept = value.decimal.left(decimals);
  kept = kept.leftJustified(decimals, QLatin1Char('0'), true);
  QString digits = trimLeadingZeros(value.integer + kept);

  const int nextDigit =
      value.decimal.size() > decimals ? value.decimal.at(decimals).digitValue() : 0;
  if (!cutOnly && nextDigit >= 5) {
    digits = addPositiveDigits(digits, QStringLiteral("1"));
  }

  return scaledDigitsToFixedString(value.negative, digits, decimals);
}

int decimalsFromString(QString text) {
  text = text.trimmed();
  if (text.isEmpty()) {
    return 0;
  }
  if (text.contains(QRegularExpression(QStringLiteral("[eE]")))) {
    text = scientificToDecimalString(text);
  }
  const qsizetype dotIndex = text.indexOf(QLatin1Char('.'));
  if (dotIndex < 0) {
    return 0;
  }
  return static_cast<int>(text.size() - dotIndex - 1);
}

int decimalsFromVariant(const QVariant& value, const QLocale& locale) {
  if (!value.isValid()) {
    return 0;
  }
  switch (value.userType()) {
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
      return decimalsFromString(doubleToPlainString(value.toDouble()));
    default:
      return decimalsFromString(decimalToCanonicalString(parseDecimalVariant(value, locale)));
  }
}

QString normalizedDecimalString(const QVariant& value, const QLocale& locale) {
  return decimalToCanonicalString(parseDecimalVariant(value, locale));
}

int mergedPrecisionForText(const QString& text, const QVariant& singleStep, int explicitPrecision,
                           const QLocale& locale, bool userTyping) {
  if (userTyping) {
    return -1;
  }
  if (explicitPrecision >= 0) {
    return explicitPrecision;
  }
  return std::max(decimalsFromString(text), decimalsFromVariant(singleStep, locale));
}

QVariant variantFromCanonicalString(const QString& text, bool exactMode) {
  if (text.trimmed().isEmpty()) {
    return QVariant();
  }
  if (exactMode) {
    return QVariant(text);
  }
  bool ok = false;
  const double number = QLocale::c().toDouble(text, &ok);
  return ok && std::isfinite(number) ? QVariant(number) : QVariant();
}

QString positiveSignToken(const QLocale& locale) {
  return locale.positiveSign().isEmpty() ? QStringLiteral("+") : locale.positiveSign();
}

QString negativeSignToken(const QLocale& locale) {
  return locale.negativeSign().isEmpty() ? QStringLiteral("-") : locale.negativeSign();
}

QString decimalPointToken(const QLocale& locale) { return QString(locale.decimalPoint()); }

QString groupDigitsForLocale(QString digits, const QLocale& locale) {
  if (digits.isEmpty()) {
    return QStringLiteral("0");
  }

  const QString separator = locale.groupSeparator();
  if (separator.isEmpty()) {
    return digits;
  }

  QString grouped;
  grouped.reserve(digits.size() + digits.size() / 3);
  for (int i = 0; i < digits.size(); ++i) {
    if (i > 0 && ((digits.size() - i) % 3) == 0) {
      grouped += separator;
    }
    grouped += digits.at(i);
  }
  return grouped;
}

QString localizedDecimalText(const DecimalValue& value, const QLocale& locale) {
  if (!value.valid || value.empty) {
    return QString();
  }

  QString text;
  if (value.negative) {
    text += negativeSignToken(locale);
  }
  text += groupDigitsForLocale(value.integer, locale);
  if (!value.decimal.isEmpty()) {
    text += decimalPointToken(locale);
    text += value.decimal;
  }
  return text;
}

QString displayTextFromCanonical(const QString& text, const QLocale& locale, int decimals) {
  if (text.isEmpty()) {
    return QString();
  }

  QString display = text;
  if (decimals >= 0) {
    display = fixedDecimalString(display, decimals);
    if (display.isEmpty()) {
      display = text;
    }
  }

  const DecimalValue value = parseCanonicalDecimalString(display);
  if (!value.valid) {
    return text;
  }
  return localizedDecimalText(value, locale);
}

}  // namespace

InputNumberValueModel::InputNumberValueModel(Config config) : config_(std::move(config)) {}

const InputNumberValueModel::Config& InputNumberValueModel::config() const { return config_; }

void InputNumberValueModel::setConfig(const Config& config) { config_ = config; }

QVariant InputNumberValueModel::normalizeBoundaryValue(const QVariant& value) const {
  if (!value.isValid()) {
    return QVariant();
  }

  if (value.userType() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
    return QVariant();
  }

  const QString text = normalizedDecimalString(value, config_.locale);
  if (text.isEmpty()) {
    return QVariant();
  }
  return variantFromCanonicalString(text, config_.exactMode);
}

QVariant InputNumberValueModel::normalizeInputValue(const QVariant& value) const {
  return normalizeBoundaryValue(value);
}

QVariant InputNumberValueModel::normalizeSingleStepValue(const QVariant& value) const {
  QVariant normalized = normalizeBoundaryValue(value);
  const DecimalValue decimal = parseDecimalVariant(normalized, config_.locale);
  if (!normalized.isValid() || !decimal.valid ||
      compareDecimalValues(decimal, parseCanonicalDecimalString(QStringLiteral("0"))) <= 0) {
    normalized = variantFromCanonicalString(QStringLiteral("1"), config_.exactMode);
  }
  return normalized;
}

QVariant InputNumberValueModel::parseInputText(const QString& text,
                                               const std::function<QString(const QString&)>& parser,
                                               bool userTyping) const {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return QVariant();
  }

  const QString parsedText = parser ? parser(text) : text;
  QVariant parsed = QVariant(parsedText);
  QVariant normalized = normalizeInputValue(parsed);
  if (!normalized.isValid()) {
    return QVariant();
  }
  if (!userTyping) {
    normalized = applyDecimals(normalized);
  }
  return normalized;
}

QVariant InputNumberValueModel::clamp(const QVariant& value) const {
  if (!value.isValid()) {
    return QVariant();
  }

  QString text = normalizedDecimalString(value, config_.locale);
  if (text.isEmpty()) {
    return QVariant();
  }

  DecimalValue current = parseCanonicalDecimalString(text);
  const QString minText = normalizedDecimalString(config_.minimum, config_.locale);
  if (!minText.isEmpty()) {
    const DecimalValue minValue = parseCanonicalDecimalString(minText);
    if (minValue.valid && compareDecimalValues(current, minValue) < 0) {
      text = minText;
      current = minValue;
    }
  }

  const QString maxText = normalizedDecimalString(config_.maximum, config_.locale);
  if (!maxText.isEmpty()) {
    const DecimalValue maxValue = parseCanonicalDecimalString(maxText);
    if (maxValue.valid && compareDecimalValues(current, maxValue) > 0) {
      text = maxText;
    }
  }

  return variantFromCanonicalString(text, config_.exactMode);
}

QVariant InputNumberValueModel::applyDecimals(const QVariant& value) const {
  if (!value.isValid()) {
    return QVariant();
  }

  const QString text = normalizedDecimalString(value, config_.locale);
  if (text.isEmpty()) {
    return QVariant();
  }

  const int mergedPrecision =
      mergedPrecisionForText(text, config_.singleStep, config_.decimals, config_.locale, false);
  if (mergedPrecision < 0) {
    return variantFromCanonicalString(text, config_.exactMode);
  }

  QString fixed = fixedDecimalString(text, mergedPrecision);
  if (fixed.isEmpty()) {
    return QVariant();
  }

  if (!config_.permissiveRange) {
    const DecimalValue rounded = parseCanonicalDecimalString(fixed);
    const QString minText = normalizedDecimalString(config_.minimum, config_.locale);
    if (!minText.isEmpty()) {
      const DecimalValue minValue = parseCanonicalDecimalString(minText);
      if (rounded.valid && minValue.valid && compareDecimalValues(rounded, minValue) < 0) {
        fixed = fixedDecimalString(text, mergedPrecision, true);
      }
    }

    const QString maxText = normalizedDecimalString(config_.maximum, config_.locale);
    if (!maxText.isEmpty()) {
      const DecimalValue maxValue = parseCanonicalDecimalString(maxText);
      const DecimalValue roundedMax = parseCanonicalDecimalString(fixed);
      if (roundedMax.valid && maxValue.valid && compareDecimalValues(roundedMax, maxValue) > 0) {
        fixed = fixedDecimalString(text, mergedPrecision, true);
      }
    }
  }

  return variantFromCanonicalString(decimalToCanonicalString(parseCanonicalDecimalString(fixed)),
                                    config_.exactMode);
}

QVariant InputNumberValueModel::committedValue(const QVariant& value) const {
  const QVariant normalized = normalizeInputValue(value);
  if (!normalized.isValid()) {
    return QVariant();
  }
  return applyDecimals(config_.permissiveRange ? normalized : clamp(normalized));
}

QVariant InputNumberValueModel::steppedValue(const QVariant& currentValue, int steps) const {
  if (steps == 0) {
    return committedValue(currentValue);
  }

  QString stepText = normalizedDecimalString(config_.singleStep, config_.locale);
  const DecimalValue stepDecimal = parseCanonicalDecimalString(stepText);
  if (!stepDecimal.valid ||
      compareDecimalValues(stepDecimal, parseCanonicalDecimalString(QStringLiteral("0"))) <= 0) {
    stepText = QStringLiteral("1");
  }

  QString currentText = normalizedDecimalString(currentValue, config_.locale);
  if (currentText.isEmpty()) {
    currentText = steps > 0 ? normalizedDecimalString(config_.minimum, config_.locale)
                            : normalizedDecimalString(config_.maximum, config_.locale);
  }
  if (currentText.isEmpty()) {
    currentText = QStringLiteral("0");
  }

  QString nextText = currentText;
  const int count = std::abs(steps);
  for (int i = 0; i < count; ++i) {
    QString offsetText = stepText;
    if (steps < 0) {
      offsetText.prepend(QLatin1Char('-'));
    }
    nextText = addDecimalStrings(nextText, offsetText);
    if (nextText.isEmpty()) {
      return QVariant();
    }
  }

  return committedValue(QVariant(nextText));
}

QString InputNumberValueModel::canonicalText(const QVariant& value) const {
  return normalizedDecimalString(value, config_.locale);
}

QString InputNumberValueModel::defaultDisplayText(const QVariant& value, bool userTyping) const {
  if (!value.isValid()) {
    return QString();
  }

  QString text = normalizedDecimalString(value, config_.locale);
  if (text.isEmpty()) {
    return QString();
  }

  if (!userTyping) {
    const int mergedPrecision =
        mergedPrecisionForText(text, config_.singleStep, config_.decimals, config_.locale, false);
    if (mergedPrecision >= 0) {
      const QString fixed = fixedDecimalString(text, mergedPrecision);
      if (!fixed.isEmpty()) {
        text = fixed;
      }
    }
  }

  const int displayPrecision =
      userTyping ? -1
                 : mergedPrecisionForText(text, config_.singleStep, config_.decimals,
                                          config_.locale, false);
  return displayTextFromCanonical(text, config_.locale, displayPrecision);
}

bool InputNumberValueModel::equals(const QVariant& lhs, const QVariant& rhs) const {
  if (!lhs.isValid() && !rhs.isValid()) {
    return true;
  }
  if (lhs.isValid() != rhs.isValid()) {
    return false;
  }

  const QString left = normalizedDecimalString(lhs, config_.locale);
  const QString right = normalizedDecimalString(rhs, config_.locale);
  if (!left.isEmpty() && !right.isEmpty()) {
    return left == right;
  }

  if (lhs.userType() == rhs.userType()) {
    return lhs == rhs;
  }
  return lhs.toString() == rhs.toString();
}

bool InputNumberValueModel::isOutOfRange(const QVariant& value) const {
  const QString currentText = normalizedDecimalString(value, config_.locale);
  if (currentText.isEmpty()) {
    return false;
  }

  const DecimalValue current = parseCanonicalDecimalString(currentText);
  const QString minText = normalizedDecimalString(config_.minimum, config_.locale);
  if (!minText.isEmpty()) {
    const DecimalValue minValue = parseCanonicalDecimalString(minText);
    if (minValue.valid && compareDecimalValues(current, minValue) < 0) {
      return true;
    }
  }

  const QString maxText = normalizedDecimalString(config_.maximum, config_.locale);
  if (!maxText.isEmpty()) {
    const DecimalValue maxValue = parseCanonicalDecimalString(maxText);
    if (maxValue.valid && compareDecimalValues(current, maxValue) > 0) {
      return true;
    }
  }

  return false;
}

bool InputNumberValueModel::isStepDisabled(const QVariant& value, bool up) const {
  const QString currentText = normalizedDecimalString(value, config_.locale);
  if (currentText.isEmpty()) {
    return false;
  }

  const DecimalValue current = parseCanonicalDecimalString(currentText);
  if (!current.valid) {
    return false;
  }

  if (up) {
    const QString maxText = normalizedDecimalString(config_.maximum, config_.locale);
    if (maxText.isEmpty()) {
      return false;
    }
    const DecimalValue maxValue = parseCanonicalDecimalString(maxText);
    return maxValue.valid && compareDecimalValues(current, maxValue) >= 0;
  }

  const QString minText = normalizedDecimalString(config_.minimum, config_.locale);
  if (minText.isEmpty()) {
    return false;
  }
  const DecimalValue minValue = parseCanonicalDecimalString(minText);
  return minValue.valid && compareDecimalValues(current, minValue) <= 0;
}

bool InputNumberValueModel::lessThan(const QVariant& lhs, const QVariant& rhs) const {
  const QString left = normalizedDecimalString(lhs, config_.locale);
  const QString right = normalizedDecimalString(rhs, config_.locale);
  if (left.isEmpty() || right.isEmpty()) {
    return false;
  }
  return compareDecimalValues(parseCanonicalDecimalString(left),
                              parseCanonicalDecimalString(right)) < 0;
}

bool InputNumberValueModel::greaterThan(const QVariant& lhs, const QVariant& rhs) const {
  const QString left = normalizedDecimalString(lhs, config_.locale);
  const QString right = normalizedDecimalString(rhs, config_.locale);
  if (left.isEmpty() || right.isEmpty()) {
    return false;
  }
  return compareDecimalValues(parseCanonicalDecimalString(left),
                              parseCanonicalDecimalString(right)) > 0;
}

bool InputNumberValueModel::isIntermediateText(const QString& text, const QLocale& locale) {
  const QString trimmed = text.trimmed();
  const QString plus = positiveSignToken(locale);
  const QString minus = negativeSignToken(locale);
  const QString decimal = QRegularExpression::escape(decimalPointToken(locale));
  const QString decimalPoint = decimalPointToken(locale);
  if (trimmed.isEmpty() || trimmed == plus || trimmed == minus || trimmed == decimalPoint ||
      trimmed == (minus + decimalPoint) || trimmed == (plus + decimalPoint)) {
    return true;
  }

  const QString signPattern =
      QStringLiteral("(?:%1|%2)?")
          .arg(QRegularExpression::escape(plus), QRegularExpression::escape(minus));
  const QRegularExpression trailingDecimal(
      QStringLiteral("^%1[0-9]+%2$").arg(signPattern, decimal));
  const QRegularExpression leadingDecimal(QStringLiteral("^%1%2[0-9]*$").arg(signPattern, decimal));
  return trailingDecimal.match(trimmed).hasMatch() || leadingDecimal.match(trimmed).hasMatch();
}

}  // namespace adqt::widgets::detail
