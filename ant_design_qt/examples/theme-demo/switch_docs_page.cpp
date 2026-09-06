#include "switch_docs_page.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "antd_icons.h"

using adqt::widgets::AdSwitch;
namespace outlined_icons = adqt::icons::antd::outlined;

SwitchDocsPage::SwitchDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Switch");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel("Used to toggle between two states.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Disabled", "Demo: disabled.tsx", buildDisabledDemo());
  addSection(root, "Text, icon & label", "Demo: text.tsx", buildTextDemo());
  addSection(root, "Two sizes", "Demo: size.tsx", buildSizeDemo());
  addSection(root, "Loading", "Demo: loading.tsx", buildLoadingDemo());
  addSection(root, "Custom component tokens", "Demo: component-token.tsx",
             buildComponentTokenDemo());
  addSection(root, "Style resolver", "Demo: style-resolver.tsx", buildStyleResolverDemo());

  root->addStretch();
}

const QVector<QWidget*>& SwitchDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& SwitchDocsPage::sectionTitles() const { return titles_; }

void SwitchDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

QWidget* SwitchDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);

  auto* sw = new AdSwitch();
  sw->setChecked(true);
  row->addWidget(sw);
  row->addStretch();
  return box;
}

QWidget* SwitchDocsPage::buildDisabledDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* sw = new AdSwitch();
  sw->setChecked(true);
  sw->setEnabled(false);

  auto* toggle = new QPushButton("Toggle disabled");
  connect(toggle, &QAbstractButton::clicked, sw, [sw]() { sw->setEnabled(!sw->isEnabled()); });

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->addWidget(sw);
  row->addStretch();

  layout->addLayout(row);
  layout->addWidget(toggle, 0, Qt::AlignLeft);
  return box;
}

QWidget* SwitchDocsPage::buildTextDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first = new AdSwitch();
  first->setCheckedText("ON");
  first->setUncheckedText("OFF");
  first->setChecked(true);

  auto* second = new AdSwitch();
  second->setCheckedText("1");
  second->setUncheckedText("0");

  auto* third = new AdSwitch();
  third->setCheckedIconRef(outlined_icons::Check());
  third->setUncheckedIconRef(outlined_icons::Close());
  third->setText("Power");
  third->setChecked(true);

  auto* row1 = new QHBoxLayout();
  row1->setContentsMargins(0, 0, 0, 0);
  row1->setSpacing(8);
  row1->addWidget(first);
  row1->addWidget(second);
  row1->addStretch();

  auto* row2 = new QHBoxLayout();
  row2->setContentsMargins(0, 0, 0, 0);
  row2->setSpacing(8);
  row2->addWidget(third);
  row2->addStretch();

  layout->addLayout(row1);
  layout->addLayout(row2);
  return box;
}

QWidget* SwitchDocsPage::buildSizeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first = new AdSwitch();
  first->setChecked(true);

  auto* second = new AdSwitch();
  second->setControlSize(AdSwitch::ControlSize::Small);
  second->setChecked(true);

  auto* row1 = new QHBoxLayout();
  row1->setContentsMargins(0, 0, 0, 0);
  row1->addWidget(first);
  row1->addStretch();

  auto* row2 = new QHBoxLayout();
  row2->setContentsMargins(0, 0, 0, 0);
  row2->addWidget(second);
  row2->addStretch();

  layout->addLayout(row1);
  layout->addLayout(row2);
  return box;
}

QWidget* SwitchDocsPage::buildLoadingDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first = new AdSwitch();
  first->setLoading(true);
  first->setChecked(true);

  auto* second = new AdSwitch();
  second->setControlSize(AdSwitch::ControlSize::Small);
  second->setLoading(true);

  auto* row1 = new QHBoxLayout();
  row1->setContentsMargins(0, 0, 0, 0);
  row1->addWidget(first);
  row1->addStretch();

  auto* row2 = new QHBoxLayout();
  row2->setContentsMargins(0, 0, 0, 0);
  row2->addWidget(second);
  row2->addStretch();

  layout->addLayout(row1);
  layout->addLayout(row2);
  return box;
}

QWidget* SwitchDocsPage::buildComponentTokenDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);

  auto* sw = new AdSwitch();
  sw->setChecked(true);
  AdSwitch::ComponentTokens overrides;
  overrides.metrics.trackHeight = 14;
  overrides.metrics.trackMinWidth = 32;
  overrides.metrics.trackPadding = 0;
  overrides.metrics.thumbSize = 20;
  overrides.colors.checkedTrack = QColor(QStringLiteral("#1976d2"));
  overrides.colors.checkedTrackHover = QColor(QStringLiteral("#1976d2"));
  overrides.colors.thumb = QColor(QStringLiteral("#1976d2"));
  sw->setComponentTokens(overrides);

  row->addWidget(sw);
  row->addStretch();
  return box;
}

QWidget* SwitchDocsPage::buildStyleResolverDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* objectStyled = new AdSwitch();
  objectStyled->setControlSize(AdSwitch::ControlSize::Small);
  objectStyled->setCheckedText("on");
  objectStyled->setUncheckedText("off");
  objectStyled->setChecked(true);

  AdSwitch::ComponentTokens objectOverrides;
  objectOverrides.colors.uncheckedTrack = QColor("#F5D2D2");
  objectOverrides.colors.uncheckedTrackHover = QColor("#F5D2D2");
  objectOverrides.colors.checkedTrack = QColor("#F5D2D2");
  objectOverrides.colors.checkedTrackHover = QColor("#F5D2D2");
  objectOverrides.colors.content = QColor("#4A4A4A");
  objectOverrides.colors.thumb = QColor("#FFFFFF");
  objectStyled->setComponentTokens(objectOverrides);

  auto* resolverStyled = new AdSwitch();
  resolverStyled->setCheckedText("on");
  resolverStyled->setUncheckedText("off");
  resolverStyled->setChecked(true);
  resolverStyled->setComponentTokenResolver([](const AdSwitch::ComponentTokenContext& ctx) {
    AdSwitch::ComponentTokens out;
    if (ctx.controlSize == AdSwitch::ControlSize::Medium) {
      out.colors.uncheckedTrack = QColor("#BDE3C3");
      out.colors.uncheckedTrackHover = QColor("#BDE3C3");
      out.colors.checkedTrack = QColor("#BDE3C3");
      out.colors.checkedTrackHover = QColor("#BDE3C3");
      out.colors.content = QColor("#214D28");
    }
    if (ctx.checked) {
      out.colors.thumb = QColor("#FFFFFF");
    }
    return out;
  });

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  row->addWidget(objectStyled);
  row->addWidget(resolverStyled);
  row->addStretch();

  layout->addLayout(row);
  return box;
}
