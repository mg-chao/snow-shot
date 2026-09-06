#include "checkbox_docs_page.h"

#include "demo_theme_utils.h"
#include "widgets/checkbox.h"
#include "widgets/checkbox_group.h"

#include <QBoxLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

using adqt::widgets::AdCheckbox;
using adqt::widgets::AdCheckboxGroup;

namespace {

struct ManagedCheckboxGroup {
  QWidget* container = nullptr;
  QBoxLayout* layout = nullptr;
  AdCheckboxGroup* controller = nullptr;
};

ManagedCheckboxGroup createGroup(Qt::Orientation orientation = Qt::Horizontal) {
  ManagedCheckboxGroup group;
  group.container = new QWidget();
  group.layout = orientation == Qt::Vertical
                     ? static_cast<QBoxLayout*>(new QVBoxLayout(group.container))
                     : static_cast<QBoxLayout*>(new QHBoxLayout(group.container));
  group.layout->setContentsMargins(0, 0, 0, 0);
  group.controller = new AdCheckboxGroup(group.container);
  group.controller->setManagedLayout(group.layout);
  return group;
}

AdCheckbox* addOption(const ManagedCheckboxGroup& group, const QString& label,
                      const QVariant& value, bool disabled = false) {
  auto* checkbox = new AdCheckbox(label, group.container);
  checkbox->setEnabled(!disabled);
  group.layout->addWidget(checkbox);
  group.controller->addCheckbox(checkbox, value);
  return checkbox;
}

QString valuesText(const QVariantList& values) {
  QStringList labels;
  labels.reserve(values.size());
  for (const QVariant& value : values) {
    labels.append(value.toString());
  }
  return labels.isEmpty() ? QStringLiteral("None") : labels.join(QStringLiteral(", "));
}

}  // namespace

CheckboxDocsPage::CheckboxDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Checkbox"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      QStringLiteral("Collect choices from one or more options. The examples follow Ant Design's "
                     "Checkbox behaviors using Qt-native signals and value types."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic"), QStringLiteral("A standalone checkbox."),
             buildBasicDemo());
  addSection(root, QStringLiteral("Disabled"),
             QStringLiteral("Unavailable checked and unchecked states."), buildDisabledDemo());
  addSection(root, QStringLiteral("Controlled Checkbox"),
             QStringLiteral("Programmatically update checked and enabled state."),
             buildControlledDemo());
  addSection(root, QStringLiteral("Checkbox Group"),
             QStringLiteral("Select multiple typed values in stable option order."),
             buildGroupDemo());
  addSection(root, QStringLiteral("Check all"),
             QStringLiteral("Use Qt's native partial check state to summarize partial selection."),
             buildCheckAllDemo());
  addSection(root, QStringLiteral("Vertical group"),
             QStringLiteral("Groups can manage horizontal or vertical Qt box layouts."),
             buildVerticalDemo());
  addSection(root, QStringLiteral("Component tokens"),
             QStringLiteral("Per-widget tokens and state-aware token resolvers."),
             buildTokenDemo());
  root->addStretch();
}

const QVector<QWidget*>& CheckboxDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& CheckboxDocsPage::sectionTitles() const { return titles_; }

void CheckboxDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                  const QString& description, QWidget* content) {
  auto* panel = new QFrame();
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* titleLabel = new QLabel(title);
  QFont font = titleLabel->font();
  font.setBold(true);
  font.setPointSize(font.pointSize() + 1);
  titleLabel->setFont(font);
  auto* descriptionLabel = new QLabel(description);
  descriptionLabel->setWordWrap(true);

  layout->addWidget(titleLabel);
  layout->addWidget(descriptionLabel);
  layout->addWidget(content);
  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* CheckboxDocsPage::buildBasicDemo() {
  auto* host = new QWidget();
  auto* row = new QHBoxLayout(host);
  row->setContentsMargins(0, 0, 0, 0);
  row->addWidget(new AdCheckbox(QStringLiteral("Checkbox"), host));
  row->addStretch();
  return host;
}

QWidget* CheckboxDocsPage::buildDisabledDemo() {
  auto* host = new QWidget();
  auto* row = new QHBoxLayout(host);
  row->setContentsMargins(0, 0, 0, 0);
  auto* unchecked = new AdCheckbox(QStringLiteral("Disabled"), host);
  unchecked->setEnabled(false);
  auto* checked = new AdCheckbox(QStringLiteral("Disabled checked"), host);
  checked->setChecked(true);
  checked->setEnabled(false);
  row->addWidget(unchecked);
  row->addWidget(checked);
  row->addStretch();
  return host;
}

QWidget* CheckboxDocsPage::buildControlledDemo() {
  auto* host = new QWidget();
  auto* column = new QVBoxLayout(host);
  column->setContentsMargins(0, 0, 0, 0);
  auto* checkbox = new AdCheckbox(QStringLiteral("Controlled checkbox"), host);
  auto* controls = new QWidget(host);
  auto* row = new QHBoxLayout(controls);
  row->setContentsMargins(0, 0, 0, 0);
  auto* toggleChecked = new QPushButton(QStringLiteral("Toggle checked"), controls);
  auto* toggleDisabled = new QPushButton(QStringLiteral("Toggle disabled"), controls);
  row->addWidget(toggleChecked);
  row->addWidget(toggleDisabled);
  row->addStretch();
  QObject::connect(toggleChecked, &QPushButton::clicked, checkbox,
                   [checkbox]() { checkbox->setChecked(!checkbox->isChecked()); });
  QObject::connect(toggleDisabled, &QPushButton::clicked, checkbox,
                   [checkbox]() { checkbox->setEnabled(!checkbox->isEnabled()); });
  column->addWidget(checkbox);
  column->addWidget(controls);
  return host;
}

QWidget* CheckboxDocsPage::buildGroupDemo() {
  auto* host = new QWidget();
  auto* column = new QVBoxLayout(host);
  column->setContentsMargins(0, 0, 0, 0);
  ManagedCheckboxGroup group = createGroup();
  group.container->setParent(host);
  addOption(group, QStringLiteral("Apple"), QStringLiteral("Apple"));
  addOption(group, QStringLiteral("Pear"), QStringLiteral("Pear"));
  addOption(group, QStringLiteral("Orange"), QStringLiteral("Orange"), true);
  auto* output = makeHintLabel(QStringLiteral("Selected: Apple"), host);
  group.controller->setValues({QStringLiteral("Apple")});
  QObject::connect(group.controller, &AdCheckboxGroup::valuesChanged, output,
                   [output](const QVariantList& values) {
                     output->setText(QStringLiteral("Selected: %1").arg(valuesText(values)));
                   });
  column->addWidget(group.container);
  column->addWidget(output);
  return host;
}

QWidget* CheckboxDocsPage::buildCheckAllDemo() {
  auto* host = new QWidget();
  auto* column = new QVBoxLayout(host);
  column->setContentsMargins(0, 0, 0, 0);
  auto* checkAll = new AdCheckbox(QStringLiteral("Check all"), host);
  ManagedCheckboxGroup group = createGroup();
  group.container->setParent(host);
  for (const QString& value :
       {QStringLiteral("Apple"), QStringLiteral("Pear"), QStringLiteral("Orange")}) {
    addOption(group, value, value);
  }

  auto syncSummary = [checkAll](const QVariantList& values) {
    const QSignalBlocker blocker(checkAll);
    checkAll->setChecked(values.size() == 3);
    checkAll->setIndeterminate(!values.isEmpty() && values.size() < 3);
  };
  QObject::connect(group.controller, &AdCheckboxGroup::valuesChanged, checkAll, syncSummary);
  QObject::connect(checkAll, &QAbstractButton::clicked, group.controller, [group](bool checked) {
    group.controller->setValues(checked
                                    ? QVariantList({QStringLiteral("Apple"), QStringLiteral("Pear"),
                                                    QStringLiteral("Orange")})
                                    : QVariantList());
  });
  group.controller->setValues({QStringLiteral("Apple"), QStringLiteral("Pear")});
  syncSummary(group.controller->values());
  column->addWidget(checkAll);
  column->addWidget(group.container);
  return host;
}

QWidget* CheckboxDocsPage::buildVerticalDemo() {
  ManagedCheckboxGroup group = createGroup(Qt::Vertical);
  addOption(group, QStringLiteral("Hangzhou"), QStringLiteral("hangzhou"));
  addOption(group, QStringLiteral("Shanghai"), QStringLiteral("shanghai"));
  addOption(group, QStringLiteral("Beijing"), QStringLiteral("beijing"));
  group.controller->setValues({QStringLiteral("hangzhou"), QStringLiteral("beijing")});
  group.layout->addStretch();
  return group.container;
}

QWidget* CheckboxDocsPage::buildTokenDemo() {
  auto* host = new QWidget();
  auto* row = new QHBoxLayout(host);
  row->setContentsMargins(0, 0, 0, 0);

  auto* fixed = new AdCheckbox(QStringLiteral("Object tokens"), host);
  fixed->setChecked(true);
  AdCheckbox::ComponentTokens fixedTokens;
  fixedTokens.colors.indicatorFillColor = QColor(QStringLiteral("#13a8a8"));
  fixedTokens.colors.indicatorBorderColor = QColor(QStringLiteral("#13a8a8"));
  fixedTokens.metrics.borderRadius = 4;
  fixed->setComponentTokens(fixedTokens);

  auto* stateAware = new AdCheckbox(QStringLiteral("State-aware resolver"), host);
  stateAware->setComponentTokenResolver([](const AdCheckbox::ComponentTokenContext& state) {
    AdCheckbox::ComponentTokens tokens;
    if (state.checked) {
      tokens.colors.indicatorFillColor = QColor(QStringLiteral("#52c41a"));
      tokens.colors.indicatorBorderColor = QColor(QStringLiteral("#52c41a"));
    }
    return tokens;
  });
  row->addWidget(fixed);
  row->addWidget(stateAware);
  row->addStretch();
  return host;
}
