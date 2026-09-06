#include "overlay_accessibility.h"

#include <QAccessible>
#include <QAccessibleEvent>
#include <QVariant>
#include <QWidget>

namespace adqt::widgets::detail {

namespace {

constexpr char kManagedDescriptionProperty[] = "_adqt_overlay_managed_accessible_description";
constexpr char kPreviousDescriptionProperty[] = "_adqt_overlay_previous_accessible_description";
constexpr char kManagedDescriptionValueProperty[] =
    "_adqt_overlay_managed_accessible_description_value";

void setManagedDescriptionFlag(QWidget* widget, bool managed) {
  if (!widget) {
    return;
  }
  widget->setProperty(kManagedDescriptionProperty, managed);
}

bool hasManagedDescription(const QWidget* widget) {
  return widget && widget->property(kManagedDescriptionProperty).toBool();
}

QString managedDescriptionValue(const QWidget* widget) {
  return widget ? widget->property(kManagedDescriptionValueProperty).toString() : QString();
}

void clearManagedDescriptionTracking(QWidget* widget) {
  if (!widget) {
    return;
  }
  widget->setProperty(kPreviousDescriptionProperty, QVariant());
  widget->setProperty(kManagedDescriptionValueProperty, QVariant());
  setManagedDescriptionFlag(widget, false);
}

}  // namespace

void notifyAccessibilityEvent(QWidget* widget, QAccessible::Event eventType) {
  if (!widget) {
    return;
  }
  QAccessibleEvent event(widget, eventType);
  QAccessible::updateAccessibility(&event);
}

void syncDerivedAccessibleDescription(QWidget* widget, const QString& description) {
  if (!widget) {
    return;
  }

  const QString trimmed = description.trimmed();
  if (trimmed.isEmpty()) {
    clearDerivedAccessibleDescription(widget);
    return;
  }

  const bool managed = hasManagedDescription(widget);
  const QString current = widget->accessibleDescription();
  const QString managedValue = managedDescriptionValue(widget);
  if (managed && current != managedValue) {
    clearManagedDescriptionTracking(widget);
    return;
  }
  if (!managed && !current.trimmed().isEmpty()) {
    return;
  }

  if (!managed) {
    widget->setProperty(kPreviousDescriptionProperty, current);
    setManagedDescriptionFlag(widget, true);
  }

  if (current == trimmed) {
    widget->setProperty(kManagedDescriptionValueProperty, trimmed);
    return;
  }

  widget->setAccessibleDescription(trimmed);
  widget->setProperty(kManagedDescriptionValueProperty, trimmed);
  notifyAccessibilityEvent(widget, QAccessible::DescriptionChanged);
}

void clearDerivedAccessibleDescription(QWidget* widget) {
  if (!widget || !hasManagedDescription(widget)) {
    return;
  }

  const QString current = widget->accessibleDescription();
  const QString managedValue = managedDescriptionValue(widget);
  if (current != managedValue) {
    clearManagedDescriptionTracking(widget);
    return;
  }

  const QString previous = widget->property(kPreviousDescriptionProperty).toString();
  widget->setAccessibleDescription(previous);
  clearManagedDescriptionTracking(widget);
  notifyAccessibilityEvent(widget, QAccessible::DescriptionChanged);
}

}  // namespace adqt::widgets::detail
