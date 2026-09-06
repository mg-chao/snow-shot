#include "popconfirm_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include "antd_icons.h"

using adqt::widgets::AdButton;
using adqt::widgets::AdPopconfirm;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {}  // namespace

PopconfirmDocsPage::PopconfirmDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Popconfirm");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "A compact confirmation popup for actions. This page ports the official antd Popconfirm "
      "examples.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Locale text", "Demo: locale.tsx", buildLocaleDemo());
  addSection(root, "Placement", "Demo: placement.tsx", buildPlacementDemo());
  addSection(root, "Auto Shift", "Demo: shift.tsx", buildAutoShiftDemo());
  addSection(root, "Conditional trigger", "Demo: dynamic-trigger.tsx", buildDynamicTriggerDemo());
  addSection(root, "Customize icon", "Demo: icon.tsx", buildIconDemo());
  addSection(root, "Asynchronously close", "Demo: async.tsx", buildAsyncDemo());
  addSection(root, "Asynchronously close on Promise", "Demo: promise.tsx", buildPromiseDemo());
  addSection(root, "Custom semantic dom styling", "Demo: style-class.tsx", buildStyleClassDemo());
  addSection(root, "_InternalPanelDoNotUseOrYouWillBeFired", "Demo: render-panel.tsx",
             buildRenderPanelDemo());
  addSection(root, "Wireframe", "Demo: wireframe.tsx", buildWireframeDemo());

  root->addStretch();
}

const QVector<QWidget*>& PopconfirmDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& PopconfirmDocsPage::sectionTitles() const { return titles_; }

void PopconfirmDocsPage::addSection(QVBoxLayout* root, const QString& title,
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

QWidget* PopconfirmDocsPage::makePopconfirm(const QString& triggerText, const QString& title,
                                            const QString& description, Triggers triggers,
                                            QWidget* parent, AdPopconfirm** popconfirmOut) {
  auto* trigger = new AdButton(triggerText, parent);
  trigger->setButtonStyle(AdButton::ButtonStyle::Outline);
  trigger->setAccentRole(AdButton::AccentRole::Neutral);
  auto* popconfirm = new AdPopconfirm(trigger);
  popconfirm->setSourceWidget(trigger);
  popconfirm->setText(title);
  popconfirm->setInformativeText(description);
  popconfirm->setTriggers(triggers);
  if (popconfirmOut) {
    *popconfirmOut = popconfirm;
  }
  return trigger;
}

QWidget* PopconfirmDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* status = new QLabel("Result: waiting for action.");
  AdPopconfirm* popconfirm = nullptr;
  QWidget* trigger =
      makePopconfirm("Delete", "Delete the task", "Are you sure to delete this task?",
                     Trigger::Click, nullptr, &popconfirm);
  popconfirm->setButtonText(StandardButton::Ok, "Yes");
  popconfirm->setButtonText(StandardButton::Cancel, "No");
  if (auto* button = qobject_cast<AdButton*>(trigger)) {
    button->setAccentRole(AdButton::AccentRole::Danger);
  }

  connect(popconfirm, &AdPopconfirm::accepted, status,
          [status]() { status->setText("Result: Click on Yes."); });
  connect(popconfirm, &AdPopconfirm::rejected, status,
          [status]() { status->setText("Result: Click on No."); });

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->addWidget(trigger);
  row->addWidget(status);
  row->addStretch();

  layout->addLayout(row);
  return box;
}

QWidget* PopconfirmDocsPage::buildLocaleDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  AdPopconfirm* locale = nullptr;
  QWidget* localeTrigger =
      makePopconfirm("Delete", "Delete the task", "Are you sure to delete this task?",
                     Trigger::Click, nullptr, &locale);
  locale->setButtonText(StandardButton::Ok, "Yes");
  locale->setButtonText(StandardButton::Cancel, "No");
  if (auto* trigger = qobject_cast<AdButton*>(localeTrigger)) {
    trigger->setAccentRole(AdButton::AccentRole::Danger);
  }

  row->addWidget(localeTrigger);
  row->addWidget(makeHintLabel("Use `setButtonText` to customize locale text."));
  row->addStretch();
  return box;
}

QWidget* PopconfirmDocsPage::buildPlacementDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(8);
  const QString title = "Are you sure to delete this task?";
  const QString description = "Delete the task";
  constexpr int buttonWidth = 80;

  auto addPlacement = [&](int row, int column, const QString& text, Placement placement) {
    AdPopconfirm* popconfirm = nullptr;
    QWidget* trigger =
        makePopconfirm(text, title, description, Trigger::Click, nullptr, &popconfirm);
    popconfirm->setPlacement(placement);
    popconfirm->setButtonText(StandardButton::Ok, "Yes");
    popconfirm->setButtonText(StandardButton::Cancel, "No");
    if (auto* button = qobject_cast<AdButton*>(trigger)) {
      button->setFixedWidth(buttonWidth);
    }
    grid->addWidget(trigger, row, column);
  };

  addPlacement(0, 1, "TL", Placement::TopLeft);
  addPlacement(0, 2, "Top", Placement::Top);
  addPlacement(0, 3, "TR", Placement::TopRight);

  addPlacement(1, 0, "LT", Placement::LeftTop);
  addPlacement(2, 0, "Left", Placement::Left);
  addPlacement(3, 0, "LB", Placement::LeftBottom);

  addPlacement(1, 4, "RT", Placement::RightTop);
  addPlacement(2, 4, "Right", Placement::Right);
  addPlacement(3, 4, "RB", Placement::RightBottom);

  addPlacement(4, 1, "BL", Placement::BottomLeft);
  addPlacement(4, 2, "Bottom", Placement::Bottom);
  addPlacement(4, 3, "BR", Placement::BottomRight);

  return box;
}

QWidget* PopconfirmDocsPage::buildAutoShiftDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* frame = new QFrame();
  frame->setFrameShape(QFrame::StyledPanel);
  frame->setFixedSize(360, 160);
  auto* frameLayout = new QHBoxLayout(frame);
  frameLayout->setContentsMargins(8, 8, 8, 8);
  frameLayout->setSpacing(0);

  AdPopconfirm* popconfirm = nullptr;
  QWidget* trigger = makePopconfirm("Scroll The Window", "Thanks for using antd. Have a nice day !",
                                    "Popconfirm auto shifts near viewport edges.", Trigger::Click,
                                    frame, &popconfirm);
  if (auto* button = qobject_cast<AdButton*>(trigger)) {
    button->setButtonStyle(AdButton::ButtonStyle::Solid);
    button->setAccentRole(AdButton::AccentRole::Primary);
  }
  popconfirm->setPlacement(Placement::Top);
  popconfirm->setAutoAdjustOverflow(true);
  popconfirm->setVisible(true);

  frameLayout->addWidget(trigger, 0, Qt::AlignTop | Qt::AlignLeft);
  frameLayout->addStretch();

  layout->addWidget(frame);
  layout->addWidget(
      makeHintLabel("Near the edge, popup position and arrow auto-adjust to keep visible area."));
  return box;
}

QWidget* PopconfirmDocsPage::buildDynamicTriggerDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* conditionCheck = new QCheckBox("Whether directly execute");
  conditionCheck->setChecked(true);
  auto* status = new QLabel("Result: waiting for action.");

  AdPopconfirm* popconfirm = nullptr;
  QWidget* triggerWidget =
      makePopconfirm("Delete a task", "Delete the task", "Are you sure to delete this task?",
                     Trigger::Click, nullptr, &popconfirm);
  if (auto* trigger = qobject_cast<AdButton*>(triggerWidget)) {
    trigger->setAccentRole(AdButton::AccentRole::Danger);
  }
  popconfirm->setVisibilityMode(VisibilityMode::Manual);
  popconfirm->setButtonText(StandardButton::Ok, "Yes");
  popconfirm->setButtonText(StandardButton::Cancel, "No");

  connect(popconfirm, &AdPopconfirm::visibilityRequested, this,
          [popconfirm, conditionCheck, status](bool nextOpen) {
            if (!nextOpen) {
              popconfirm->setVisible(false);
              return;
            }
            if (conditionCheck->isChecked()) {
              status->setText("Result: Next step.");
              popconfirm->setVisible(false);
              return;
            }
            popconfirm->setVisible(true);
          });

  connect(popconfirm, &AdPopconfirm::accepted, status,
          [status]() { status->setText("Result: Next step."); });
  connect(popconfirm, &AdPopconfirm::rejected, status,
          [status]() { status->setText("Result: Click on cancel."); });

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);
  row->addWidget(triggerWidget);
  row->addWidget(conditionCheck);
  row->addStretch();

  layout->addLayout(row);
  layout->addWidget(status);
  return box;
}

QWidget* PopconfirmDocsPage::buildIconDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  QWidget* defaultIcon =
      makePopconfirm("Default icon", "Delete the task", "Are you sure to delete this task?");

  AdPopconfirm* customIcon = nullptr;
  QWidget* customIconTrigger =
      makePopconfirm("Custom icon", "Delete the task", "Are you sure to delete this task?",
                     Trigger::Click, nullptr, &customIcon);
  customIcon->setCustomIconRef(
      outlined_icons::QuestionCircle(adqt::icons::IconColors::primary(QColor("#ff4d4f"))));

  row->addWidget(defaultIcon);
  row->addWidget(customIconTrigger);
  row->addStretch();
  return box;
}

QWidget* PopconfirmDocsPage::buildAsyncDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* status = new QLabel("Result: waiting for action.");
  AdPopconfirm* popconfirm = nullptr;
  QWidget* trigger =
      makePopconfirm("Open Popconfirm with async logic", "Title",
                     "Open Popconfirm with async logic", Trigger::Click, nullptr, &popconfirm);
  if (auto* button = qobject_cast<AdButton*>(trigger)) {
    button->setButtonStyle(AdButton::ButtonStyle::Solid);
    button->setAccentRole(AdButton::AccentRole::Primary);
  }
  popconfirm->setVisibilityMode(VisibilityMode::Manual);
  popconfirm->setAutoCloseButtons(StandardButton::Cancel);

  connect(popconfirm, &AdPopconfirm::visibilityRequested, this,
          [popconfirm](bool nextOpen) { popconfirm->setVisible(nextOpen); });
  connect(popconfirm, &AdPopconfirm::accepted, this, [popconfirm, status]() {
    status->setText("Result: confirming...");
    popconfirm->setButtonBusy(StandardButton::Ok, true);
    QTimer::singleShot(2000, popconfirm, [popconfirm, status]() {
      popconfirm->setButtonBusy(StandardButton::Ok, false);
      popconfirm->setVisible(false);
      status->setText("Result: confirmed after async logic.");
    });
  });
  connect(popconfirm, &AdPopconfirm::rejected, this, [popconfirm, status]() {
    popconfirm->setButtonBusy(StandardButton::Ok, false);
    popconfirm->setVisible(false);
    status->setText("Result: canceled.");
  });

  layout->addWidget(trigger, 0, Qt::AlignLeft);
  layout->addWidget(status);
  return box;
}

QWidget* PopconfirmDocsPage::buildPromiseDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* status = new QLabel("Result: open change log.");
  AdPopconfirm* popconfirm = nullptr;
  QWidget* trigger =
      makePopconfirm("Open Popconfirm with Promise", "Title", "Open Popconfirm with Promise",
                     Trigger::Click, nullptr, &popconfirm);
  if (auto* button = qobject_cast<AdButton*>(trigger)) {
    button->setButtonStyle(AdButton::ButtonStyle::Solid);
    button->setAccentRole(AdButton::AccentRole::Primary);
  }
  popconfirm->setVisibilityMode(VisibilityMode::Manual);
  popconfirm->setAutoCloseButtons(StandardButton::Cancel);

  connect(popconfirm, &AdPopconfirm::visibilityRequested, this,
          [popconfirm, status](bool nextOpen) {
            status->setText(
                QStringLiteral("Result: open request -> %1").arg(nextOpen ? "true" : "false"));
            popconfirm->setVisible(nextOpen);
          });
  connect(popconfirm, &AdPopconfirm::accepted, this, [popconfirm, status]() {
    status->setText("Result: promise pending...");
    popconfirm->setButtonBusy(StandardButton::Ok, true);
    QTimer::singleShot(3000, popconfirm, [popconfirm, status]() {
      popconfirm->setButtonBusy(StandardButton::Ok, false);
      popconfirm->setVisible(false);
      status->setText("Result: promise resolved, popup closed.");
    });
  });

  layout->addWidget(trigger, 0, Qt::AlignLeft);
  layout->addWidget(status);
  return box;
}

QWidget* PopconfirmDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  AdPopconfirm* objectStyle = nullptr;
  QWidget* objectStyleTrigger = makePopconfirm("Object Style", "Object text", "Object description",
                                               Trigger::Click, nullptr, &objectStyle);
  objectStyle->setArrowVisible(false);
  AdPopconfirm::SemanticStyles objectSemantic;
  objectSemantic.container.backgroundColor = QColor(53, 71, 125, 204);
  objectSemantic.title.textColor = QColor("#ffffff");
  objectSemantic.description.textColor = QColor("#ffffff");
  objectSemantic.icon.textColor = QColor("#ffffff");
  objectStyle->setSemanticStyles(objectSemantic);

  AdPopconfirm* functionStyle = nullptr;
  QWidget* functionStyleTrigger =
      makePopconfirm("Function Style", "Function text", "Function description", Trigger::Click,
                     nullptr, &functionStyle);
  functionStyle->setArrowVisible(false);
  functionStyle->setSemanticStyleResolver([](const AdPopconfirm::StyleContext& ctx) {
    AdPopconfirm::SemanticStyles styles;
    if (ctx.visible) {
      styles.container.backgroundColor = QColor("#fffbe6");
      styles.title.textColor = QColor("#ad6800");
      styles.description.textColor = QColor("#ad6800");
      styles.icon.textColor = QColor("#d48806");
    } else {
      styles.container.backgroundColor = QColor(53, 71, 125, 204);
      styles.title.textColor = QColor("#ffffff");
      styles.description.textColor = QColor("#ffffff");
      styles.icon.textColor = QColor("#ffffff");
    }
    return styles;
  });

  row->addWidget(objectStyleTrigger);
  row->addWidget(functionStyleTrigger);
  row->addStretch();
  return box;
}

QWidget* PopconfirmDocsPage::buildRenderPanelDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  AdPopconfirm* panelA = nullptr;
  QWidget* panelATrigger = makePopconfirm("Panel A", "Are you OK?", "Does this look good?",
                                          Trigger::Click, nullptr, &panelA);
  panelA->setVisible(true);

  AdPopconfirm* panelB = nullptr;
  QWidget* panelBTrigger = makePopconfirm("Panel B", "Are you OK?", "Does this look good?",
                                          Trigger::Click, nullptr, &panelB);
  panelB->setPlacement(Placement::BottomRight);
  panelB->setVisible(true);

  AdPopconfirm* panelC = nullptr;
  QWidget* panelCTrigger =
      makePopconfirm("Panel C", "Are you OK?", QString(), Trigger::Click, nullptr, &panelC);
  panelC->setIcon(Icon::NoIcon);
  panelC->setVisible(true);

  AdPopconfirm* panelD = nullptr;
  QWidget* panelDTrigger = makePopconfirm("Panel D", "Are you OK?", "Does this look good?",
                                          Trigger::Click, nullptr, &panelD);
  panelD->setIcon(Icon::NoIcon);
  panelD->setVisible(true);

  row->addWidget(panelATrigger);
  row->addWidget(panelBTrigger);
  row->addWidget(panelCTrigger);
  row->addWidget(panelDTrigger);
  row->addStretch();

  layout->addLayout(row);
  layout->addWidget(makeHintLabel(
      "Equivalent of internal panel showcase using always-open popconfirm instances."));
  return box;
}

QWidget* PopconfirmDocsPage::buildWireframeDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  AdPopconfirm* panelA = nullptr;
  QWidget* panelATrigger =
      makePopconfirm("Wireframe A", "Are you OK?", "Wireframe-like semantic style.", Trigger::Click,
                     nullptr, &panelA);
  demo::bindThemeRefresh(panelATrigger, [panelA, panelATrigger]() {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(panelATrigger);
    AdPopconfirm::SemanticStyles wireframeStyles;
    wireframeStyles.container.backgroundColor =
        demo::themeColorOr(map.colorBgElevated, QColor("#ffffff"));
    wireframeStyles.container.borderColor = demo::themeColorOr(map.colorBorder, QColor("#d9d9d9"));
    wireframeStyles.title.textColor = demo::themeColorOr(map.colorText, QColor("#262626"));
    wireframeStyles.description.textColor =
        demo::themeColorOr(map.colorTextSecondary, QColor("#595959"));
    wireframeStyles.arrow.backgroundColor =
        demo::themeColorOr(map.colorBgElevated, QColor("#ffffff"));
    panelA->setSemanticStyles(wireframeStyles);
  });
  panelA->setVisible(true);

  AdPopconfirm* panelB = nullptr;
  QWidget* panelBTrigger =
      makePopconfirm("Wireframe B", "Are you OK?", "Bottom-right wireframe style.", Trigger::Click,
                     nullptr, &panelB);
  demo::bindThemeRefresh(panelBTrigger, [panelB, panelBTrigger]() {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(panelBTrigger);
    AdPopconfirm::SemanticStyles wireframeStyles;
    wireframeStyles.container.backgroundColor =
        demo::themeColorOr(map.colorBgElevated, QColor("#ffffff"));
    wireframeStyles.container.borderColor = demo::themeColorOr(map.colorBorder, QColor("#d9d9d9"));
    wireframeStyles.title.textColor = demo::themeColorOr(map.colorText, QColor("#262626"));
    wireframeStyles.description.textColor =
        demo::themeColorOr(map.colorTextSecondary, QColor("#595959"));
    wireframeStyles.arrow.backgroundColor =
        demo::themeColorOr(map.colorBgElevated, QColor("#ffffff"));
    panelB->setSemanticStyles(wireframeStyles);
  });
  panelB->setPlacement(Placement::BottomRight);
  panelB->setVisible(true);

  row->addWidget(panelATrigger);
  row->addWidget(panelBTrigger);
  row->addStretch();
  return box;
}
