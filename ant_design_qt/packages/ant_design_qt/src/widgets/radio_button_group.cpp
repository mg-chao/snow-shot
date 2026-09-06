#include "radio_button_group.h"

#include "theme/theme.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QEvent>
#include <QLayoutItem>
#include <QScopedValueRollback>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace adqt::widgets {

namespace {

Qt::Orientation orientationForLayout(const QBoxLayout* layout) {
  if (!layout) {
    return Qt::Horizontal;
  }

  switch (layout->direction()) {
    case QBoxLayout::TopToBottom:
    case QBoxLayout::BottomToTop:
      return Qt::Vertical;
    case QBoxLayout::LeftToRight:
    case QBoxLayout::RightToLeft:
    default:
      return Qt::Horizontal;
  }
}

bool isReverseLayout(const QBoxLayout* layout, const QWidget* widget) {
  if (!layout) {
    return widget && widget->layoutDirection() == Qt::RightToLeft;
  }

  switch (layout->direction()) {
    case QBoxLayout::RightToLeft:
    case QBoxLayout::BottomToTop:
      return true;
    case QBoxLayout::LeftToRight:
      return widget && widget->layoutDirection() == Qt::RightToLeft;
    case QBoxLayout::TopToBottom:
    default:
      return false;
  }
}

bool shouldRefreshForEventType(QEvent::Type type) {
  switch (type) {
    case QEvent::LayoutRequest:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::EnabledChange:
    case QEvent::ParentChange:
    case QEvent::ChildAdded:
    case QEvent::ChildRemoved:
    case QEvent::FontChange:
    case QEvent::ApplicationFontChange:
    case QEvent::LayoutDirectionChange:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
      return true;
    default:
      return false;
  }
}

}  // namespace

AdRadioButtonGroup::AdRadioButtonGroup(QObject* parent) : QButtonGroup(parent) {
  setExclusive(true);
  connect(this, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
    if (!checked) {
      updateButtonStackingOrder();
      return;
    }
    if (!syncing_) {
      emit checkedIdChanged(id);
    }
    updateButtonStackingOrder();
  });
}

AdRadioButtonGroup::~AdRadioButtonGroup() {
  removeManagedLayoutFilters(managedLayout_);

  const QList<AdRadio*> currentRadios = radios();
  for (AdRadio* radio : currentRadios) {
    if (!radio) {
      continue;
    }
    radio->removeEventFilter(this);
    radio->setGroupPosition(AdRadio::GroupPosition::None);
    radio->setGroupVertical(false);
    radio->setGroup(nullptr);
    radio->setAutoExclusive(true);
  }
}

void AdRadioButtonGroup::addButton(QAbstractButton* button, int id) {
  if (!button) {
    return;
  }

  AdRadio* radio = qobject_cast<AdRadio*>(button);
  AdRadio::EffectiveStateSnapshot beforeState;
  if (radio) {
    beforeState = radio->captureEffectiveState(true);
    if (radio->group_ && radio->group_ != this) {
      radio->group_->removeButton(radio);
    }
  }

  const int previousCheckedId = checkedId();
  if (buttons().contains(button)) {
    QButtonGroup::removeButton(button);
  }

  QButtonGroup::addButton(button, effectiveId(id, button));

  if (radio) {
    attachRadio(radio);
  }

  refreshManagedLayoutState();

  if (radio) {
    radio->applyEffectiveStateChange(beforeState, true);
  }

  maybeEmitCheckedIdChanged(previousCheckedId);
}

void AdRadioButtonGroup::removeButton(QAbstractButton* button) {
  if (!button || !buttons().contains(button)) {
    return;
  }

  AdRadio* radio = qobject_cast<AdRadio*>(button);
  AdRadio::EffectiveStateSnapshot beforeState;
  if (radio) {
    beforeState = radio->captureEffectiveState(true);
  }

  const int previousCheckedId = checkedId();
  QButtonGroup::removeButton(button);

  if (radio) {
    detachRadio(radio);
  }

  refreshManagedLayoutState();

  if (radio) {
    radio->applyEffectiveStateChange(beforeState, true);
  }

  maybeEmitCheckedIdChanged(previousCheckedId);
}

void AdRadioButtonGroup::setId(QAbstractButton* button, int id) {
  if (!button || !buttons().contains(button)) {
    return;
  }

  const int previousCheckedId = checkedId();
  QButtonGroup::setId(button, effectiveId(id, button));
  maybeEmitCheckedIdChanged(previousCheckedId);
}

int AdRadioButtonGroup::checkedId() const { return QButtonGroup::checkedId(); }

void AdRadioButtonGroup::setCheckedId(int id) {
  const int previousCheckedId = checkedId();
  if (previousCheckedId == id) {
    return;
  }
  QScopedValueRollback<bool> guard(syncing_, true);

  QAbstractButton* target = nullptr;
  for (QAbstractButton* button : buttons()) {
    if (button && this->id(button) == id) {
      target = button;
      break;
    }
  }

  const bool restoreExclusive = !target && exclusive();
  if (restoreExclusive) {
    setExclusive(false);
  }

  for (QAbstractButton* button : buttons()) {
    if (!button) {
      continue;
    }
    const bool shouldCheck = button == target;
    if (button->isChecked() != shouldCheck) {
      button->setChecked(shouldCheck);
    }
  }

  if (restoreExclusive) {
    setExclusive(true);
  }

  maybeEmitCheckedIdChanged(previousCheckedId);
  updateButtonStackingOrder();
}

AdRadio::ControlSize AdRadioButtonGroup::controlSize() const { return controlSize_; }

void AdRadioButtonGroup::setControlSize(AdRadio::ControlSize value) {
  if (controlSize_ == value) {
    return;
  }

  const QList<RadioSnapshot> snapshots = snapshotRadios(true);
  controlSize_ = value;
  refreshManagedLayoutState();
  applySnapshots(snapshots, true);
  emit controlSizeChanged(controlSize_);
}

AdRadio::Variant AdRadioButtonGroup::variant() const { return variant_; }

void AdRadioButtonGroup::setVariant(AdRadio::Variant value) {
  if (variant_ == value) {
    return;
  }

  const QList<RadioSnapshot> snapshots = snapshotRadios(true);
  variant_ = value;
  refreshManagedLayoutState();
  applySnapshots(snapshots, true);
  emit variantChanged(variant_);
}

AdRadio::ButtonStyle AdRadioButtonGroup::buttonStyle() const { return buttonStyle_; }

void AdRadioButtonGroup::setButtonStyle(AdRadio::ButtonStyle value) {
  if (buttonStyle_ == value) {
    return;
  }

  const QList<RadioSnapshot> snapshots = snapshotRadios(true);
  buttonStyle_ = value;
  refreshManagedLayoutState();
  applySnapshots(snapshots, true);
  emit buttonStyleChanged(buttonStyle_);
}

AdRadioButtonGroup::Distribution AdRadioButtonGroup::distribution() const { return distribution_; }

void AdRadioButtonGroup::setDistribution(Distribution value) {
  if (distribution_ == value) {
    return;
  }

  const QList<RadioSnapshot> snapshots = snapshotRadios(true);
  distribution_ = value;
  refreshManagedLayoutState();
  applySnapshots(snapshots, true);
  emit distributionChanged(distribution_);
}

AdRadioButtonGroup::ComponentTokens AdRadioButtonGroup::componentTokens() const {
  return componentTokens_;
}

void AdRadioButtonGroup::setComponentTokens(const ComponentTokens& value) {
  const QList<RadioSnapshot> snapshots = snapshotRadios(true);
  componentTokens_ = value;
  refreshManagedLayoutState();
  applySnapshots(snapshots, true);
  emit componentTokensChanged();
}

void AdRadioButtonGroup::resetComponentTokens() {
  const QList<RadioSnapshot> snapshots = snapshotRadios(true);
  componentTokens_ = {};
  refreshManagedLayoutState();
  applySnapshots(snapshots, true);
  emit componentTokensChanged();
}

void AdRadioButtonGroup::setComponentTokenResolver(ComponentTokenResolver resolver) {
  const bool hadResolver = static_cast<bool>(componentTokenResolver_);
  const bool hasResolver = static_cast<bool>(resolver);
  if (!hadResolver && !hasResolver) {
    return;
  }

  const QList<RadioSnapshot> snapshots = snapshotRadios(true);
  componentTokenResolver_ = std::move(resolver);
  refreshManagedLayoutState();
  applySnapshots(snapshots, true);
  emit componentTokensChanged();
}

void AdRadioButtonGroup::resetComponentTokenResolver() {
  if (!componentTokenResolver_) {
    return;
  }
  setComponentTokenResolver(ComponentTokenResolver{});
}

QBoxLayout* AdRadioButtonGroup::managedLayout() const { return managedLayout_; }

void AdRadioButtonGroup::setManagedLayout(QBoxLayout* layout) {
  if (managedLayout_ == layout) {
    return;
  }

  removeManagedLayoutFilters(managedLayout_);
  managedLayout_ = layout;
  installManagedLayoutFilters(managedLayout_);
  refreshManagedLayoutState();
}

AdRadio* AdRadioButtonGroup::checkedRadio() const {
  return qobject_cast<AdRadio*>(checkedButton());
}

bool AdRadioButtonGroup::eventFilter(QObject* watched, QEvent* event) {
  const bool handled = QButtonGroup::eventFilter(watched, event);
  if (!refreshingLayout_ && event && event->type() == QEvent::LayoutRequest) {
    // A LayoutRequest means the managed layout is already invalid. Reusing the
    // full refresh path here would invalidate it again and post another
    // LayoutRequest, keeping the GUI event queue permanently busy.
    QScopedValueRollback<bool> guard(refreshingLayout_, true);
    syncManagedLayoutGeometry();
    refreshSegmentStates();
    return handled;
  }
  if (!refreshingLayout_ && event && shouldRefreshForEventType(event->type())) {
    refreshManagedLayoutState();
  }
  return handled;
}

QList<AdRadioButtonGroup::RadioSnapshot> AdRadioButtonGroup::snapshotRadios(
    bool includeTokens) const {
  QList<RadioSnapshot> snapshots;
  const QList<AdRadio*> currentRadios = radios();
  snapshots.reserve(currentRadios.size());
  for (AdRadio* radio : currentRadios) {
    if (!radio) {
      continue;
    }
    snapshots.append({radio, radio->captureEffectiveState(includeTokens)});
  }
  return snapshots;
}

void AdRadioButtonGroup::applySnapshots(const QList<RadioSnapshot>& snapshots,
                                        bool tokensMayChange) {
  for (const RadioSnapshot& snapshot : snapshots) {
    if (!snapshot.radio) {
      continue;
    }
    snapshot.radio->applyEffectiveStateChange(snapshot.state, tokensMayChange);
  }
}

QList<AdRadio*> AdRadioButtonGroup::radios() const {
  QList<AdRadio*> result;
  const QList<QAbstractButton*> currentButtons = buttons();
  result.reserve(currentButtons.size());
  for (QAbstractButton* button : currentButtons) {
    if (AdRadio* radio = qobject_cast<AdRadio*>(button)) {
      result.append(radio);
    }
  }
  return result;
}

QList<AdRadio*> AdRadioButtonGroup::orderedRadios(bool visibleOnly) const {
  QList<AdRadio*> result;
  const QList<AdRadio*> currentRadios = radios();
  result.reserve(currentRadios.size());

  if (managedLayout_) {
    for (int index = 0; index < managedLayout_->count(); ++index) {
      QLayoutItem* item = managedLayout_->itemAt(index);
      if (!item) {
        continue;
      }
      AdRadio* radio = qobject_cast<AdRadio*>(item->widget());
      if (!radio || !currentRadios.contains(radio)) {
        continue;
      }
      result.append(radio);
    }
  }

  for (AdRadio* radio : currentRadios) {
    if (radio && !result.contains(radio)) {
      result.append(radio);
    }
  }

  if (visibleOnly) {
    result.erase(std::remove_if(result.begin(), result.end(),
                                [](AdRadio* radio) { return !radio || radio->isHidden(); }),
                 result.end());
  }

  if (result.size() < 2) {
    return result;
  }

  QWidget* hostWidget = managedLayout_ ? managedLayout_->parentWidget() : nullptr;
  const bool vertical = orientationForLayout(managedLayout_) == Qt::Vertical;
  const bool reverse = isReverseLayout(managedLayout_, hostWidget);

  const bool allVisible = std::all_of(result.cbegin(), result.cend(), [](const AdRadio* radio) {
    return radio && radio->isVisible();
  });

  if (allVisible) {
    std::sort(result.begin(), result.end(),
              [vertical, reverse](const AdRadio* lhs, const AdRadio* rhs) {
                if (!lhs || !rhs || lhs == rhs) {
                  return false;
                }

                if (vertical) {
                  return reverse ? lhs->geometry().bottom() > rhs->geometry().bottom()
                                 : lhs->geometry().top() < rhs->geometry().top();
                }

                return reverse ? lhs->geometry().right() > rhs->geometry().right()
                               : lhs->geometry().left() < rhs->geometry().left();
              });
    return result;
  }

  if (reverse) {
    std::reverse(result.begin(), result.end());
  }
  return result;
}

void AdRadioButtonGroup::refreshManagedLayoutState() {
  if (refreshingLayout_) {
    return;
  }

  QScopedValueRollback<bool> guard(refreshingLayout_, true);
  refreshManagedLayoutSpacing();
  refreshManagedLayoutDistribution();
  syncManagedLayoutGeometry();
  refreshSegmentStates();

  if (managedLayout_) {
    managedLayout_->invalidate();
    if (QWidget* widget = managedLayout_->parentWidget()) {
      widget->updateGeometry();
      widget->update();
    }
  }
}

void AdRadioButtonGroup::refreshManagedLayoutSpacing() {
  if (!managedLayout_) {
    return;
  }

  int spacing = 0;
  const QList<AdRadio*> currentRadios = radios();
  const QWidget* spacingSource =
      managedLayout_->parentWidget()
          ? static_cast<const QWidget*>(managedLayout_->parentWidget())
          : static_cast<const QWidget*>(currentRadios.isEmpty() ? nullptr : currentRadios.front());

  if (variant_ == AdRadio::Variant::Button) {
    spacing = 0;
  } else if (orientationForLayout(managedLayout_) != Qt::Vertical && !currentRadios.isEmpty() &&
             currentRadios.front()) {
    spacing = std::max(0, currentRadios.front()->horizontalSpacingHint());
  } else {
    spacing = std::max(
        0, qRound(adqt::theme::ThemeManager::instance().resolveTheme(spacingSource).sizeXS));
  }

  managedLayout_->setSpacing(spacing);
}

void AdRadioButtonGroup::refreshManagedLayoutDistribution() {
  const QList<AdRadio*> currentRadios = radios();
  for (AdRadio* radio : currentRadios) {
    if (radio) {
      radio->syncManagedSizePolicy();
    }
  }

  if (!managedLayout_) {
    return;
  }

  for (int index = 0; index < managedLayout_->count(); ++index) {
    QLayoutItem* item = managedLayout_->itemAt(index);
    AdRadio* radio = item ? qobject_cast<AdRadio*>(item->widget()) : nullptr;
    if (!radio || !currentRadios.contains(radio)) {
      continue;
    }
    managedLayout_->setStretch(index, distribution_ == Distribution::Fill ? 1 : 0);
  }
}

void AdRadioButtonGroup::syncManagedLayoutGeometry() {
  if (!managedLayout_) {
    return;
  }

  managedLayout_->activate();
  if (!usesButtonGroupingLayout()) {
    return;
  }

  const int overlap = buttonGroupOverlapPixels();
  QList<AdRadio*> visibleRadios = orderedRadios(true);
  if (overlap <= 0 || visibleRadios.size() < 2) {
    return;
  }

  QWidget* hostWidget = managedLayout_->parentWidget();
  const bool vertical = orientationForLayout(managedLayout_) == Qt::Vertical;
  const bool reverse = isReverseLayout(managedLayout_, hostWidget);

  for (int visualIndex = 1; visualIndex < visibleRadios.size(); ++visualIndex) {
    AdRadio* radio = visibleRadios.at(visualIndex);
    if (!radio) {
      continue;
    }

    const int delta = visualIndex * overlap;
    if (vertical) {
      radio->move(radio->x(), radio->y() + (reverse ? delta : -delta));
    } else {
      radio->move(radio->x() + (reverse ? delta : -delta), radio->y());
    }
  }
}

void AdRadioButtonGroup::refreshSegmentStates() {
  const bool vertical = orientationForLayout(managedLayout_) == Qt::Vertical;
  const QList<AdRadio*> visibleRadios = orderedRadios(true);
  const bool joinedButtons = usesButtonGroupingLayout();

  for (AdRadio* radio : radios()) {
    if (!radio) {
      continue;
    }
    radio->setGroupVertical(vertical);
    if (!joinedButtons || !visibleRadios.contains(radio)) {
      radio->setGroupPosition(AdRadio::GroupPosition::None);
    }
  }

  if (joinedButtons) {
    for (int index = 0; index < visibleRadios.size(); ++index) {
      AdRadio* radio = visibleRadios.at(index);
      if (!radio) {
        continue;
      }

      AdRadio::GroupPosition position = AdRadio::GroupPosition::None;
      if (visibleRadios.size() == 1) {
        position = AdRadio::GroupPosition::Only;
      } else if (index == 0) {
        position = AdRadio::GroupPosition::First;
      } else if (index == visibleRadios.size() - 1) {
        position = AdRadio::GroupPosition::Last;
      } else {
        position = AdRadio::GroupPosition::Middle;
      }
      radio->setGroupPosition(position);
    }
  }

  updateButtonStackingOrder();
}

void AdRadioButtonGroup::updateButtonStackingOrder() {
  if (variant_ != AdRadio::Variant::Button) {
    return;
  }

  const QList<AdRadio*> visibleRadios = orderedRadios(true);
  for (AdRadio* radio : visibleRadios) {
    if (radio) {
      radio->raise();
    }
  }

  for (AdRadio* radio : visibleRadios) {
    if (radio && radio->isChecked() && radio->isEnabled()) {
      radio->raise();
    }
  }
}

bool AdRadioButtonGroup::tryHandleNavigation(AdRadio* radio, int key) const {
  if (!radio || !radio->isEnabled()) {
    return false;
  }

  const QList<AdRadio*> candidates = orderedRadios(true);
  const int currentIndex = static_cast<int>(candidates.indexOf(radio));
  if (currentIndex < 0 || candidates.size() < 2) {
    return false;
  }

  int delta = 0;
  if (orientationForLayout(managedLayout_) == Qt::Vertical) {
    if (key == Qt::Key_Up) {
      delta = -1;
    } else if (key == Qt::Key_Down) {
      delta = 1;
    } else {
      return false;
    }
  } else {
    const QWidget* hostWidget = managedLayout_ ? managedLayout_->parentWidget() : nullptr;
    const bool reverse = isReverseLayout(managedLayout_, hostWidget);
    if (key == Qt::Key_Left) {
      delta = reverse ? 1 : -1;
    } else if (key == Qt::Key_Right) {
      delta = reverse ? -1 : 1;
    } else {
      return false;
    }
  }

  const int count = static_cast<int>(candidates.size());
  for (int offset = 1; offset < count; ++offset) {
    const int nextIndex = (currentIndex + delta * offset + count * 2) % count;
    AdRadio* candidate = candidates.value(nextIndex);
    if (!candidate || candidate == radio || !candidate->isEnabled()) {
      continue;
    }
    candidate->setFocus(Qt::TabFocusReason);
    if (!candidate->isChecked()) {
      candidate->setChecked(true);
    }
    return true;
  }

  return false;
}

bool AdRadioButtonGroup::usesButtonGroupingLayout() const {
  return managedLayout_ && variant_ == AdRadio::Variant::Button;
}

int AdRadioButtonGroup::buttonGroupOverlapPixels() const {
  const QList<AdRadio*> visibleRadios = orderedRadios(true);
  for (AdRadio* radio : visibleRadios) {
    if (radio) {
      return radio->buttonGroupOverlapHint();
    }
  }
  return 0;
}

AdRadio* AdRadioButtonGroup::visibleNeighbor(const AdRadio* radio, int delta) const {
  if (!radio || delta == 0) {
    return nullptr;
  }

  const QList<AdRadio*> visibleRadios = orderedRadios(true);
  const int index = static_cast<int>(visibleRadios.indexOf(const_cast<AdRadio*>(radio)));
  if (index < 0) {
    return nullptr;
  }

  const int nextIndex = index + delta;
  if (nextIndex < 0 || nextIndex >= visibleRadios.size()) {
    return nullptr;
  }
  return visibleRadios.value(nextIndex);
}

int AdRadioButtonGroup::nextAutomaticId() const {
  int candidate = -2;
  while (true) {
    bool used = false;
    for (QAbstractButton* button : buttons()) {
      if (button && QButtonGroup::id(button) == candidate) {
        used = true;
        break;
      }
    }
    if (!used) {
      return candidate;
    }
    --candidate;
  }
}

int AdRadioButtonGroup::effectiveId(int requestedId, const QAbstractButton* ignoreButton) const {
  if (requestedId == -1) {
    return nextAutomaticId();
  }

  for (QAbstractButton* button : buttons()) {
    if (!button || button == ignoreButton) {
      continue;
    }
    if (QButtonGroup::id(button) == requestedId) {
      return nextAutomaticId();
    }
  }
  return requestedId;
}

void AdRadioButtonGroup::attachRadio(AdRadio* radio) {
  if (!radio) {
    return;
  }

  radio->setAutoExclusive(false);
  radio->setGroup(this);
  radio->installEventFilter(this);
  disconnect(radio, &QObject::destroyed, this, nullptr);
  connect(radio, &QObject::destroyed, this, [this]() { refreshManagedLayoutState(); });
}

void AdRadioButtonGroup::detachRadio(AdRadio* radio) {
  if (!radio) {
    return;
  }

  radio->removeEventFilter(this);
  radio->setGroupPosition(AdRadio::GroupPosition::None);
  radio->setGroupVertical(false);
  radio->setGroup(nullptr);
  radio->setAutoExclusive(true);
}

void AdRadioButtonGroup::installManagedLayoutFilters(QBoxLayout* layout) {
  if (!layout) {
    return;
  }
  layout->installEventFilter(this);
  if (QObject* parentObject = layout->parent()) {
    parentObject->installEventFilter(this);
  }
}

void AdRadioButtonGroup::removeManagedLayoutFilters(QBoxLayout* layout) {
  if (!layout) {
    return;
  }
  layout->removeEventFilter(this);
  if (QObject* parentObject = layout->parent()) {
    parentObject->removeEventFilter(this);
  }
}

void AdRadioButtonGroup::maybeEmitCheckedIdChanged(int previousCheckedId) {
  const int nextCheckedId = checkedId();
  if (!syncing_ && previousCheckedId != nextCheckedId) {
    emit checkedIdChanged(nextCheckedId);
  }
}

}  // namespace adqt::widgets
