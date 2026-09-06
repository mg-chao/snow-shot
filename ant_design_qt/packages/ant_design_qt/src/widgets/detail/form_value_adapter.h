#pragma once

#include <QMetaObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVector>

#include <functional>

class QObject;
class QWidget;

namespace adqt::widgets::detail {

QStringList parseFieldPath(const QString& value);
QString joinFieldPath(const QStringList& path);
bool sameFieldPath(const QStringList& lhs, const QStringList& rhs);

QVariantList variantToList(const QVariant& value);
QVariant valueAtFieldPath(const QVariant& source, const QStringList& path, bool* found = nullptr);
QVariant setValueAtFieldPath(const QVariant& container, const QStringList& path, int index,
                             const QVariant& value);

QVariant readWidgetValue(QWidget* widget,
                         const QString& valuePropertyName = QStringLiteral("value"));
void writeWidgetValue(QWidget* widget, const QVariant& value,
                      const QString& valuePropertyName = QStringLiteral("value"));
QVector<QMetaObject::Connection> connectWidgetValueSignals(QWidget* widget, QObject* receiver,
                                                           const std::function<void()>& callback);

}  // namespace adqt::widgets::detail
