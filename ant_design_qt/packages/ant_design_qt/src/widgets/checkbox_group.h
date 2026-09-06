#pragma once

#include "checkbox.h"

#include <QButtonGroup>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QVariantList>

class QBoxLayout;

namespace adqt::widgets {

class AdCheckboxGroup final : public QButtonGroup {
  Q_OBJECT

  Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY valuesChanged)
  Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

 public:
  using ComponentTokenContext = AdCheckbox::ComponentTokenContext;
  using ColorTokens = AdCheckbox::ColorTokens;
  using MetricTokens = AdCheckbox::MetricTokens;
  using ComponentTokens = AdCheckbox::ComponentTokens;
  using ComponentTokenResolver = AdCheckbox::ComponentTokenResolver;

  explicit AdCheckboxGroup(QObject* parent = nullptr);
  ~AdCheckboxGroup() override;

  void addButton(QAbstractButton* button, int id = -1);
  void removeButton(QAbstractButton* button);
  void addCheckbox(AdCheckbox* checkbox, const QVariant& value = {});
  void removeCheckbox(AdCheckbox* checkbox);
  QList<AdCheckbox*> checkboxes() const;

  QVariant value(const AdCheckbox* checkbox) const;
  void setValue(AdCheckbox* checkbox, const QVariant& value);

  QVariantList values() const;
  void setValues(const QVariantList& values);
  void clear();

  bool isEnabled() const;
  void setEnabled(bool value);

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& value);
  void resetComponentTokens();
  void setComponentTokenResolver(ComponentTokenResolver resolver);
  void resetComponentTokenResolver();

  QBoxLayout* managedLayout() const;
  void setManagedLayout(QBoxLayout* layout);

 signals:
  void valuesChanged(const QVariantList& values);
  void enabledChanged(bool value);
  void componentTokensChanged();

 private:
  friend class AdCheckbox;

  QVariant effectiveValue(AdCheckbox* checkbox, const QVariant& requested) const;
  void emitValuesIfChanged();
  void attachCheckbox(AdCheckbox* checkbox);
  void detachCheckbox(AdCheckbox* checkbox);
  void refreshCheckboxes(bool geometryChanged = true);
  void refreshLayoutSpacing();
  void handleValuePropertyChanged(AdCheckbox* checkbox);

  QList<QPointer<AdCheckbox>> order_;
  QHash<AdCheckbox*, QVariant> optionValues_;
  QHash<AdCheckbox*, bool> enabledBeforeGroupDisable_;
  QHash<AdCheckbox*, QList<QMetaObject::Connection>> checkboxConnections_;
  QVariantList lastValues_;
  bool enabled_ = true;
  ComponentTokens componentTokens_;
  ComponentTokenResolver componentTokenResolver_;
  QPointer<QBoxLayout> managedLayout_;
  bool syncing_ = false;
};

}  // namespace adqt::widgets
