#include "tabs_docs_page.h"

#include "demo_theme_utils.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include "antd_icons.h"
#include "widgets/tabs.h"

using adqt::widgets::AdTabs;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

QWidget* makePage(const QString& title, const QString& detail = QString()) {
  auto* page = new QWidget;
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(6);
  auto* titleLabel = new QLabel(title);
  QFont font = titleLabel->font();
  font.setBold(true);
  titleLabel->setFont(font);
  layout->addWidget(titleLabel);
  if (!detail.isEmpty()) {
    auto* detailLabel = new QLabel(detail);
    detailLabel->setWordWrap(true);
    layout->addWidget(detailLabel);
  }
  layout->addStretch();
  return page;
}

AdTabs::TabItem makeItem(const QString& key, const QString& label,
                         const QString& detail = QString()) {
  AdTabs::TabItem item;
  item.key = key;
  item.label = label;
  item.page = makePage(QStringLiteral("Content of %1").arg(label), detail);
  return item;
}

AdTabs* makeThreeTabs(QWidget* parent = nullptr) {
  auto* tabs = new AdTabs(parent);
  tabs->addTab(makeItem(QStringLiteral("1"), QStringLiteral("Tab 1")));
  tabs->addTab(makeItem(QStringLiteral("2"), QStringLiteral("Tab 2")));
  tabs->addTab(makeItem(QStringLiteral("3"), QStringLiteral("Tab 3")));
  tabs->setMinimumHeight(128);
  return tabs;
}

QWidget* labeledTabs(const QString& label, AdTabs* tabs) {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);
  auto* heading = new QLabel(label);
  QFont font = heading->font();
  font.setBold(true);
  heading->setFont(font);
  layout->addWidget(heading);
  layout->addWidget(tabs);
  return box;
}

}  // namespace

TabsDocsPage::TabsDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Tabs"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(QStringLiteral(
      "Tabs organize related views and keep navigation compact. The Qt component uses "
      "stable string keys, owned page widgets, native focus handling, and theme tokens."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic, disabled, and icons"),
             QStringLiteral("Cross-check: basic.tsx, disabled.tsx, centered.tsx, icon.tsx, and "
                            "custom-indicator.tsx. Arrow keys move between enabled tabs."),
             buildBasicDemo());
  addSection(
      root, QStringLiteral("Placement"),
      QStringLiteral("Cross-check: placement.tsx. Start and end follow Qt layout direction, so "
                     "the same API also behaves correctly in RTL applications."),
      buildPlacementDemo());
  addSection(
      root, QStringLiteral("Size"),
      QStringLiteral("Cross-check: size.tsx. Small, medium, and large use the shared theme's "
                     "control and typography scale."),
      buildSizeDemo());
  addSection(
      root, QStringLiteral("Card tabs"),
      QStringLiteral("Cross-check: card.tsx. Card tabs join the navigation strip to the content "
                     "surface and preserve placement-specific corners."),
      buildCardDemo());
  addSection(
      root, QStringLiteral("Editable card"),
      QStringLiteral("Cross-check: editable-card.tsx. Add and close are request signals; the "
                     "application remains the source of truth for its pages."),
      buildEditableDemo());
  addSection(
      root, QStringLiteral("Overflow and extra content"),
      QStringLiteral("Cross-check: slide.tsx and extra.tsx. Hidden tabs move into the more menu, "
                     "while start/end widgets keep dedicated space in the strip."),
      buildOverflowDemo());
  addSection(
      root, QStringLiteral("Component tokens"),
      QStringLiteral("Cross-check: component-token.tsx. Per-instance colors and metrics overlay "
                     "the resolved light, dark, comfortable, or compact theme."),
      buildTokenDemo());
  root->addStretch();
}

const QVector<QWidget*>& TabsDocsPage::sectionAnchors() const { return anchors_; }
const QStringList& TabsDocsPage::sectionTitles() const { return titles_; }

void TabsDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
                              QWidget* content) {
  auto* panel = new QFrame;
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);
  auto* heading = new QLabel(title);
  QFont font = heading->font();
  font.setBold(true);
  font.setPointSize(font.pointSize() + 1);
  heading->setFont(font);
  auto* copy = new QLabel(description);
  copy->setWordWrap(true);
  layout->addWidget(heading);
  layout->addWidget(copy);
  layout->addWidget(content);
  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* TabsDocsPage::buildBasicDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(14);

  auto* basic = new AdTabs;
  auto mail = makeItem(QStringLiteral("mail"), QStringLiteral("Mail"),
                       QStringLiteral("Unread conversations and recent activity."));
  mail.icon = outlined_icons::Mail();
  auto calendar = makeItem(QStringLiteral("calendar"), QStringLiteral("Calendar"),
                           QStringLiteral("Upcoming meetings and reminders."));
  calendar.icon = outlined_icons::Calendar();
  auto disabled = makeItem(QStringLiteral("tasks"), QStringLiteral("Tasks"));
  disabled.icon = outlined_icons::CheckSquare();
  disabled.enabled = false;
  basic->addTab(mail);
  basic->addTab(calendar);
  basic->addTab(disabled);
  basic->setCentered(true);
  basic->setIndicatorSize(28);
  basic->setIndicatorAlignment(AdTabs::IndicatorAlignment::Center);
  basic->setMinimumHeight(132);
  layout->addWidget(basic);
  layout->addWidget(makeHintLabel(QStringLiteral(
      "The Tasks tab is disabled. Use Left/Right, Home, and End when a tab has focus.")));
  return box;
}

QWidget* TabsDocsPage::buildPlacementDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto* controls = new QHBoxLayout;
  auto* label = new QLabel(QStringLiteral("Tab placement:"));
  auto* placement = new QComboBox;
  placement->addItems({QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("start"),
                       QStringLiteral("end")});
  controls->addWidget(label);
  controls->addWidget(placement);
  controls->addStretch();
  auto* tabs = makeThreeTabs();
  tabs->setMinimumHeight(190);
  connect(placement, &QComboBox::currentIndexChanged, tabs, [tabs](int index) {
    static const AdTabs::Placement placements[] = {
        AdTabs::Placement::Top, AdTabs::Placement::Bottom, AdTabs::Placement::Start,
        AdTabs::Placement::End};
    tabs->setTabPlacement(placements[std::clamp(index, 0, 3)]);
  });
  layout->addLayout(controls);
  layout->addWidget(tabs);
  return box;
}

QWidget* TabsDocsPage::buildSizeDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(14);
  auto* small = makeThreeTabs();
  small->setControlSize(AdTabs::ControlSize::Small);
  auto* medium = makeThreeTabs();
  auto* large = makeThreeTabs();
  large->setControlSize(AdTabs::ControlSize::Large);
  layout->addWidget(labeledTabs(QStringLiteral("Small"), small));
  layout->addWidget(labeledTabs(QStringLiteral("Medium"), medium));
  layout->addWidget(labeledTabs(QStringLiteral("Large"), large));
  return box;
}

QWidget* TabsDocsPage::buildCardDemo() {
  auto* tabs = makeThreeTabs();
  tabs->setType(AdTabs::Type::Card);
  tabs->setMinimumHeight(150);
  return tabs;
}

QWidget* TabsDocsPage::buildEditableDemo() {
  auto* tabs = new AdTabs;
  tabs->setType(AdTabs::Type::EditableCard);
  tabs->setMinimumHeight(156);
  for (int index = 1; index <= 3; ++index) {
    auto item = makeItem(QString::number(index), QStringLiteral("Tab %1").arg(index));
    item.closable = index != 3;
    tabs->addTab(item);
  }
  auto counter = std::make_shared<int>(0);
  connect(tabs, &AdTabs::addRequested, tabs, [tabs, counter] {
    const QString key = QStringLiteral("new-%1").arg(++*counter);
    tabs->addTab(makeItem(key, QStringLiteral("New Tab")));
    tabs->setCurrentKey(key);
  });
  connect(tabs, &AdTabs::tabCloseRequested, tabs,
          [tabs](const QString& key) { tabs->removeTab(key); });
  return tabs;
}

QWidget* TabsDocsPage::buildOverflowDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto* tabs = new AdTabs;
  tabs->setMinimumHeight(132);
  for (int index = 0; index < 20; ++index) {
    auto item = makeItem(QString::number(index), QStringLiteral("Tab %1").arg(index));
    item.enabled = index != 18;
    tabs->addTab(item);
  }
  auto* start = new QLabel(QStringLiteral("Workspace"));
  QFont font = start->font();
  font.setBold(true);
  start->setFont(font);
  start->setContentsMargins(0, 0, 16, 0);
  auto* end = new QPushButton(QStringLiteral("Refresh"));
  end->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
  tabs->setTabBarExtraContentStart(start);
  tabs->setTabBarExtraContentEnd(end);
  layout->addWidget(tabs);
  layout->addWidget(makeHintLabel(
      QStringLiteral("Resize the window to change which tabs appear in the more menu.")));
  return box;
}

QWidget* TabsDocsPage::buildTokenDemo() {
  auto* tabs = makeThreeTabs();
  tabs->setMinimumHeight(142);
  AdTabs::ComponentTokens tokens;
  tokens.colors.itemSelectedColor = QColor(QStringLiteral("#d4380d"));
  tokens.colors.inkBarColor = QColor(QStringLiteral("#d4380d"));
  tokens.colors.itemHoverColor = QColor(QStringLiteral("#ff7a45"));
  tokens.metrics.horizontalItemGutter = 20;
  tokens.metrics.indicatorThickness = 3;
  tabs->setComponentTokens(tokens);
  return tabs;
}
