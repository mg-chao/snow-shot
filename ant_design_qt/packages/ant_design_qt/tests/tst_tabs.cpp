#include <QAbstractButton>
#include <QDir>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtTest>

#include "antd_icons.h"
#include "theme/theme.h"
#include "widgets/tabs.h"

#include <algorithm>

using adqt::widgets::AdTabs;

namespace {

QAbstractButton* tabButton(AdTabs* tabs, const QString& name) {
  const QList<QAbstractButton*> buttons = tabs->findChildren<QAbstractButton*>();
  for (QAbstractButton* button : buttons) {
    if (button->accessibleName() == name &&
        button->accessibleDescription().contains("tab", Qt::CaseInsensitive)) {
      return button;
    }
  }
  return nullptr;
}

QAbstractButton* operationButton(AdTabs* tabs, const QString& name) {
  const QList<QAbstractButton*> buttons = tabs->findChildren<QAbstractButton*>();
  for (QAbstractButton* button : buttons) {
    if (button->accessibleName() == name) {
      return button;
    }
  }
  return nullptr;
}

}  // namespace

class TabsTest final : public QObject {
  Q_OBJECT

 private slots:
  void firstEnabledTabBecomesCurrent();
  void keysStayStableAcrossInsertionAndRemoval();
  void disabledTabsCannotBecomeCurrent();
  void takeTabTransfersOwnership();
  void externallyDestroyedPageIsRemoved();
  void invalidPageHierarchiesAreRejected();
  void editableCardEmitsAddAndCloseRequests();
  void arrowKeysSelectTheNextEnabledTab();
  void selectionUsesStandardButtonState();
  void propertiesRoundTrip();
  void sizeHintsFollowContentAndTokens();
  void extraContentTracksQObjectLifetime();
  void widerLabelsShrinkBeforeShorterTabsOverflow();
  void overflowCollapsesTabs();
  void startPlacementFollowsLayoutDirection();
  void rendersRepresentativeStates();
};

void TabsTest::firstEnabledTabBecomesCurrent() {
  AdTabs tabs;
  QSignalSpy indexChanged(&tabs, &AdTabs::currentIndexChanged);
  QSignalSpy keyChanged(&tabs, &AdTabs::currentKeyChanged);

  AdTabs::TabItem disabled;
  disabled.key = QStringLiteral("disabled");
  disabled.label = QStringLiteral("Disabled");
  disabled.enabled = false;
  QCOMPARE(tabs.addTab(disabled), 0);
  QCOMPARE(tabs.currentIndex(), -1);

  QCOMPARE(tabs.addTab(QStringLiteral("profile"), QStringLiteral("Profile")), 1);
  QCOMPARE(tabs.currentIndex(), 1);
  QCOMPARE(tabs.currentKey(), QStringLiteral("profile"));
  QCOMPARE(indexChanged.count(), 1);
  QCOMPARE(keyChanged.count(), 1);
  QCOMPARE(tabs.addTab(QStringLiteral("profile"), QStringLiteral("Duplicate")), -1);
  QCOMPARE(tabs.count(), 2);
}

void TabsTest::keysStayStableAcrossInsertionAndRemoval() {
  AdTabs tabs;
  tabs.addTab(QStringLiteral("a"), QStringLiteral("A"));
  tabs.addTab(QStringLiteral("b"), QStringLiteral("B"));
  tabs.setCurrentKey(QStringLiteral("b"));

  AdTabs::TabItem inserted;
  inserted.key = QStringLiteral("zero");
  inserted.label = QStringLiteral("Zero");
  QCOMPARE(tabs.insertTab(0, inserted), 0);
  QCOMPARE(tabs.currentIndex(), 2);
  QCOMPARE(tabs.currentKey(), QStringLiteral("b"));

  tabs.removeTab(QStringLiteral("a"));
  QCOMPARE(tabs.currentIndex(), 1);
  QCOMPARE(tabs.currentKey(), QStringLiteral("b"));
  QCOMPARE(tabs.tabKey(0), QStringLiteral("zero"));
}

void TabsTest::disabledTabsCannotBecomeCurrent() {
  AdTabs tabs;
  tabs.addTab(QStringLiteral("a"), QStringLiteral("A"));
  tabs.addTab(QStringLiteral("b"), QStringLiteral("B"));
  tabs.addTab(QStringLiteral("c"), QStringLiteral("C"));
  tabs.setTabEnabled(1, false);

  tabs.setCurrentIndex(1);
  QCOMPARE(tabs.currentKey(), QStringLiteral("a"));
  tabs.setTabEnabled(0, false);
  QCOMPARE(tabs.currentKey(), QStringLiteral("c"));
  tabs.setTabEnabled(2, false);
  QCOMPARE(tabs.currentIndex(), -1);
  tabs.setTabEnabled(1, true);
  QCOMPARE(tabs.currentKey(), QStringLiteral("b"));
}

void TabsTest::takeTabTransfersOwnership() {
  AdTabs tabs;
  auto* page = new QLabel(QStringLiteral("Page"));
  QPointer<QWidget> guard(page);
  tabs.addTab(QStringLiteral("page"), QStringLiteral("Page"), page);

  QWidget* taken = tabs.takeTab(0);
  QCOMPARE(taken, page);
  QVERIFY(taken->parentWidget() == nullptr);
  QCOMPARE(tabs.count(), 0);
  QVERIFY(!guard.isNull());

  auto* replacement = new QLabel(QStringLiteral("Replacement"));
  tabs.addTab(QStringLiteral("page"), QStringLiteral("Replacement"), replacement);
  delete taken;
  QVERIFY(guard.isNull());
  QCoreApplication::processEvents();
  QCOMPARE(tabs.count(), 1);
  QCOMPARE(tabs.widget(0), replacement);
}

void TabsTest::externallyDestroyedPageIsRemoved() {
  AdTabs tabs;
  auto* first = new QWidget;
  tabs.addTab(QStringLiteral("first"), QStringLiteral("First"), first);
  tabs.addTab(QStringLiteral("second"), QStringLiteral("Second"));
  QSignalSpy indexChanged(&tabs, &AdTabs::currentIndexChanged);
  QSignalSpy keyChanged(&tabs, &AdTabs::currentKeyChanged);

  delete first;
  QTRY_COMPARE(tabs.count(), 1);

  QCOMPARE(tabs.currentIndex(), 0);
  QCOMPARE(tabs.currentKey(), QStringLiteral("second"));
  QCOMPARE(indexChanged.count(), 0);
  QCOMPARE(keyChanged.count(), 1);
}

void TabsTest::invalidPageHierarchiesAreRejected() {
  AdTabs tabs;
  auto* page = new QWidget;
  QCOMPARE(tabs.addTab(QStringLiteral("first"), QStringLiteral("First"), page), 0);
  QCOMPARE(tabs.addTab(QStringLiteral("duplicate-page"), QStringLiteral("Duplicate"), page), -1);
  QCOMPARE(tabs.addTab(QStringLiteral("self"), QStringLiteral("Self"), &tabs), -1);
  QCOMPARE(tabs.count(), 1);

  tabs.setTabBarExtraContentStart(page);
  QVERIFY(tabs.tabBarExtraContentStart() == nullptr);
  QCOMPARE(tabs.widget(0), page);
  QStackedWidget* stack = tabs.findChild<QStackedWidget*>();
  QVERIFY(stack);
  tabs.setTabBarExtraContentStart(stack);
  QVERIFY(tabs.tabBarExtraContentStart() == nullptr);

  delete tabs.takeTab(0);
}

void TabsTest::editableCardEmitsAddAndCloseRequests() {
  AdTabs tabs;
  tabs.setType(AdTabs::Type::EditableCard);
  tabs.addTab(QStringLiteral("one"), QStringLiteral("One"));
  tabs.resize(420, 180);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));

  QSignalSpy addRequested(&tabs, &AdTabs::addRequested);
  QSignalSpy closeRequested(&tabs, &AdTabs::tabCloseRequested);
  QAbstractButton* add = operationButton(&tabs, QStringLiteral("Add tab"));
  QAbstractButton* tab = tabButton(&tabs, QStringLiteral("One"));
  QVERIFY(add);
  QVERIFY(add->isVisible());
  QVERIFY(tab);

  QTest::mouseClick(add, Qt::LeftButton);
  QCOMPARE(addRequested.count(), 1);
  tab->setFocus(Qt::TabFocusReason);
  QTest::keyClick(tab, Qt::Key_Delete);
  QCOMPARE(closeRequested.count(), 1);
  QCOMPARE(closeRequested.takeFirst().at(0).toString(), QStringLiteral("one"));

  tabs.setTabClosable(0, false);
  QTest::keyClick(tab, Qt::Key_Delete);
  QCOMPARE(closeRequested.count(), 0);
}

void TabsTest::arrowKeysSelectTheNextEnabledTab() {
  AdTabs tabs;
  tabs.addTab(QStringLiteral("a"), QStringLiteral("A"));
  tabs.addTab(QStringLiteral("b"), QStringLiteral("B"));
  tabs.addTab(QStringLiteral("c"), QStringLiteral("C"));
  tabs.setTabEnabled(1, false);
  tabs.resize(520, 180);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));

  QAbstractButton* first = tabButton(&tabs, QStringLiteral("A"));
  QVERIFY(first);
  first->setFocus(Qt::TabFocusReason);
  QTest::keyClick(first, Qt::Key_Right);
  QCOMPARE(tabs.currentKey(), QStringLiteral("c"));
  QAbstractButton* last = tabButton(&tabs, QStringLiteral("C"));
  QVERIFY(last);
  QVERIFY(last->hasFocus());
}

void TabsTest::selectionUsesStandardButtonState() {
  AdTabs tabs;
  tabs.addTab(QStringLiteral("a"), QStringLiteral("A"));
  tabs.addTab(QStringLiteral("b"), QStringLiteral("B"));
  tabs.resize(420, 180);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));

  QAbstractButton* first = tabButton(&tabs, QStringLiteral("A"));
  QAbstractButton* second = tabButton(&tabs, QStringLiteral("B"));
  QVERIFY(first);
  QVERIFY(second);
  QVERIFY(first->isCheckable());
  QVERIFY(first->isChecked());
  QCOMPARE(first->focusPolicy(), Qt::TabFocus);
  QVERIFY(!second->isChecked());
  QCOMPARE(second->focusPolicy(), Qt::NoFocus);

  QTest::mouseClick(second, Qt::LeftButton);
  QVERIFY(!first->isChecked());
  QCOMPARE(first->focusPolicy(), Qt::NoFocus);
  QVERIFY(second->isChecked());
  QCOMPARE(second->focusPolicy(), Qt::TabFocus);

  tabs.setEnabled(false);
  QCOMPARE(second->cursor().shape(), Qt::ArrowCursor);

  AdTabs iconOnly;
  iconOnly.addTab(QStringLiteral("files"), QString());
  QAbstractButton* iconOnlyButton = tabButton(&iconOnly, QStringLiteral("files"));
  QVERIFY(iconOnlyButton);
}

void TabsTest::propertiesRoundTrip() {
  AdTabs tabs;
  tabs.setType(AdTabs::Type::Card);
  tabs.setControlSize(AdTabs::ControlSize::Large);
  tabs.setTabPlacement(AdTabs::Placement::Start);
  tabs.setCentered(true);
  tabs.setAnimated(false);
  tabs.setHideAdd(true);
  tabs.setTabBarGutter(12);
  tabs.setIndicatorSize(24);
  tabs.setIndicatorAlignment(AdTabs::IndicatorAlignment::Center);

  QCOMPARE(tabs.type(), AdTabs::Type::Card);
  QCOMPARE(tabs.controlSize(), AdTabs::ControlSize::Large);
  QCOMPARE(tabs.tabPlacement(), AdTabs::Placement::Start);
  QVERIFY(tabs.centered());
  QVERIFY(!tabs.animated());
  QVERIFY(tabs.hideAdd());
  QCOMPARE(tabs.tabBarGutter(), 12);
  QCOMPARE(tabs.indicatorSize(), 24);
  QCOMPARE(tabs.indicatorAlignment(), AdTabs::IndicatorAlignment::Center);
}

void TabsTest::sizeHintsFollowContentAndTokens() {
  AdTabs tabs;
  tabs.addTab(QStringLiteral("short"), QStringLiteral("Short"));
  const QSize shortHint = tabs.sizeHint();

  tabs.setTabText(0, QStringLiteral("A substantially longer tab label"));
  const QSize longHint = tabs.sizeHint();
  QVERIFY(longHint.width() > shortHint.width());

  AdTabs::ComponentTokens tokens;
  tokens.metrics.verticalItemPadding = 30;
  tabs.setComponentTokens(tokens);
  QVERIFY(tabs.sizeHint().height() > longHint.height());
}

void TabsTest::extraContentTracksQObjectLifetime() {
  AdTabs tabs;
  tabs.addTab(QStringLiteral("tab"), QStringLiteral("Tab"));
  auto* extra = new QLabel(QStringLiteral("Extra"));
  extra->setFixedWidth(100);
  tabs.setTabBarExtraContentStart(extra);
  QCOMPARE(tabs.tabBarExtraContentStart(), extra);
  tabs.setTabBarExtraContentEnd(extra);
  QVERIFY(tabs.tabBarExtraContentStart() == nullptr);
  QCOMPARE(tabs.tabBarExtraContentEnd(), extra);
  tabs.setTabBarExtraContentStart(extra);
  QCOMPARE(tabs.tabBarExtraContentStart(), extra);
  QVERIFY(tabs.tabBarExtraContentEnd() == nullptr);

  tabs.resize(360, 160);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));
  QAbstractButton* button = tabButton(&tabs, QStringLiteral("Tab"));
  QVERIFY(button);
  const int withExtraX = button->x();
  QVERIFY(withExtraX >= extra->width());

  delete extra;
  QVERIFY(tabs.tabBarExtraContentStart() == nullptr);
  QTRY_VERIFY(button->x() < withExtraX);
}

void TabsTest::widerLabelsShrinkBeforeShorterTabsOverflow() {
  AdTabs tabs;
  tabs.setAnimated(false);
  tabs.setTabBarGutter(0);
  tabs.addTab(QStringLiteral("history"), QStringLiteral("History"));
  tabs.addTab(QStringLiteral("storage-status"), QStringLiteral("Storage Status"));
  tabs.setCurrentKey(QStringLiteral("storage-status"));
  tabs.resize(480, 160);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));

  QAbstractButton* history = tabButton(&tabs, QStringLiteral("History"));
  QAbstractButton* storageStatus = tabButton(&tabs, QStringLiteral("Storage Status"));
  QAbstractButton* more = operationButton(&tabs, QStringLiteral("More tabs"));
  QVERIFY(history);
  QVERIFY(storageStatus);
  QVERIFY(more);

  const int historyNaturalWidth = history->sizeHint().width();
  const int storageNaturalWidth = storageStatus->sizeHint().width();
  QVERIFY(storageNaturalWidth > historyNaturalWidth);
  const int reduction = std::max(1, (storageNaturalWidth - historyNaturalWidth) / 2);
  tabs.resize(historyNaturalWidth + storageNaturalWidth - reduction, tabs.height());
  QCoreApplication::processEvents();

  QVERIFY(history->isVisible());
  QVERIFY(storageStatus->isVisible());
  QVERIFY(more->isHidden());
  QCOMPARE(history->width(), historyNaturalWidth);
  QVERIFY(storageStatus->width() < storageNaturalWidth);
  QVERIFY(history->toolTip().isEmpty());
  QCOMPARE(storageStatus->toolTip(), QStringLiteral("Storage Status"));
}

void TabsTest::overflowCollapsesTabs() {
  AdTabs tabs;
  for (int index = 0; index < 12; ++index) {
    tabs.addTab(QString::number(index), QStringLiteral("Long tab %1").arg(index));
  }
  tabs.resize(360, 180);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));

  QAbstractButton* more = operationButton(&tabs, QStringLiteral("More tabs"));
  QVERIFY(more);
  QVERIFY(more->isVisible());
  int hiddenTabCount = 0;
  const QList<QAbstractButton*> buttons =
      tabs.findChildren<QAbstractButton*>(QStringLiteral("ad-tabs-item"));
  for (QAbstractButton* button : buttons) {
    hiddenTabCount += button->isHidden() ? 1 : 0;
  }
  QVERIFY(hiddenTabCount > 0);
  QVERIFY(tabButton(&tabs, QStringLiteral("Long tab 0"))->isVisible());
}

void TabsTest::startPlacementFollowsLayoutDirection() {
  AdTabs tabs;
  tabs.setTabPlacement(AdTabs::Placement::Start);
  tabs.addTab(QStringLiteral("a"), QStringLiteral("A"));
  tabs.resize(480, 220);
  tabs.show();
  QVERIFY(QTest::qWaitForWindowExposed(&tabs));
  QWidget* strip = tabs.findChild<QWidget*>(QStringLiteral("ad-tabs-strip"));
  QStackedWidget* stack = tabs.findChild<QStackedWidget*>();
  QVERIFY(strip);
  QVERIFY(stack);
  QVERIFY(strip->geometry().right() < stack->geometry().left());

  tabs.setLayoutDirection(Qt::RightToLeft);
  QCoreApplication::processEvents();
  QVERIFY(strip->geometry().left() > stack->geometry().right());
}

void TabsTest::rendersRepresentativeStates() {
  adqt::icons::antd::ensureRegistered();
  auto& themeManager = adqt::theme::ThemeManager::instance();
  const adqt::theme::ThemeConfig originalConfig = themeManager.config();
  themeManager.applyTo(*qApp);

  QWidget showcase;
  showcase.setObjectName(QStringLiteral("tabsSnapshot"));
  showcase.setFocusPolicy(Qt::StrongFocus);
  showcase.resize(960, 640);
  auto* grid = new QGridLayout(&showcase);
  grid->setContentsMargins(24, 24, 24, 24);
  grid->setHorizontalSpacing(24);
  grid->setVerticalSpacing(24);

  auto makePage = [](const QString& text) {
    auto* page = new QLabel(text);
    page->setAlignment(Qt::AlignCenter);
    return page;
  };
  auto populate = [&makePage](AdTabs* tabs, int count) {
    for (int index = 0; index < count; ++index) {
      AdTabs::TabItem item;
      item.key = QString::number(index);
      item.label = QStringLiteral("Tab %1").arg(index + 1);
      item.page = makePage(QStringLiteral("Content %1").arg(index + 1));
      item.enabled = index != 2;
      item.closable = index != 0;
      if (index == 0) {
        item.icon = adqt::icons::antd::outlined::FolderOpen();
      }
      tabs->addTab(item);
    }
  };

  auto* line = new AdTabs;
  populate(line, 4);
  line->setCentered(true);
  line->setIndicatorSize(30);
  line->setIndicatorAlignment(AdTabs::IndicatorAlignment::Center);
  auto* card = new AdTabs;
  card->setType(AdTabs::Type::Card);
  populate(card, 4);
  auto* editable = new AdTabs;
  editable->setType(AdTabs::Type::EditableCard);
  populate(editable, 4);
  auto* vertical = new AdTabs;
  vertical->setTabPlacement(AdTabs::Placement::Start);
  populate(vertical, 7);
  auto* overflow = new AdTabs;
  populate(overflow, 14);
  auto* extra = new QLabel(QStringLiteral("Workspace"));
  extra->setContentsMargins(0, 0, 12, 0);
  overflow->setTabBarExtraContentStart(extra);

  grid->addWidget(line, 0, 0);
  grid->addWidget(card, 0, 1);
  grid->addWidget(editable, 1, 0);
  grid->addWidget(vertical, 1, 1);
  grid->addWidget(overflow, 2, 0, 1, 2);
  grid->setRowStretch(0, 1);
  grid->setRowStretch(1, 1);
  grid->setRowStretch(2, 1);

  const QString snapshotDirectory = qEnvironmentVariable("ADQT_TABS_SNAPSHOT_DIR");
  auto renderScheme = [&](adqt::theme::ThemeScheme scheme, const QString& fileName) {
    themeManager.setPreset(scheme, adqt::theme::ThemeDensity::Comfortable);
    const adqt::theme::ThemeMapToken colors = themeManager.resolveTheme(&showcase);
    QPalette palette = showcase.palette();
    palette.setColor(QPalette::Window, colors.colorBgContainer);
    palette.setColor(QPalette::Base, colors.colorBgContainer);
    palette.setColor(QPalette::WindowText, colors.colorText);
    palette.setColor(QPalette::Text, colors.colorText);
    showcase.setPalette(palette);
    showcase.setAutoFillBackground(true);
    showcase.show();
    showcase.setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::qWait(250);
    if (QWidget* focus = qApp->focusWidget()) {
      focus->clearFocus();
    }
    QCoreApplication::processEvents();
    QWidget* lineIndicator = line->findChild<QWidget*>(QStringLiteral("ad-tabs-indicator"));
    QVERIFY(lineIndicator);
    QVERIFY(lineIndicator->isVisible());
    QVERIFY(lineIndicator->width() >= 28);
    QVERIFY(lineIndicator->height() >= 2);
    QVERIFY(lineIndicator->height() <= 4);
    const QImage image = showcase.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), showcase.size());
    QSet<QRgb> sampledColors;
    for (int y = 0; y < image.height(); y += 8) {
      for (int x = 0; x < image.width(); x += 8) {
        sampledColors.insert(image.pixel(x, y));
      }
    }
    QVERIFY2(sampledColors.size() > 8,
             "Tabs snapshot should contain text, "
             "borders, surfaces, and state colors");
    if (!snapshotDirectory.isEmpty()) {
      QDir().mkpath(snapshotDirectory);
      QVERIFY(image.save(QDir(snapshotDirectory).filePath(fileName)));
    }
  };

  renderScheme(adqt::theme::ThemeScheme::Light, QStringLiteral("tabs-light.png"));
  renderScheme(adqt::theme::ThemeScheme::Dark, QStringLiteral("tabs-dark.png"));
  themeManager.setConfig(originalConfig);
}

QTEST_MAIN(TabsTest)

#include "tst_tabs.moc"
