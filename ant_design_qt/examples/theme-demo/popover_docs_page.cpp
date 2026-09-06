#include "popover_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "antd_icons.h"

using adqt::widgets::AdButton;
using adqt::widgets::AdPopover;

PopoverDocsPage::PopoverDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Popover");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "A floating card that appears on hover, focus, click or context menu, aligned to a source "
      "widget.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Simple title + text card.", buildBasicDemo());
  addSection(root, "Trigger Modes", "Hover / Focus / Click / ContextMenu.", buildTriggerDemo());
  addSection(root, "Placement", "12 placements with automatic fallback and clamping.",
             buildPlacementDemo());
  addSection(root, "Arrow", "Show/hide arrow and point-at-center behavior.", buildArrowDemo());
  addSection(root, "Auto Shift", "When near viewport edge, placement and position auto adjust.",
             buildAutoShiftDemo());
  addSection(root, "Popup Layer", "Qt extension: popupLayerMode.", buildPopupLayerModeDemo());
  addSection(root, "Controlled Open", "External state drives popup visibility.",
             buildControlledDemo());
  addSection(root, "Hover With Click", "Mixed nested interaction pattern.",
             buildHoverWithClickDemo());
  addSection(root, "Visual Styling", "Direct Qt-style color and font properties.",
             buildVisualStyleDemo());
  addSection(root, "Visual Properties", "Override layout, geometry and surface metrics.",
             buildVisualPropertyDemo());
  addSection(root, "API Overview", "Core API names and property meanings.", buildApiOverview());

  root->addStretch();
}

const QVector<QWidget*>& PopoverDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& PopoverDocsPage::sectionTitles() const { return titles_; }

void PopoverDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                 const QString& description, QWidget* content) {
  auto* panel = new QFrame();
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* titleLabel = new QLabel(title);
  QFont titleFont = titleLabel->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 1);
  titleLabel->setFont(titleFont);

  auto* descLabel = new QLabel(description);
  descLabel->setWordWrap(true);

  layout->addWidget(titleLabel);
  layout->addWidget(descLabel);
  layout->addWidget(content);

  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* PopoverDocsPage::makePopover(const QString& triggerText, const QString& title,
                                      const QString& content, Triggers triggers, QWidget* parent,
                                      AdPopover** popoverOut) {
  auto* trigger = new AdButton(triggerText, parent);
  trigger->setButtonStyle(AdButton::ButtonStyle::Outline);
  trigger->setAccentRole(AdButton::AccentRole::Neutral);

  auto* popover = new AdPopover(trigger);
  popover->setSourceWidget(trigger);
  popover->setTitle(title);
  popover->setText(content);
  popover->setTriggers(triggers);
  if (popoverOut) {
    *popoverOut = popover;
  }
  return trigger;
}

QWidget* PopoverDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* hover = makePopover("Hover me", "Title", "Content", Trigger::Hover);
  auto* click = makePopover("Click me", "Title", "Content", Trigger::Click);
  auto* focus = makePopover("Focus me", "Title", "Focusable trigger", Trigger::Focus);

  row->addWidget(hover);
  row->addWidget(click);
  row->addWidget(focus);
  row->addStretch();
  return box;
}

QWidget* PopoverDocsPage::buildTriggerDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  row->addWidget(makePopover("Hover", "Hover", "Trigger = hover", Trigger::Hover));
  row->addWidget(makePopover("Focus", "Focus", "Trigger = focus", Trigger::Focus));
  row->addWidget(makePopover("Click", "Click", "Trigger = click", Trigger::Click));
  row->addWidget(
      makePopover("ContextMenu", "Context Menu", "Right click me", Trigger::ContextMenu));
  row->addStretch();
  return box;
}

QWidget* PopoverDocsPage::buildPlacementDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* placementBox = new QComboBox();
  placementBox->addItem("top", static_cast<int>(Placement::Top));
  placementBox->addItem("topLeft", static_cast<int>(Placement::TopLeft));
  placementBox->addItem("topRight", static_cast<int>(Placement::TopRight));
  placementBox->addItem("bottom", static_cast<int>(Placement::Bottom));
  placementBox->addItem("bottomLeft", static_cast<int>(Placement::BottomLeft));
  placementBox->addItem("bottomRight", static_cast<int>(Placement::BottomRight));
  placementBox->addItem("left", static_cast<int>(Placement::Left));
  placementBox->addItem("leftTop", static_cast<int>(Placement::LeftTop));
  placementBox->addItem("leftBottom", static_cast<int>(Placement::LeftBottom));
  placementBox->addItem("right", static_cast<int>(Placement::Right));
  placementBox->addItem("rightTop", static_cast<int>(Placement::RightTop));
  placementBox->addItem("rightBottom", static_cast<int>(Placement::RightBottom));
  placementBox->setCurrentIndex(0);

  AdPopover* popover = nullptr;
  QWidget* trigger = makePopover("Open popover", "Placement", "Use combo box to change placement.",
                                 Trigger::Click, nullptr, &popover);
  popover->setAutoAdjustOverflow(true);
  popover->setVisible(true);

  connect(placementBox, QOverload<int>::of(&QComboBox::currentIndexChanged), popover,
          [placementBox, popover](int) {
            popover->setPlacement(static_cast<Placement>(placementBox->currentData().toInt()));
          });

  layout->addWidget(placementBox, 0, Qt::AlignLeft);
  layout->addWidget(trigger, 0, Qt::AlignLeft);
  layout->addWidget(
      makeHintLabel("When not enough space, Popover falls back to opposite placement."));
  return box;
}

QWidget* PopoverDocsPage::buildArrowDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* showArrow = new QCheckBox("Show Arrow");
  showArrow->setChecked(true);
  auto* pointAtCenter = new QCheckBox("Point At Center");

  auto* row = new QHBoxLayout();
  row->addWidget(showArrow);
  row->addWidget(pointAtCenter);
  row->addStretch();

  AdPopover* popover = nullptr;
  QWidget* trigger =
      makePopover("Arrow demo", "Arrow", "Toggle arrow visibility and center alignment.",
                  Trigger::Click, nullptr, &popover);
  popover->setVisible(true);

  connect(showArrow, &QCheckBox::toggled, popover, &AdPopover::setArrowVisible);
  connect(pointAtCenter, &QCheckBox::toggled, popover, &AdPopover::setArrowPointAtCenter);

  layout->addLayout(row);
  layout->addWidget(trigger, 0, Qt::AlignLeft);
  return box;
}

QWidget* PopoverDocsPage::buildAutoShiftDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* frame = new QFrame();
  frame->setFrameShape(QFrame::StyledPanel);
  frame->setFixedSize(320, 140);
  auto* frameLayout = new QHBoxLayout(frame);
  frameLayout->setContentsMargins(8, 8, 8, 8);

  AdPopover* popover = nullptr;
  QWidget* trigger = makePopover("Near top-left", "Auto Adjust",
                                 "Try resizing window: popover keeps visible area.", Trigger::Click,
                                 frame, &popover);
  popover->setPlacement(Placement::TopLeft);
  popover->setVisible(true);
  popover->setAutoAdjustOverflow(true);
  frameLayout->addWidget(trigger, 0, Qt::AlignTop | Qt::AlignLeft);
  frameLayout->addStretch();

  layout->addWidget(frame);
  layout->addWidget(
      makeHintLabel("This mirrors antd auto-shift behavior on narrow or edge viewport."));
  return box;
}

QWidget* PopoverDocsPage::buildPopupLayerModeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* modeBox = new QComboBox();
  modeBox->addItem(QStringLiteral("InWindow"),
                   static_cast<int>(AdPopover::PopupLayerMode::InWindow));
  modeBox->addItem(QStringLiteral("QtTool"), static_cast<int>(AdPopover::PopupLayerMode::QtTool));
  modeBox->setCurrentIndex(1);

  auto* stage = new QFrame();
  stage->setFrameShape(QFrame::StyledPanel);
  stage->setMinimumSize(360, 150);
  auto* stageLayout = new QHBoxLayout(stage);
  stageLayout->setContentsMargins(12, 12, 12, 12);

  AdPopover* popover = nullptr;
  QWidget* trigger =
      makePopover("Open layer", "Popup Layer", "QtTool uses interactive top-level positioning.",
                  Trigger::Click, stage, &popover);
  popover->setPlacement(Placement::BottomRight);
  popover->setPopupLayerMode(AdPopover::PopupLayerMode::QtTool);
  stageLayout->addStretch();
  stageLayout->addWidget(trigger, 0, Qt::AlignRight | Qt::AlignBottom);

  connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), popover,
          [modeBox, popover](int) {
            popover->setPopupLayerMode(
                static_cast<AdPopover::PopupLayerMode>(modeBox->currentData().toInt()));
          });

  layout->addWidget(modeBox, 0, Qt::AlignLeft);
  layout->addWidget(stage);
  return box;
}

QWidget* PopoverDocsPage::buildControlledDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* openCheck = new QCheckBox("Open");
  AdPopover* popover = nullptr;
  QWidget* trigger =
      makePopover("Controlled popover", "Controlled", "Open state is fully controlled by checkbox.",
                  Trigger::Click, nullptr, &popover);
  popover->setVisibilityPolicy(VisibilityPolicy::Manual);

  connect(openCheck, &QCheckBox::toggled, popover, &AdPopover::setVisible);
  connect(popover, &AdPopover::visibleChanged, openCheck, &QCheckBox::setChecked);
  connect(popover, &AdPopover::visibilityRequested, openCheck, &QCheckBox::setChecked);

  layout->addWidget(openCheck, 0, Qt::AlignLeft);
  layout->addWidget(trigger, 0, Qt::AlignLeft);
  return box;
}

QWidget* PopoverDocsPage::buildHoverWithClickDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  AdPopover* outer = nullptr;
  QWidget* outerTrigger =
      makePopover("Hover area", "Hover title", "Hover content", Trigger::Hover, nullptr, &outer);

  auto* clickInsideHost = new QWidget();
  auto* clickInsideLayout = new QHBoxLayout(clickInsideHost);
  clickInsideLayout->setContentsMargins(0, 0, 0, 0);

  QWidget* innerTrigger = makePopover("Click inside", "Click title", "Nested click popover content",
                                      Trigger::Click, clickInsideHost);
  clickInsideLayout->addWidget(innerTrigger);
  clickInsideLayout->addStretch();
  outer->setContentWidget(clickInsideHost);

  row->addWidget(outerTrigger);
  row->addWidget(
      makeHintLabel("Outer uses hover, inner uses click. Both can keep popup interactions alive."));
  row->addStretch();
  return box;
}

QWidget* PopoverDocsPage::buildVisualStyleDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  AdPopover* fixed = nullptr;
  QWidget* fixedTrigger =
      makePopover("Fixed style", "Visual", "Direct colors and fonts from Qt properties.",
                  Trigger::Click, nullptr, &fixed);
  QFont fixedTitleFont = fixedTrigger->font();
  fixedTitleFont.setBold(true);
  fixedTitleFont.setPointSize(fixedTitleFont.pointSize() + 1);
  fixed->setBackgroundColor(QColor("#f6ffed"));
  fixed->setBorderColor(QColor("#b7eb8f"));
  fixed->setTitleColor(QColor("#389e0d"));
  fixed->setTextColor(QColor("#237804"));
  fixed->setTitleFont(fixedTitleFont);

  AdPopover* dynamic = nullptr;
  QWidget* dynamicTrigger =
      makePopover("Dynamic style", "Dynamic", "Color palette flips when the popup opens or closes.",
                  Trigger::Click, nullptr, &dynamic);
  auto applyDynamicStyle = [dynamic](bool open) {
    if (!dynamic) {
      return;
    }
    if (open) {
      dynamic->setBackgroundColor(QColor("#fff7e6"));
      dynamic->setBorderColor(QColor("#ffd591"));
      dynamic->setTitleColor(QColor("#d46b08"));
      dynamic->setTextColor(QColor("#ad4e00"));
    } else {
      dynamic->setBackgroundColor(QColor("#e6f4ff"));
      dynamic->setBorderColor(QColor("#91caff"));
      dynamic->setTitleColor(QColor("#0958d9"));
      dynamic->setTextColor(QColor("#003eb3"));
    }
  };
  applyDynamicStyle(false);
  connect(dynamic, &AdPopover::visibleChanged, dynamic, applyDynamicStyle);

  row->addWidget(fixedTrigger);
  row->addWidget(dynamicTrigger);
  row->addStretch();
  return box;
}

QWidget* PopoverDocsPage::buildVisualPropertyDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  AdPopover* custom = nullptr;
  QWidget* customTrigger =
      makePopover("Custom metrics", "Visual", "Override width, padding, radius, arrow and offset.",
                  Trigger::Click, nullptr, &custom);
  custom->setTitleMinimumWidth(40);
  custom->setMaximumWidth(240);
  custom->setContentMargins(QMargins(10, 10, 10, 10));
  custom->setPopupOffset(6);
  custom->setCornerRadius(12);
  custom->setArrowSize(10);
  custom->setBackgroundColor(QColor("#fffbe6"));
  custom->setBorderColor(QColor("#ffe58f"));
  custom->setTitleColor(QColor("#d48806"));
  custom->setTextColor(QColor("#ad6800"));

  QWidget* defaults =
      makePopover("Default metrics", "Default", "Theme-driven visual properties.", Trigger::Click);

  row->addWidget(customTrigger);
  row->addWidget(defaults);
  row->addStretch();
  return box;
}

QWidget* PopoverDocsPage::buildApiOverview() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(16);
  grid->setVerticalSpacing(8);

  const QVector<QPair<QString, QString>> rows = {
      {"placement",
       "top/topLeft/topRight/bottom/bottomLeft/bottomRight/left/leftTop/leftBottom/right/rightTop/"
       "rightBottom"},
      {"triggers", "Hover | Focus | Click | ContextMenu (flags, combinable)"},
      {"visible", "controller-driven popup visibility"},
      {"visibilityPolicy", "Automatic or Manual visibility handling"},
      {"autoAdjustOverflow", "enable opposite placement fallback and edge clamping"},
      {"popupLayerMode", "InWindow or QtTool popup surface ownership"},
      {"arrowVisible / arrowPointAtCenter", "arrow visibility and center alignment behavior"},
      {"hoverOpenDelayMs / hoverCloseDelayMs", "hover open/close delay (ms)"},
      {"popupLifetime", "retain surface or recreate on open"},
      {"title / text", "basic text content"},
      {"setTitleWidget/setContentWidget", "custom widget content slots"},
      {"sourceWidget/anchorWidget", "separate trigger and anchor widgets"},
      {"titleFont/textFont/backgroundColor/borderColor",
       "Qt-style visual customization properties"},
      {"contentMargins/titleMinimumWidth/maximumWidth/cornerRadius",
       "layout and geometry customization"},
      {"signals", "visibleChanged(bool), visibilityRequested(bool)"},
  };

  for (int i = 0; i < rows.size(); ++i) {
    auto* name = new QLabel(rows.at(i).first);
    auto* desc = new QLabel(rows.at(i).second);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setWordWrap(true);
    QFont nameFont = name->font();
    nameFont.setBold(true);
    name->setFont(nameFont);
    grid->addWidget(name, i, 0, Qt::AlignTop);
    grid->addWidget(desc, i, 1);
  }

  return box;
}
