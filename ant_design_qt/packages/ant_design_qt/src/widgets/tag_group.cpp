#include "tag_group.h"

#include "detail/flow_layout.h"
#include "theme/theme.h"

#include <QEvent>
#include <QLayoutItem>
#include <QPaintEvent>
#include <QPainter>

#include <tuple>
#include <utility>

namespace adqt::widgets {

namespace {

bool componentTokensEqual(const AdTagGroup::ComponentTokens& lhs,
                          const AdTagGroup::ComponentTokens& rhs) {
  return std::tie(lhs.spacing, lhs.padding, lhs.borderRadius) ==
         std::tie(rhs.spacing, rhs.padding, rhs.borderRadius);
}

bool semanticSlotStyleEqual(const AdTagGroup::SemanticSlotStyle& lhs,
                            const AdTagGroup::SemanticSlotStyle& rhs) {
  return std::tie(lhs.textColor, lhs.backgroundColor, lhs.borderColor) ==
         std::tie(rhs.textColor, rhs.backgroundColor, rhs.borderColor);
}

bool semanticStylesEqual(const AdTagGroup::SemanticStyles& lhs,
                         const AdTagGroup::SemanticStyles& rhs) {
  return semanticSlotStyleEqual(lhs.root, rhs.root) && semanticSlotStyleEqual(lhs.item, rhs.item);
}

template <typename T>
void mergeOptional(std::optional<T>* target, const std::optional<T>& source) {
  if (target && source.has_value()) {
    *target = source;
  }
}

void mergeComponentTokens(AdTagGroup::ComponentTokens* target,
                          const AdTagGroup::ComponentTokens& source) {
  if (!target) {
    return;
  }
  mergeOptional(&target->spacing, source.spacing);
  mergeOptional(&target->padding, source.padding);
  mergeOptional(&target->borderRadius, source.borderRadius);
}

void mergeSemanticSlotStyle(AdTagGroup::SemanticSlotStyle* target,
                            const AdTagGroup::SemanticSlotStyle& source) {
  if (!target) {
    return;
  }
  mergeOptional(&target->textColor, source.textColor);
  mergeOptional(&target->backgroundColor, source.backgroundColor);
  mergeOptional(&target->borderColor, source.borderColor);
}

void mergeSemanticStyles(AdTagGroup::SemanticStyles* target,
                         const AdTagGroup::SemanticStyles& source) {
  if (!target) {
    return;
  }
  mergeSemanticSlotStyle(&target->root, source.root);
  mergeSemanticSlotStyle(&target->item, source.item);
}

}  // namespace

AdTagGroup::AdTagGroup(QWidget* parent) : QFrame(parent) {
  setFrameShape(QFrame::NoFrame);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

  flowLayout_ = new detail::FlowLayout();
  setLayout(flowLayout_);
  applyContainerStyle();
}

AdTagGroup::~AdTagGroup() = default;

AdTagGroup::SelectionMode AdTagGroup::selectionMode() const { return selectionMode_; }

void AdTagGroup::setSelectionMode(SelectionMode value) {
  if (selectionMode_ == value) {
    return;
  }

  const QVariantList previousValues = selectedValues_;
  selectionMode_ = value;
  selectedValues_ = normalizedValues(selectedValues_);
  applySelectionToTags();
  applyContainerStyle();

  emit selectionModeChanged(selectionMode_);
  if (previousValues != selectedValues_) {
    emit selectedValueChanged(selectedValue());
    emit selectedValuesChanged(selectedValues_);
  }
}

QVariant AdTagGroup::selectedValue() const {
  return selectedValues_.isEmpty() ? QVariant() : selectedValues_.constFirst();
}

void AdTagGroup::setSelectedValue(const QVariant& value) {
  if (!value.isValid()) {
    setSelectedValues({});
    return;
  }
  setSelectedValues(QVariantList{value});
}

QVariantList AdTagGroup::selectedValues() const { return selectedValues_; }

void AdTagGroup::setSelectedValues(const QVariantList& values) {
  const QVariantList normalized = normalizedValues(values);
  if (normalized == selectedValues_) {
    return;
  }

  selectedValues_ = normalized;
  applySelectionToTags();
  emit selectedValueChanged(selectedValue());
  emit selectedValuesChanged(selectedValues_);
}

QVector<AdTagGroup::Option> AdTagGroup::options() const { return options_; }

void AdTagGroup::setOptions(const QVector<Option>& options) {
  options_ = options;
  rebuildTags();
}

void AdTagGroup::clearOptions() { setOptions({}); }

int AdTagGroup::spacing() const { return spacing_; }

void AdTagGroup::setSpacing(int value) {
  const int normalized = value < 0 ? -1 : value;
  if (spacing_ == normalized) {
    return;
  }
  spacing_ = normalized;
  applyContainerStyle();
  emit spacingChanged(spacing_);
}

AdTagGroup::ComponentTokens AdTagGroup::componentTokens() const { return componentTokens_; }

void AdTagGroup::setComponentTokens(const ComponentTokens& value) {
  if (componentTokensEqual(componentTokens_, value)) {
    return;
  }
  componentTokens_ = value;
  applyContainerStyle();
  emit componentTokensChanged();
}

void AdTagGroup::resetComponentTokens() { setComponentTokens(ComponentTokens{}); }

void AdTagGroup::setComponentTokenResolver(ComponentTokenResolver resolver) {
  const bool hadResolver = static_cast<bool>(componentTokenResolver_);
  const bool hasResolver = static_cast<bool>(resolver);
  if (!hadResolver && !hasResolver) {
    return;
  }
  componentTokenResolver_ = std::move(resolver);
  applyContainerStyle();
  emit componentTokensChanged();
}

void AdTagGroup::resetComponentTokenResolver() { setComponentTokenResolver({}); }

AdTagGroup::SemanticStyles AdTagGroup::semanticStyles() const { return semanticStyles_; }

void AdTagGroup::setSemanticStyles(const SemanticStyles& styles) {
  if (semanticStylesEqual(semanticStyles_, styles)) {
    return;
  }
  semanticStyles_ = styles;
  applyContainerStyle();
  emit semanticStylesChanged();
}

void AdTagGroup::resetSemanticStyles() { setSemanticStyles(SemanticStyles{}); }

void AdTagGroup::setSemanticStyleResolver(SemanticStyleResolver resolver) {
  const bool hadResolver = static_cast<bool>(semanticStyleResolver_);
  const bool hasResolver = static_cast<bool>(resolver);
  if (!hadResolver && !hasResolver) {
    return;
  }
  semanticStyleResolver_ = std::move(resolver);
  applyContainerStyle();
  emit semanticStylesChanged();
}

void AdTagGroup::resetSemanticStyleResolver() { setSemanticStyleResolver({}); }

void AdTagGroup::paintEvent(QPaintEvent* event) {
  QFrame::paintEvent(event);

  const SemanticStyles styles = resolvedSemanticStyles();
  const ComponentTokens tokens = resolvedComponentTokens();
  if (!styles.root.backgroundColor.has_value() && !styles.root.borderColor.has_value()) {
    return;
  }

  const adqt::theme::ResolvedTheme resolvedTheme =
      adqt::theme::ThemeManager::instance().resolve(this);
  const int radius =
      tokens.borderRadius.value_or(std::max(0, qRound(resolvedTheme.values.borderRadiusSM)));

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF rectF = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  const QColor background = styles.root.backgroundColor.value_or(Qt::transparent);
  const QColor border = styles.root.borderColor.value_or(Qt::transparent);

  painter.setPen(Qt::NoPen);
  painter.setBrush(background);
  painter.drawRoundedRect(rectF, radius, radius);

  if (border.alpha() > 0) {
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rectF, radius, radius);
  }
}

void AdTagGroup::changeEvent(QEvent* event) {
  QFrame::changeEvent(event);
  if (!event) {
    return;
  }

  switch (event->type()) {
    case QEvent::EnabledChange:
    case QEvent::FontChange:
    case QEvent::ApplicationFontChange:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::LayoutDirectionChange:
    case QEvent::StyleChange:
      applyContainerStyle();
      break;
    default:
      break;
  }
}

AdTagGroup::ComponentTokens AdTagGroup::resolvedComponentTokens() const {
  ComponentTokens resolved;
  mergeComponentTokens(&resolved, componentTokens_);
  if (componentTokenResolver_) {
    mergeComponentTokens(&resolved, componentTokenResolver_(currentComponentTokenContext()));
  }
  return resolved;
}

AdTagGroup::SemanticStyles AdTagGroup::resolvedSemanticStyles() const {
  SemanticStyles resolved;
  mergeSemanticStyles(&resolved, semanticStyles_);
  if (semanticStyleResolver_) {
    mergeSemanticStyles(&resolved, semanticStyleResolver_(currentStyleContext()));
  }
  return resolved;
}

AdTagGroup::ComponentTokenContext AdTagGroup::currentComponentTokenContext() const {
  ComponentTokenContext context;
  context.selectionMode = selectionMode_;
  context.disabled = !isEnabled();
  context.selectedValues = selectedValues_;
  return context;
}

AdTagGroup::StyleContext AdTagGroup::currentStyleContext() const {
  StyleContext context;
  context.selectionMode = selectionMode_;
  context.disabled = !isEnabled();
  context.selectedValues = selectedValues_;
  return context;
}

void AdTagGroup::rebuildTags() {
  while (flowLayout_ && flowLayout_->count() > 0) {
    QLayoutItem* item = flowLayout_->takeAt(0);
    if (item) {
      if (QWidget* widget = item->widget()) {
        widget->deleteLater();
      }
      delete item;
    }
  }
  itemTags_.clear();

  for (const Option& option : std::as_const(options_)) {
    auto* tag = new AdTag(option.label, this);
    tag->setCheckable(true);
    tag->setIconRef(option.iconRef);
    tag->setEnabled(isEnabled() && !option.disabled);
    tag->setChecked(selectedValues_.contains(option.value));
    tag->setProperty("adqt.tag-group.value", option.value);
    connect(tag, &QAbstractButton::toggled, this,
            [this, tag](bool checked) { updateSelectionFromTag(tag, checked); });
    flowLayout_->addWidget(tag);
    itemTags_.append(tag);
  }

  const QVariantList normalized = normalizedValues(selectedValues_);
  const bool selectionChanged = normalized != selectedValues_;
  selectedValues_ = normalized;
  applyContainerStyle();
  applySelectionToTags();
  if (selectionChanged) {
    emit selectedValueChanged(selectedValue());
    emit selectedValuesChanged(selectedValues_);
  }
}

void AdTagGroup::applySelectionToTags() {
  syncing_ = true;
  for (int i = 0; i < itemTags_.size(); ++i) {
    AdTag* tag = itemTags_.at(i);
    if (!tag) {
      continue;
    }
    const QVariant value = tag->property("adqt.tag-group.value");
    tag->setChecked(selectedValues_.contains(value));
  }
  syncing_ = false;
}

void AdTagGroup::applyContainerStyle() {
  if (!flowLayout_) {
    return;
  }

  const adqt::theme::ResolvedTheme resolvedTheme =
      adqt::theme::ThemeManager::instance().resolve(this);
  const ComponentTokens tokens = resolvedComponentTokens();
  const SemanticStyles semantic = resolvedSemanticStyles();

  const int resolvedSpacing =
      spacing_ >= 0 ? spacing_
                    : tokens.spacing.value_or(std::max(0, qRound(resolvedTheme.values.sizeXS)));
  const QMargins resolvedPadding = tokens.padding.value_or(QMargins());

  flowLayout_->setHorizontalSpacing(resolvedSpacing);
  flowLayout_->setVerticalSpacing(resolvedSpacing);
  flowLayout_->setContentsMargins(resolvedPadding);

  for (int i = 0; i < itemTags_.size(); ++i) {
    if (i >= options_.size()) {
      break;
    }
    AdTag* tag = itemTags_.at(i);
    if (!tag) {
      continue;
    }

    const Option& option = options_.at(i);
    tag->setEnabled(isEnabled() && !option.disabled);

    AdTag::SemanticStyles itemStyles;
    itemStyles.root.textColor = semantic.item.textColor;
    itemStyles.root.backgroundColor = semantic.item.backgroundColor;
    itemStyles.root.borderColor = semantic.item.borderColor;
    itemStyles.content.textColor = semantic.item.textColor;
    itemStyles.icon.textColor = semantic.item.textColor;
    tag->setSemanticStyles(itemStyles);
  }

  updateGeometry();
  update();
}

void AdTagGroup::updateSelectionFromTag(AdTag* tag, bool checked) {
  if (syncing_ || !tag) {
    return;
  }

  const QVariant value = tag->property("adqt.tag-group.value");
  if (!value.isValid()) {
    return;
  }

  QVariantList nextValues = selectedValues_;
  if (selectionMode_ == SelectionMode::Single) {
    nextValues = checked ? QVariantList{value} : QVariantList{};
  } else if (checked) {
    if (!nextValues.contains(value)) {
      nextValues.append(value);
    }
  } else {
    nextValues.removeAll(value);
  }

  nextValues = normalizedValues(nextValues);
  if (nextValues == selectedValues_) {
    applySelectionToTags();
    return;
  }

  selectedValues_ = nextValues;
  applySelectionToTags();
  emit selectedValueChanged(selectedValue());
  emit selectedValuesChanged(selectedValues_);
}

QVariantList AdTagGroup::normalizedValues(const QVariantList& values) const {
  QVariantList normalized;
  for (const QVariant& value : values) {
    bool exists = false;
    for (const Option& option : options_) {
      if (option.value == value) {
        exists = true;
        break;
      }
    }
    if (!exists || normalized.contains(value)) {
      continue;
    }
    normalized.append(value);
    if (selectionMode_ == SelectionMode::Single) {
      break;
    }
  }
  return normalized;
}

}  // namespace adqt::widgets
