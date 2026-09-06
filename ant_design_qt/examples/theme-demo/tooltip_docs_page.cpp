#include "tooltip_docs_page.h"

#include "demo_theme_utils.h"

#include "theme/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using adqt::widgets::AdButton;
using adqt::widgets::AdTooltip;

namespace {

QWidget* wrapDisabledWidget(QWidget* child) {
  auto* host = new QWidget();
  host->setAttribute(Qt::WA_Hover, true);
  host->setMouseTracking(true);
  auto* layout = new QVBoxLayout(host);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(child);
  return host;
}

QColor resolvePresetTooltipColor(const QString& name) {
  const auto& accents = adqt::theme::ThemeManager::instance().theme().accents;
  const QString key = name.trimmed().toLower();
  if (key == QStringLiteral("pink")) {
    return QColor(accents.pink);
  }
  if (key == QStringLiteral("red")) {
    return QColor(accents.red);
  }
  if (key == QStringLiteral("yellow")) {
    return QColor(accents.yellow);
  }
  if (key == QStringLiteral("orange")) {
    return QColor(accents.orange);
  }
  if (key == QStringLiteral("cyan")) {
    return QColor(accents.cyan);
  }
  if (key == QStringLiteral("green")) {
    return QColor(accents.green);
  }
  if (key == QStringLiteral("blue")) {
    return QColor(accents.blue);
  }
  if (key == QStringLiteral("purple")) {
    return QColor(accents.purple);
  }
  if (key == QStringLiteral("geekblue")) {
    return QColor(accents.geekblue);
  }
  if (key == QStringLiteral("magenta")) {
    return QColor(accents.magenta);
  }
  if (key == QStringLiteral("volcano")) {
    return QColor(accents.volcano);
  }
  if (key == QStringLiteral("gold")) {
    return QColor(accents.gold);
  }
  if (key == QStringLiteral("lime")) {
    return QColor(accents.lime);
  }
  return QColor(name);
}

void setTooltipColors(AdTooltip* tooltip, const QColor& background, const QColor& text = QColor()) {
  if (!tooltip) {
    return;
  }

  AdTooltip::ComponentTokens tokens = tooltip->componentTokens();
  tokens.popupBg = background;
  if (text.isValid()) {
    tokens.textColor = text;
  } else {
    tokens.textColor.reset();
  }
  tooltip->setComponentTokens(tokens);
}

class EventAwareWidget final : public QWidget {
 public:
  explicit EventAwareWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(0);
    auto* label = new QLabel("This text is inside a custom event-aware component.");
    label->setWordWrap(true);
    layout->addWidget(label);
  }
};

}  // namespace

TooltipDocsPage::TooltipDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Tooltip");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "Simple text popup box. The tip appears on hover/focus/click/context menu and is typically "
      "used "
      "to explain an element.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Smooth Transition", "Demo: smooth-transition.tsx (behavior only, no animation)",
             buildSmoothTransitionDemo());
  addSection(root, "Placement", "Demo: placement.tsx", buildPlacementDemo());
  addSection(root, "Arrow", "Demo: arrow.tsx + arrow-point-at-center.tsx", buildArrowDemo());
  addSection(root, "Auto Shift", "Demo: shift.tsx", buildShiftDemo());
  addSection(root, "Adjust placement automatically", "Demo: auto-adjust-overflow.tsx",
             buildAutoAdjustOverflowDemo());
  addSection(root, "Destroy tooltip when hidden", "Demo: destroy-on-close.tsx",
             buildDestroyOnHiddenDemo());
  addSection(root, "Colorful Tooltip", "Demo: colorful.tsx", buildColorfulDemo());
  addSection(root, "Disabled", "Demo: disabled.tsx", buildDisabledDemo());
  addSection(root, "Disabled children", "Demo: disabled-children.tsx", buildDisabledChildrenDemo());
  addSection(root, "Wrap custom component", "Demo: wrap-custom-component.tsx",
             buildWrapCustomComponentDemo());
  addSection(root, "Custom semantic dom styling", "Demo: style-class.tsx", buildStyleClassDemo());

  root->addStretch();
}

const QVector<QWidget*>& TooltipDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& TooltipDocsPage::sectionTitles() const { return titles_; }

void TooltipDocsPage::addSection(QVBoxLayout* root, const QString& title,
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

QWidget* TooltipDocsPage::makeTooltip(const QString& triggerText, const QString& title,
                                      Triggers triggers, QWidget* parent, AdTooltip** tooltipOut) {
  auto* trigger = new AdButton(triggerText, parent);
  trigger->setButtonStyle(AdButton::ButtonStyle::Outline);
  trigger->setAccentRole(AdButton::AccentRole::Neutral);
  auto* tooltip = new AdTooltip(trigger);
  tooltip->setTargetWidget(trigger);
  tooltip->setText(title);
  tooltip->setTriggers(triggers);
  if (tooltipOut) {
    *tooltipOut = tooltip;
  }
  return trigger;
}

QWidget* TooltipDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  row->addWidget(makeTooltip("Hover me", "prompt text", Trigger::Hover));
  row->addWidget(new QLabel("Tooltip will show on mouse enter."));
  row->addStretch();
  return box;
}

QWidget* TooltipDocsPage::buildSmoothTransitionDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* row1 = new QHBoxLayout();
  row1->setSpacing(8);
  auto* row2 = new QHBoxLayout();
  row2->setSpacing(8);

  auto* t1 = makeTooltip("Button 1", "First tooltip");
  auto* t2 = makeTooltip("Button 2", "Second tooltip");
  auto* t3 = makeTooltip("Button 3", "Third tooltip");
  auto* t4 = makeTooltip("Button 4", "Fourth tooltip");

  row1->addWidget(t1);
  row1->addWidget(t2);
  row1->addStretch();
  row2->addWidget(t3);
  row2->addWidget(t4);
  row2->addStretch();

  layout->addLayout(row1);
  layout->addLayout(row2);
  layout->addWidget(
      makeHintLabel("No animation in this Qt demo. Behavior matches unique display: only one "
                    "tooltip is visible in the same window scope."));
  return box;
}

QWidget* TooltipDocsPage::buildPlacementDemo() {
  auto* box = new QWidget();
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(8);

  auto addPlacement = [&](int row, int column, const QString& text, Placement placement) {
    AdTooltip* tooltip = nullptr;
    QWidget* trigger = makeTooltip(text, "prompt text", Trigger::Hover, nullptr, &tooltip);
    tooltip->setPlacement(placement);
    if (auto* button = qobject_cast<AdButton*>(trigger)) {
      button->setFixedWidth(78);
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

QWidget* TooltipDocsPage::buildArrowDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* modeBox = new QComboBox();
  modeBox->addItem("Show");
  modeBox->addItem("Hide");
  modeBox->addItem("Center");

  auto* row = new QHBoxLayout();
  row->setSpacing(8);
  AdTooltip* leftTooltip = nullptr;
  QWidget* left = makeTooltip("TL", "prompt text", Trigger::Hover, nullptr, &leftTooltip);
  leftTooltip->setPlacement(Placement::TopLeft);
  AdTooltip* middleTooltip = nullptr;
  QWidget* middle = makeTooltip("Top", "prompt text", Trigger::Hover, nullptr, &middleTooltip);
  middleTooltip->setPlacement(Placement::Top);
  AdTooltip* rightTooltip = nullptr;
  QWidget* right = makeTooltip("TR", "prompt text", Trigger::Hover, nullptr, &rightTooltip);
  rightTooltip->setPlacement(Placement::TopRight);
  row->addWidget(left);
  row->addWidget(middle);
  row->addWidget(right);
  row->addStretch();

  const QList<AdTooltip*> samples = {leftTooltip, middleTooltip, rightTooltip};
  auto applyMode = [samples](const QString& mode) {
    const bool visible = mode != QStringLiteral("Hide");
    const bool center = mode == QStringLiteral("Center");
    for (AdTooltip* tooltip : samples) {
      if (!tooltip) {
        continue;
      }
      tooltip->setArrowVisible(visible);
      tooltip->setArrowPointAtCenter(center);
    }
  };

  connect(modeBox, &QComboBox::currentTextChanged, this, applyMode);
  applyMode(modeBox->currentText());

  layout->addWidget(modeBox, 0, Qt::AlignLeft);
  layout->addLayout(row);
  return box;
}

QWidget* TooltipDocsPage::buildShiftDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* frame = new QFrame();
  frame->setFrameShape(QFrame::StyledPanel);
  frame->setFixedSize(320, 140);
  auto* frameLayout = new QHBoxLayout(frame);
  frameLayout->setContentsMargins(8, 8, 8, 8);
  frameLayout->setSpacing(0);

  AdTooltip* tooltip = nullptr;
  QWidget* trigger = makeTooltip("Near top-left", "Thanks for using antd. Have a nice day !",
                                 Trigger::Click, frame, &tooltip);
  tooltip->setPlacement(Placement::Top);
  tooltip->setVisible(true);
  tooltip->setAutoAdjustOverflow(true);

  frameLayout->addWidget(trigger, 0, Qt::AlignLeft | Qt::AlignTop);
  frameLayout->addStretch();

  layout->addWidget(frame);
  layout->addWidget(
      makeHintLabel("When close to viewport edge, popup and arrow will auto shift for "
                    "top/bottom/left/right placements."));
  return box;
}

QWidget* TooltipDocsPage::buildAutoAdjustOverflowDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* autoAdjust = new QCheckBox("autoAdjustOverflow");
  autoAdjust->setChecked(true);

  AdTooltip* tooltip = nullptr;
  QWidget* trigger =
      makeTooltip("Placement: left", "Prompt Text", Trigger::Click, nullptr, &tooltip);
  tooltip->setPlacement(Placement::Left);
  tooltip->setVisible(true);
  tooltip->setAutoAdjustOverflow(true);

  connect(autoAdjust, &QCheckBox::toggled, tooltip, &AdTooltip::setAutoAdjustOverflow);

  layout->addWidget(autoAdjust, 0, Qt::AlignLeft);
  layout->addWidget(trigger, 0, Qt::AlignLeft);
  layout->addWidget(
      makeHintLabel("Toggle this option to compare automatic flip/clamp behavior with "
                    "overflow-allowed behavior."));
  return box;
}

QWidget* TooltipDocsPage::buildDestroyOnHiddenDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  AdTooltip* tooltip = nullptr;
  QWidget* trigger = makeTooltip("Dom will destroyed when Tooltip close", "prompt text",
                                 Trigger::Hover, nullptr, &tooltip);
  tooltip->setPopupLifetime(AdTooltip::PopupLifetime::RecreateOnOpen);

  row->addWidget(trigger);
  row->addStretch();
  return box;
}

QWidget* TooltipDocsPage::buildColorfulDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  const QStringList presetColors = {
      "pink",   "red",      "yellow",  "orange",  "cyan", "green", "blue",
      "purple", "geekblue", "magenta", "volcano", "gold", "lime",
  };
  const QStringList customColors = {"#f50", "#2db7f5", "#87d068", "#108ee9"};

  auto* presets = new QGridLayout();
  presets->setHorizontalSpacing(8);
  presets->setVerticalSpacing(8);
  for (int i = 0; i < presetColors.size(); ++i) {
    AdTooltip* tooltip = nullptr;
    QWidget* trigger =
        makeTooltip(presetColors.at(i), "prompt text", Trigger::Hover, nullptr, &tooltip);
    setTooltipColors(tooltip, resolvePresetTooltipColor(presetColors.at(i)));
    presets->addWidget(trigger, i / 5, i % 5);
  }

  auto* customs = new QHBoxLayout();
  customs->setSpacing(8);
  for (const QString& color : customColors) {
    AdTooltip* tooltip = nullptr;
    QWidget* trigger = makeTooltip(color, "prompt text", Trigger::Hover, nullptr, &tooltip);
    setTooltipColors(tooltip, QColor(color));
    customs->addWidget(trigger);
  }
  customs->addStretch();

  layout->addWidget(new QLabel("Presets"));
  layout->addLayout(presets);
  layout->addWidget(new QLabel("Custom"));
  layout->addLayout(customs);
  return box;
}

QWidget* TooltipDocsPage::buildDisabledDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* trigger = new AdButton("Enable", box);
  auto* tooltip = new AdTooltip(trigger);
  tooltip->setTargetWidget(trigger);
  tooltip->setText(QString());

  tooltip->setProperty("tooltipDisabledState", true);
  auto updateState = [tooltip, trigger]() {
    const bool disabled = tooltip->property("tooltipDisabledState").toBool();
    if (disabled) {
      tooltip->setText(QString());
      trigger->setText("Enable");
    } else {
      tooltip->setText("prompt text");
      trigger->setText("Disable");
    }
  };

  connect(trigger, &QAbstractButton::clicked, this, [tooltip, updateState]() {
    const bool disabled = tooltip->property("tooltipDisabledState").toBool();
    tooltip->setProperty("tooltipDisabledState", !disabled);
    updateState();
  });

  updateState();
  row->addWidget(trigger);
  row->addStretch();
  return box;
}

QWidget* TooltipDocsPage::buildDisabledChildrenDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto addWrappedTooltip = [row](QWidget* disabledWidget) {
    auto* wrapper = wrapDisabledWidget(disabledWidget);
    auto* tooltip = new AdTooltip(wrapper);
    tooltip->setTargetWidget(wrapper);
    tooltip->setAnchorWidget(wrapper);
    tooltip->setText("Thanks for using antd. Have a nice day !");
    row->addWidget(wrapper);
  };

  auto* disabledButton = new AdButton("Disabled");
  disabledButton->setEnabled(false);
  addWrappedTooltip(disabledButton);

  auto* disabledInput = new QLineEdit();
  disabledInput->setPlaceholderText("disabled");
  disabledInput->setEnabled(false);
  addWrappedTooltip(disabledInput);

  auto* disabledCheck = new QCheckBox("Checkbox");
  disabledCheck->setEnabled(false);
  addWrappedTooltip(disabledCheck);

  auto* disabledCombo = new QComboBox();
  disabledCombo->addItems({"One", "Two", "Three"});
  disabledCombo->setEnabled(false);
  addWrappedTooltip(disabledCombo);

  row->addStretch();
  return box;
}

QWidget* TooltipDocsPage::buildWrapCustomComponentDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* target = new EventAwareWidget(box);
  auto* tooltip = new AdTooltip(target);
  tooltip->setTargetWidget(target);
  tooltip->setText("prompt text");

  row->addWidget(target);
  row->addStretch();
  return box;
}

QWidget* TooltipDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  AdTooltip* objectStyledTooltip = nullptr;
  QWidget* objectStyled =
      makeTooltip("Object Style", "Object text", Trigger::Click, nullptr, &objectStyledTooltip);
  objectStyledTooltip->setArrowVisible(false);
  AdTooltip::SemanticStyles objectStyles;
  objectStyles.surface.backgroundColor = QColor(53, 71, 125, 204);
  objectStyles.content.textColor = QColor("#ffffff");
  objectStyledTooltip->setSemanticStyles(objectStyles);

  AdTooltip* resolverStyledTooltip = nullptr;
  QWidget* resolverStyled = makeTooltip("Function Style", "Function text", Trigger::Click, nullptr,
                                        &resolverStyledTooltip);
  resolverStyledTooltip->setArrowVisible(false);
  resolverStyledTooltip->setSemanticStyleResolver([](const AdTooltip::StyleContext& context) {
    AdTooltip::SemanticStyles styles;
    if (context.visible) {
      styles.surface.backgroundColor = QColor("#fffbe6");
      styles.content.textColor = QColor("#ad6800");
    } else {
      styles.surface.backgroundColor = QColor(53, 71, 125, 204);
      styles.content.textColor = QColor("#ffffff");
    }
    return styles;
  });

  row->addWidget(objectStyled);
  row->addWidget(resolverStyled);
  row->addStretch();
  return box;
}
