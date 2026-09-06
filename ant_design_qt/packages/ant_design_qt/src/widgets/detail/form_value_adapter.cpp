#include "widgets/detail/form_value_adapter.h"

#include "widgets/form.h"
#include "widgets/input_number.h"
#include "widgets/input_text_edit.h"
#include "widgets/select.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextEdit>

namespace adqt::widgets::detail {

namespace {

bool segmentToListIndex(const QString& segment, int* indexOut = nullptr) {
  bool ok = false;
  const int index = segment.toInt(&ok);
  if (!ok || index < 0 || QString::number(index) != segment.trimmed()) {
    return false;
  }
  if (indexOut) {
    *indexOut = index;
  }
  return true;
}

QVariant widgetPropertyValue(QWidget* widget, const QString& propertyName) {
  if (!widget || propertyName.trimmed().isEmpty()) {
    return {};
  }
  const QByteArray propertyKey = propertyName.trimmed().toUtf8();
  return widget->property(propertyKey.constData());
}

bool writeWidgetPropertyValue(QWidget* widget, const QString& propertyName, const QVariant& value) {
  if (!widget || propertyName.trimmed().isEmpty()) {
    return false;
  }
  const QByteArray propertyKey = propertyName.trimmed().toUtf8();
  return widget->setProperty(propertyKey.constData(), value);
}

}  // namespace

QStringList parseFieldPath(const QString& value) {
  QStringList result;
  QString token;
  bool inBracket = false;
  bool inQuote = false;
  QChar quoteChar;

  auto pushToken = [&]() {
    const QString trimmed = token.trimmed();
    if (!trimmed.isEmpty()) {
      result.append(trimmed);
    }
    token.clear();
  };

  for (int i = 0; i < value.size(); ++i) {
    const QChar ch = value.at(i);
    if (inQuote) {
      if (ch == quoteChar) {
        inQuote = false;
      } else {
        token.append(ch);
      }
      continue;
    }

    if (inBracket) {
      if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
        inQuote = true;
        quoteChar = ch;
      } else if (ch == QLatin1Char(']')) {
        pushToken();
        inBracket = false;
      } else {
        token.append(ch);
      }
      continue;
    }

    if (ch == QLatin1Char('.')) {
      pushToken();
    } else if (ch == QLatin1Char('[')) {
      pushToken();
      inBracket = true;
    } else {
      token.append(ch);
    }
  }

  pushToken();
  return result;
}

QString joinFieldPath(const QStringList& path) {
  QStringList normalized;
  for (const QString& part : path) {
    const QString trimmed = part.trimmed();
    if (!trimmed.isEmpty()) {
      normalized.append(trimmed);
    }
  }
  return normalized.join(QLatin1Char('.'));
}

bool sameFieldPath(const QStringList& lhs, const QStringList& rhs) { return lhs == rhs; }

QVariantList variantToList(const QVariant& value) {
  if (!value.isValid() || value.isNull()) {
    return {};
  }
  if (value.userType() == QMetaType::QStringList) {
    QVariantList result;
    const QStringList strings = value.toStringList();
    result.reserve(strings.size());
    for (const QString& item : strings) {
      result.append(item);
    }
    return result;
  }
  return value.toList();
}

QVariant valueAtFieldPath(const QVariant& source, const QStringList& path, bool* found) {
  QVariant current = source;
  bool ok = !path.isEmpty();
  for (const QString& segment : path) {
    if (!ok) {
      break;
    }

    int index = -1;
    if (segmentToListIndex(segment, &index)) {
      const QVariantList list = current.toList();
      if (index < 0 || index >= list.size()) {
        ok = false;
        break;
      }
      current = list.at(index);
    } else {
      const QVariantMap map = current.toMap();
      if (!map.contains(segment)) {
        ok = false;
        break;
      }
      current = map.value(segment);
    }
  }
  if (found) {
    *found = ok;
  }
  return ok ? current : QVariant();
}

QVariant setValueAtFieldPath(const QVariant& container, const QStringList& path, int index,
                             const QVariant& value) {
  if (index >= path.size()) {
    return value;
  }

  const QString& segment = path.at(index);
  int listIndex = -1;
  if (segmentToListIndex(segment, &listIndex)) {
    QVariantList list = container.toList();
    while (list.size() <= listIndex) {
      list.append(QVariant());
    }
    list[listIndex] = setValueAtFieldPath(list.value(listIndex), path, index + 1, value);
    return list;
  }

  QVariantMap map = container.toMap();
  map.insert(segment, setValueAtFieldPath(map.value(segment), path, index + 1, value));
  return map;
}

QVariant readWidgetValue(QWidget* widget, const QString& valuePropertyName) {
  if (!widget) {
    return {};
  }

  if (auto* formList = qobject_cast<AdFormList*>(widget)) {
    return formList->values();
  }
  if (auto* select = qobject_cast<AdSelect*>(widget)) {
    if (select->mode() == AdSelect::Mode::Multiple || select->mode() == AdSelect::Mode::Tags) {
      return select->currentValues();
    }
    return select->currentValue();
  }
  if (auto* inputNumber = qobject_cast<AdInputNumber*>(widget)) {
    if (!inputNumber->hasValue()) {
      return {};
    }
    return inputNumber->valueMode() == AdInputNumber::ValueMode::ExactDecimal
               ? QVariant(inputNumber->exactValue())
               : QVariant(inputNumber->value());
  }
  if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
    return lineEdit->text();
  }
  if (auto* textEdit = qobject_cast<AdTextEdit*>(widget)) {
    return textEdit->plainText();
  }
  if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
    return plainTextEdit->toPlainText();
  }
  if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
    return textEdit->toPlainText();
  }
  if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
    if (button->isCheckable()) {
      return button->isChecked();
    }
  }
  if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
    const QVariant data = comboBox->currentData();
    return data.isValid() ? data : QVariant(comboBox->currentText());
  }
  if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
    return spinBox->value();
  }
  if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
    return doubleSpinBox->value();
  }

  QVariant valueProperty = widgetPropertyValue(widget, valuePropertyName);
  if (valueProperty.isValid()) {
    return valueProperty;
  }
  if (valuePropertyName != QStringLiteral("value")) {
    const QVariant defaultValueProperty = widgetPropertyValue(widget, QStringLiteral("value"));
    if (defaultValueProperty.isValid()) {
      return defaultValueProperty;
    }
  }
  QVariant textProperty = widget->property("text");
  if (textProperty.isValid()) {
    return textProperty;
  }
  return {};
}

void writeWidgetValue(QWidget* widget, const QVariant& value, const QString& valuePropertyName) {
  if (!widget) {
    return;
  }

  if (auto* formList = qobject_cast<AdFormList*>(widget)) {
    if (!value.isValid() || value.isNull()) {
      formList->setValues({});
    } else {
      formList->setValues(variantToList(value));
    }
    return;
  }
  if (auto* select = qobject_cast<AdSelect*>(widget)) {
    const int type = value.userType();
    if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
      select->setCurrentValues(value.toList());
    } else {
      select->setCurrentValue(value);
    }
    return;
  }
  if (auto* inputNumber = qobject_cast<AdInputNumber*>(widget)) {
    if (!value.isValid() || value.isNull() || value.toString().isEmpty()) {
      inputNumber->clear();
    } else if (inputNumber->valueMode() == AdInputNumber::ValueMode::ExactDecimal) {
      inputNumber->setExactValue(value.toString());
    } else {
      inputNumber->setValue(value.toDouble());
    }
    return;
  }
  if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
    lineEdit->setText(value.toString());
    return;
  }
  if (auto* textEdit = qobject_cast<AdTextEdit*>(widget)) {
    textEdit->setPlainText(value.toString());
    return;
  }
  if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
    plainTextEdit->setPlainText(value.toString());
    return;
  }
  if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
    textEdit->setPlainText(value.toString());
    return;
  }
  if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
    if (button->isCheckable()) {
      button->setChecked(value.toBool());
      return;
    }
  }
  if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
    int index = comboBox->findData(value);
    if (index < 0) {
      index = comboBox->findText(value.toString());
    }
    if (index >= 0) {
      comboBox->setCurrentIndex(index);
    } else if (comboBox->isEditable()) {
      comboBox->setCurrentText(value.toString());
    }
    return;
  }
  if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
    spinBox->setValue(value.toInt());
    return;
  }
  if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
    doubleSpinBox->setValue(value.toDouble());
    return;
  }

  if (widgetPropertyValue(widget, valuePropertyName).isValid()) {
    writeWidgetPropertyValue(widget, valuePropertyName, value);
  } else if (valuePropertyName != QStringLiteral("value") &&
             widgetPropertyValue(widget, QStringLiteral("value")).isValid()) {
    writeWidgetPropertyValue(widget, QStringLiteral("value"), value);
  } else if (widget->property("text").isValid()) {
    widget->setProperty("text", value);
  }
}

QVector<QMetaObject::Connection> connectWidgetValueSignals(QWidget* widget, QObject* receiver,
                                                           const std::function<void()>& callback) {
  QVector<QMetaObject::Connection> connections;
  if (!widget || !receiver || !callback) {
    return connections;
  }

  auto bind = [&connections](const QMetaObject::Connection& connection) {
    connections.append(connection);
  };

  if (auto* formList = qobject_cast<AdFormList*>(widget)) {
    bind(QObject::connect(formList, &AdFormList::valuesChanged, receiver,
                          [callback](const QVariantList&) { callback(); }));
    return connections;
  }
  if (auto* select = qobject_cast<AdSelect*>(widget)) {
    bind(QObject::connect(select, &AdSelect::currentValueChanged, receiver,
                          [callback]() { callback(); }));
    bind(QObject::connect(select, &AdSelect::currentValuesChanged, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* inputNumber = qobject_cast<AdInputNumber*>(widget)) {
    bind(QObject::connect(inputNumber, &AdInputNumber::valueChanged, receiver,
                          [callback]() { callback(); }));
    bind(QObject::connect(inputNumber, &AdInputNumber::exactValueChanged, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
    bind(QObject::connect(lineEdit, &QLineEdit::textChanged, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* textEdit = qobject_cast<AdTextEdit*>(widget)) {
    bind(QObject::connect(textEdit, &AdTextEdit::plainTextChanged, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
    bind(QObject::connect(plainTextEdit, &QPlainTextEdit::textChanged, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
    bind(QObject::connect(textEdit, &QTextEdit::textChanged, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
    bind(QObject::connect(button, &QAbstractButton::toggled, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
    bind(QObject::connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), receiver,
                          [callback]() { callback(); }));
    bind(QObject::connect(comboBox, &QComboBox::currentTextChanged, receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
    bind(QObject::connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), receiver,
                          [callback]() { callback(); }));
    return connections;
  }
  if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
    bind(QObject::connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                          receiver, [callback]() { callback(); }));
  }
  return connections;
}

}  // namespace adqt::widgets::detail
