#pragma once

#include <QAccessible>
#include <QString>

class QWidget;

namespace adqt::widgets::detail {

void notifyAccessibilityEvent(QWidget* widget, QAccessible::Event eventType);
void syncDerivedAccessibleDescription(QWidget* widget, const QString& description);
void clearDerivedAccessibleDescription(QWidget* widget);

}  // namespace adqt::widgets::detail
