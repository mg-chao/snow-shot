#pragma once

#include "radio.h"

#include <QButtonGroup>
#include <QList>
#include <QPointer>

class QBoxLayout;
class QEvent;

namespace adqt::widgets {

class AdRadioButtonGroup final : public QButtonGroup {
  Q_OBJECT

  Q_PROPERTY(int checkedId READ checkedId WRITE setCheckedId NOTIFY checkedIdChanged)
  Q_PROPERTY(adqt::widgets::AdRadio::ControlSize controlSize READ controlSize WRITE setControlSize
                 NOTIFY controlSizeChanged)
  Q_PROPERTY(
      adqt::widgets::AdRadio::Variant variant READ variant WRITE setVariant NOTIFY variantChanged)
  Q_PROPERTY(adqt::widgets::AdRadio::ButtonStyle buttonStyle READ buttonStyle WRITE setButtonStyle
                 NOTIFY buttonStyleChanged)
  Q_PROPERTY(
      Distribution distribution READ distribution WRITE setDistribution NOTIFY distributionChanged)

 public:
  using ComponentTokenContext = AdRadio::ComponentTokenContext;
  using ColorTokens = AdRadio::ColorTokens;
  using MetricTokens = AdRadio::MetricTokens;
  using ComponentTokens = AdRadio::ComponentTokens;
  using ComponentTokenResolver = AdRadio::ComponentTokenResolver;

  enum class Distribution {
    Content,
    Fill,
  };
  Q_ENUM(Distribution)

  explicit AdRadioButtonGroup(QObject* parent = nullptr);
  ~AdRadioButtonGroup() override;

  void addButton(QAbstractButton* button, int id = -1);
  void removeButton(QAbstractButton* button);
  void setId(QAbstractButton* button, int id);

  int checkedId() const;
  void setCheckedId(int id);

  AdRadio::ControlSize controlSize() const;
  void setControlSize(AdRadio::ControlSize value);

  AdRadio::Variant variant() const;
  void setVariant(AdRadio::Variant value);

  AdRadio::ButtonStyle buttonStyle() const;
  void setButtonStyle(AdRadio::ButtonStyle value);

  Distribution distribution() const;
  void setDistribution(Distribution value);

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& value);
  void resetComponentTokens();
  void setComponentTokenResolver(ComponentTokenResolver resolver);
  void resetComponentTokenResolver();

  QBoxLayout* managedLayout() const;
  void setManagedLayout(QBoxLayout* layout);

  AdRadio* checkedRadio() const;

 signals:
  void checkedIdChanged(int id);
  void controlSizeChanged(adqt::widgets::AdRadio::ControlSize value);
  void variantChanged(adqt::widgets::AdRadio::Variant value);
  void buttonStyleChanged(adqt::widgets::AdRadio::ButtonStyle value);
  void distributionChanged(adqt::widgets::AdRadioButtonGroup::Distribution value);
  void componentTokensChanged();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  friend class AdRadio;

  struct RadioSnapshot {
    AdRadio* radio = nullptr;
    AdRadio::EffectiveStateSnapshot state;
  };

  QList<AdRadio*> radios() const;
  QList<AdRadio*> orderedRadios(bool visibleOnly) const;
  QList<RadioSnapshot> snapshotRadios(bool includeTokens) const;
  void applySnapshots(const QList<RadioSnapshot>& snapshots, bool tokensMayChange);
  void refreshManagedLayoutState();
  void refreshManagedLayoutSpacing();
  void refreshManagedLayoutDistribution();
  void syncManagedLayoutGeometry();
  void refreshSegmentStates();
  void updateButtonStackingOrder();
  bool tryHandleNavigation(AdRadio* radio, int key) const;
  bool usesButtonGroupingLayout() const;
  int buttonGroupOverlapPixels() const;
  AdRadio* visibleNeighbor(const AdRadio* radio, int delta) const;
  int nextAutomaticId() const;
  int effectiveId(int requestedId, const QAbstractButton* ignoreButton = nullptr) const;
  void attachRadio(AdRadio* radio);
  void detachRadio(AdRadio* radio);
  void installManagedLayoutFilters(QBoxLayout* layout);
  void removeManagedLayoutFilters(QBoxLayout* layout);
  void maybeEmitCheckedIdChanged(int previousCheckedId);

  AdRadio::ControlSize controlSize_ = AdRadio::ControlSize::Medium;
  AdRadio::Variant variant_ = AdRadio::Variant::Default;
  AdRadio::ButtonStyle buttonStyle_ = AdRadio::ButtonStyle::Outline;
  Distribution distribution_ = Distribution::Content;
  ComponentTokens componentTokens_;
  ComponentTokenResolver componentTokenResolver_;
  QPointer<QBoxLayout> managedLayout_;
  bool syncing_ = false;
  bool refreshingLayout_ = false;
};

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdRadioButtonGroup::Distribution)
