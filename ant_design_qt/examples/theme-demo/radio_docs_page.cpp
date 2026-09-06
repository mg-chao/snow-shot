#include "radio_docs_page.h"

#include "demo_theme_utils.h"

#include <QButtonGroup>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

using adqt::widgets::AdRadio;
using adqt::widgets::AdRadioButtonGroup;

namespace {

struct RadioOption {
  int id = -1;
  QVariant value;
  QString label;
  bool disabled = false;
  QString title;
};

RadioOption makeOption(int id, const QVariant& value, const QString& label, bool disabled = false,
                       const QString& title = QString()) {
  RadioOption option;
  option.id = id;
  option.value = value;
  option.label = label;
  option.disabled = disabled;
  option.title = title;
  return option;
}

QVector<RadioOption> cityOptions() {
  return {
      makeOption(1, QStringLiteral("a"), QStringLiteral("Hangzhou")),
      makeOption(2, QStringLiteral("b"), QStringLiteral("Shanghai")),
      makeOption(3, QStringLiteral("c"), QStringLiteral("Beijing")),
      makeOption(4, QStringLiteral("d"), QStringLiteral("Chengdu")),
  };
}

QVector<RadioOption> fruitOptions(bool disableOrange = false) {
  return {
      makeOption(1, QStringLiteral("Apple"), QStringLiteral("Apple")),
      makeOption(2, QStringLiteral("Pear"), QStringLiteral("Pear")),
      makeOption(3, QStringLiteral("Orange"), QStringLiteral("Orange"), disableOrange,
                 QStringLiteral("Orange")),
  };
}

struct ManagedRadioGroup {
  QWidget* container = nullptr;
  QBoxLayout* layout = nullptr;
  AdRadioButtonGroup* controller = nullptr;
};

ManagedRadioGroup createManagedRadioGroup(Qt::Orientation orientation = Qt::Horizontal) {
  ManagedRadioGroup managed;
  managed.container = new QWidget();
  managed.layout = orientation == Qt::Vertical
                       ? static_cast<QBoxLayout*>(new QVBoxLayout(managed.container))
                       : static_cast<QBoxLayout*>(new QHBoxLayout(managed.container));
  managed.layout->setContentsMargins(0, 0, 0, 0);
  managed.controller = new AdRadioButtonGroup(managed.container);
  managed.controller->setManagedLayout(managed.layout);
  return managed;
}

void populateGroup(const ManagedRadioGroup& group, const QVector<RadioOption>& options) {
  if (!group.container || !group.layout || !group.controller) {
    return;
  }

  for (const RadioOption& option : options) {
    auto* radio = new AdRadio(option.label, group.container);
    radio->setEnabled(!option.disabled);
    if (!option.title.isEmpty()) {
      radio->setToolTip(option.title);
    }
    group.layout->addWidget(radio);
    group.controller->addButton(radio, option.id);
  }
}

QString optionValueText(const QVector<RadioOption>& options, int id) {
  for (const RadioOption& option : options) {
    if (option.id == id) {
      return option.value.toString();
    }
  }
  return QString();
}

}  // namespace

RadioDocsPage::RadioDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Radio");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "Used to select a single state from multiple options. This page mirrors Ant Design "
      "Radio public demos.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Qt native grouping", "Qt-first adaptation: sibling radios are auto-exclusive.",
             buildQtNativeGroupingDemo());
  addSection(root, "disabled", "Demo: disabled.tsx", buildDisabledDemo());
  addSection(root, "Radio Group", "Demo: radiogroup.tsx", buildRadioGroupDemo());
  addSection(root, "Vertical Radio.Group", "Demo: radiogroup-more.tsx", buildVerticalGroupDemo());
  addSection(root, "Block Radio.Group", "Demo: radiogroup-block.tsx", buildBlockGroupDemo());
  addSection(root, "Radio.Group group - optional", "Demo: radiogroup-options.tsx",
             buildGroupOptionsDemo());
  addSection(root, "radio style", "Demo: radiobutton.tsx", buildRadioButtonDemo());
  addSection(root, "Size", "Demo: size.tsx", buildSizeDemo());
  addSection(root, "Solid radio button", "Demo: radiobutton-solid.tsx", buildSolidButtonDemo());
  addSection(root, "Custom semantic dom styling", "Demo: style-class.tsx", buildStyleClassDemo());

  root->addStretch();
}

const QVector<QWidget*>& RadioDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& RadioDocsPage::sectionTitles() const { return titles_; }

void RadioDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
                               QWidget* content) {
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

QWidget* RadioDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);

  auto* radio = new AdRadio("Radio");
  row->addWidget(radio);
  row->addStretch();
  return box;
}

QWidget* RadioDocsPage::buildQtNativeGroupingDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* first = new AdRadio("Alpha");
  auto* second = new AdRadio("Beta");
  auto* third = new AdRadio("Gamma");
  first->setChecked(true);

  row->addWidget(first);
  row->addWidget(second);
  row->addWidget(third);
  row->addStretch();

  layout->addLayout(row);
  layout->addWidget(
      makeHintLabel("These radios share the same parent widget, so Qt handles exclusivity without "
                    "a controller."));
  return box;
}

QWidget* RadioDocsPage::buildDisabledDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* first = new AdRadio("Disabled");
  first->setChecked(false);
  first->setDisabled(true);
  auto* second = new AdRadio("Disabled");
  second->setChecked(true);
  second->setDisabled(true);

  row->addWidget(first);
  row->addWidget(second);
  row->addStretch();

  auto* toggle = new QPushButton("Toggle disabled");
  connect(toggle, &QPushButton::clicked, this, [first, second]() {
    const bool next = first->isEnabled();
    first->setDisabled(next);
    second->setDisabled(next);
  });

  layout->addLayout(row);
  layout->addWidget(toggle, 0, Qt::AlignLeft);
  return box;
}

QWidget* RadioDocsPage::buildRadioGroupDemo() {
  const QVector<RadioOption> options = {
      makeOption(1, 1, QStringLiteral("LineChart")),
      makeOption(2, 2, QStringLiteral("DotChart")),
      makeOption(3, 3, QStringLiteral("BarChart")),
      makeOption(4, 4, QStringLiteral("PieChart")),
  };

  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  const ManagedRadioGroup group = createManagedRadioGroup();
  populateGroup(group, options);
  group.controller->setCheckedId(1);

  auto* output = makeHintLabel("selected: 1");
  connect(group.controller, &AdRadioButtonGroup::checkedIdChanged, output,
          [output, options](int id) {
            output->setText(QStringLiteral("selected: %1").arg(optionValueText(options, id)));
          });

  layout->addWidget(group.container, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* RadioDocsPage::buildVerticalGroupDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  const ManagedRadioGroup group = createManagedRadioGroup(Qt::Vertical);
  populateGroup(group, {
                           makeOption(1, 1, QStringLiteral("Option A")),
                           makeOption(2, 2, QStringLiteral("Option B")),
                           makeOption(3, 3, QStringLiteral("Option C")),
                           makeOption(4, 4, QStringLiteral("More...")),
                       });
  group.controller->setCheckedId(1);

  auto* moreInput = new QLineEdit();
  moreInput->setPlaceholderText("please input");
  moreInput->setVisible(false);
  moreInput->setFixedWidth(120);

  connect(group.controller, &AdRadioButtonGroup::checkedIdChanged, moreInput,
          [moreInput](int id) { moreInput->setVisible(id == 4); });

  layout->addWidget(group.container, 0, Qt::AlignLeft);
  layout->addWidget(moreInput, 0, Qt::AlignLeft);
  return box;
}

QWidget* RadioDocsPage::buildBlockGroupDemo() {
  const QVector<RadioOption> fruits = fruitOptions(false);

  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  const ManagedRadioGroup group1 = createManagedRadioGroup();
  group1.controller->setDistribution(AdRadioButtonGroup::Distribution::Fill);
  populateGroup(group1, fruits);
  group1.controller->setCheckedId(1);
  group1.container->setFixedWidth(520);

  const ManagedRadioGroup group2 = createManagedRadioGroup();
  group2.controller->setDistribution(AdRadioButtonGroup::Distribution::Fill);
  group2.controller->setVariant(AdRadio::Variant::Button);
  group2.controller->setButtonStyle(AdRadio::ButtonStyle::Solid);
  populateGroup(group2, fruits);
  group2.controller->setCheckedId(1);
  group2.container->setFixedWidth(520);

  const ManagedRadioGroup group3 = createManagedRadioGroup();
  group3.controller->setDistribution(AdRadioButtonGroup::Distribution::Fill);
  group3.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(group3, fruits);
  group3.controller->setCheckedId(2);
  group3.container->setFixedWidth(520);

  layout->addWidget(group1.container);
  layout->addWidget(group2.container);
  layout->addWidget(group3.container);
  return box;
}

QWidget* RadioDocsPage::buildGroupOptionsDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  const ManagedRadioGroup group1 = createManagedRadioGroup();
  populateGroup(group1, fruitOptions(false));
  group1.controller->setCheckedId(1);

  const ManagedRadioGroup group2 = createManagedRadioGroup();
  populateGroup(group2, fruitOptions(true));
  group2.controller->setCheckedId(1);

  const ManagedRadioGroup group3 = createManagedRadioGroup();
  group3.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(group3, fruitOptions(false));
  group3.controller->setCheckedId(1);

  const ManagedRadioGroup group4 = createManagedRadioGroup();
  group4.controller->setVariant(AdRadio::Variant::Button);
  group4.controller->setButtonStyle(AdRadio::ButtonStyle::Solid);
  populateGroup(group4, fruitOptions(true));
  group4.controller->setCheckedId(1);

  layout->addWidget(group1.container, 0, Qt::AlignLeft);
  layout->addWidget(group2.container, 0, Qt::AlignLeft);
  layout->addWidget(group3.container, 0, Qt::AlignLeft);
  layout->addWidget(group4.container, 0, Qt::AlignLeft);
  return box;
}

QWidget* RadioDocsPage::buildRadioButtonDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  const ManagedRadioGroup first = createManagedRadioGroup();
  first.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(first, cityOptions());
  first.controller->setCheckedId(1);

  const ManagedRadioGroup second = createManagedRadioGroup();
  second.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(second, {
                            makeOption(1, QStringLiteral("a"), QStringLiteral("Hangzhou")),
                            makeOption(2, QStringLiteral("b"), QStringLiteral("Shanghai"), true),
                            makeOption(3, QStringLiteral("c"), QStringLiteral("Beijing")),
                            makeOption(4, QStringLiteral("d"), QStringLiteral("Chengdu")),
                        });
  second.controller->setCheckedId(1);

  const ManagedRadioGroup third = createManagedRadioGroup();
  third.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(third, cityOptions());
  third.controller->setCheckedId(1);
  third.container->setDisabled(true);

  layout->addWidget(first.container, 0, Qt::AlignLeft);
  layout->addWidget(second.container, 0, Qt::AlignLeft);
  layout->addWidget(third.container, 0, Qt::AlignLeft);
  return box;
}

QWidget* RadioDocsPage::buildSizeDemo() {
  const QVector<RadioOption> cities = cityOptions();

  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  const ManagedRadioGroup large = createManagedRadioGroup();
  large.controller->setControlSize(AdRadio::ControlSize::Large);
  large.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(large, cities);
  large.controller->setCheckedId(1);

  const ManagedRadioGroup medium = createManagedRadioGroup();
  medium.controller->setControlSize(AdRadio::ControlSize::Medium);
  medium.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(medium, cities);
  medium.controller->setCheckedId(1);

  const ManagedRadioGroup small = createManagedRadioGroup();
  small.controller->setControlSize(AdRadio::ControlSize::Small);
  small.controller->setVariant(AdRadio::Variant::Button);
  populateGroup(small, cities);
  small.controller->setCheckedId(1);

  layout->addWidget(large.container, 0, Qt::AlignLeft);
  layout->addWidget(medium.container, 0, Qt::AlignLeft);
  layout->addWidget(small.container, 0, Qt::AlignLeft);
  return box;
}

QWidget* RadioDocsPage::buildSolidButtonDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  const ManagedRadioGroup first = createManagedRadioGroup();
  first.controller->setVariant(AdRadio::Variant::Button);
  first.controller->setButtonStyle(AdRadio::ButtonStyle::Solid);
  populateGroup(first, cityOptions());
  first.controller->setCheckedId(1);

  const ManagedRadioGroup second = createManagedRadioGroup();
  second.controller->setVariant(AdRadio::Variant::Button);
  second.controller->setButtonStyle(AdRadio::ButtonStyle::Solid);
  populateGroup(second, {
                            makeOption(1, QStringLiteral("a"), QStringLiteral("Hangzhou")),
                            makeOption(2, QStringLiteral("b"), QStringLiteral("Shanghai"), true),
                            makeOption(3, QStringLiteral("c"), QStringLiteral("Beijing")),
                            makeOption(4, QStringLiteral("d"), QStringLiteral("Chengdu")),
                        });
  second.controller->setCheckedId(3);

  layout->addWidget(first.container, 0, Qt::AlignLeft);
  layout->addWidget(second.container, 0, Qt::AlignLeft);
  return box;
}

QWidget* RadioDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* objectStyle = new AdRadio("Object styles");
  demo::bindThemeRefresh(objectStyle, [objectStyle]() {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(objectStyle);
    AdRadio::ComponentTokens tokens;
    tokens.colors.indicatorBorderColor = demo::themeColorOr(map.colorWarning, QColor("#faad14"));
    tokens.colors.indicatorFillColor = demo::themeColorOr(map.colorWarningBg, QColor("#fff7e6"));
    tokens.colors.textColor = demo::themeColorOr(map.colorPrimary, QColor("#1677ff"));
    objectStyle->setComponentTokens(tokens);
  });
  objectStyle->setChecked(true);

  auto* functionStyle = new AdRadio("Function semantic resolver");
  functionStyle->setComponentTokenResolver([functionStyle](
                                               const AdRadio::ComponentTokenContext& state) {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(functionStyle);
    AdRadio::ComponentTokens tokens;
    if (state.checked) {
      tokens.colors.indicatorBorderColor = demo::themeColorOr(map.colorWarning, QColor("#faad14"));
      tokens.colors.indicatorFillColor = demo::themeColorOr(map.colorWarning, QColor("#faad14"));
      tokens.colors.textColor = demo::themeColorOr(map.colorWarningText, QColor("#d48806"));
    } else {
      tokens.colors.indicatorBorderColor = demo::themeColorOr(map.colorBorder, QColor("#d9d9d9"));
      tokens.colors.textColor = demo::themeColorOr(map.colorTextTertiary, QColor("#8c8c8c"));
    }
    return tokens;
  });

  auto* group = new QButtonGroup(box);
  group->setExclusive(true);
  group->addButton(objectStyle);
  group->addButton(functionStyle);

  layout->addWidget(objectStyle, 0, Qt::AlignLeft);
  layout->addWidget(functionStyle, 0, Qt::AlignLeft);
  return box;
}
