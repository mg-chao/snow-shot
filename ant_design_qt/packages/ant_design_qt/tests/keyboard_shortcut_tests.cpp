#include <QGuiApplication>
#include <QLineEdit>
#include <QShortcut>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

#include "widgets/button.h"
#include "widgets/switch.h"

namespace {

void focusWithTab(QLineEdit* initialFocus, QWidget* target) {
  initialFocus->setFocus(Qt::TabFocusReason);
  QTRY_VERIFY(initialFocus->hasFocus());

  QTest::keyClick(initialFocus, Qt::Key_Tab);
  QTRY_VERIFY(target->hasFocus());
}

}  // namespace

class KeyboardShortcutTests final : public QObject {
  Q_OBJECT

 private slots:
  void buttonUsesTabFocusByDefault();
  void enterActivatesButtonAfterTabFocus_data();
  void enterActivatesButtonAfterTabFocus();
  void enterTogglesSwitchAfterTabFocus_data();
  void enterTogglesSwitchAfterTabFocus();
  void busyButtonDoesNotInterceptApplicationShortcut();
  void busyIndicatorPresentationDefaultsToInline();
  void isolatedBusyIndicatorAdvancesOnWindowsSurface();
  void loadingSwitchDoesNotInterceptApplicationShortcut();
};

void KeyboardShortcutTests::buttonUsesTabFocusByDefault() {
  adqt::widgets::AdButton button;
  QCOMPARE(button.focusPolicy(), Qt::TabFocus);
}

void KeyboardShortcutTests::enterActivatesButtonAfterTabFocus_data() {
  QTest::addColumn<int>("key");
  QTest::newRow("return") << static_cast<int>(Qt::Key_Return);
  QTest::newRow("keypad enter") << static_cast<int>(Qt::Key_Enter);
}

void KeyboardShortcutTests::enterActivatesButtonAfterTabFocus() {
  QFETCH(int, key);

  QWidget window;
  QVBoxLayout layout(&window);
  auto* initialFocus = new QLineEdit(&window);
  auto* button = new adqt::widgets::AdButton(QStringLiteral("Activate"), &window);
  layout.addWidget(initialFocus);
  layout.addWidget(button);
  QSignalSpy clicked(button, &QAbstractButton::clicked);

  window.show();
  QTest::qWait(10);
  focusWithTab(initialFocus, button);

  QTest::keyClick(button, static_cast<Qt::Key>(key));
  QCOMPARE(clicked.count(), 1);
  QVERIFY(!button->isDown());
}

void KeyboardShortcutTests::enterTogglesSwitchAfterTabFocus_data() {
  QTest::addColumn<int>("key");
  QTest::newRow("return") << static_cast<int>(Qt::Key_Return);
  QTest::newRow("keypad enter") << static_cast<int>(Qt::Key_Enter);
}

void KeyboardShortcutTests::enterTogglesSwitchAfterTabFocus() {
  QFETCH(int, key);

  QWidget window;
  QVBoxLayout layout(&window);
  auto* initialFocus = new QLineEdit(&window);
  auto* control = new adqt::widgets::AdSwitch(&window);
  layout.addWidget(initialFocus);
  layout.addWidget(control);
  QSignalSpy toggled(control, &QAbstractButton::toggled);

  window.show();
  QTest::qWait(10);
  focusWithTab(initialFocus, control);

  QTest::keyClick(control, static_cast<Qt::Key>(key));
  QVERIFY(control->isChecked());
  QCOMPARE(toggled.count(), 1);
  QVERIFY(!control->isDown());
}

void KeyboardShortcutTests::busyButtonDoesNotInterceptApplicationShortcut() {
  QWidget window;
  QVBoxLayout layout(&window);
  auto* initialFocus = new QLineEdit(&window);
  auto* button = new adqt::widgets::AdButton(QStringLiteral("Busy"), &window);
  button->setBusy(true);
  layout.addWidget(initialFocus);
  layout.addWidget(button);

  QShortcut shortcut(QKeySequence(Qt::CTRL | Qt::Key_K), &window);
  shortcut.setContext(Qt::ApplicationShortcut);
  QSignalSpy activated(&shortcut, &QShortcut::activated);

  window.show();
  QTest::qWait(10);
  focusWithTab(initialFocus, button);

  QTest::keyClick(button, Qt::Key_K, Qt::ControlModifier);
  QTRY_COMPARE(activated.count(), 1);
}

void KeyboardShortcutTests::busyIndicatorPresentationDefaultsToInline() {
  adqt::widgets::AdButton button;
  QCOMPARE(button.busyIndicatorPresentation(),
           adqt::widgets::AdButton::BusyIndicatorPresentation::Inline);
}

void KeyboardShortcutTests::isolatedBusyIndicatorAdvancesOnWindowsSurface() {
  QWidget window;
  QVBoxLayout layout(&window);
  auto* button = new adqt::widgets::AdButton(QStringLiteral("Busy"), &window);
  button->setBusyIndicatorPresentation(
      adqt::widgets::AdButton::BusyIndicatorPresentation::IsolatedSurface);
  layout.addWidget(button);
  window.show();
  button->setBusy(true);
  QTest::qWait(120);

  if (QGuiApplication::platformName().compare(QStringLiteral("windows"), Qt::CaseInsensitive) ==
      0) {
    QVERIFY(button->busyIndicatorFrameCount() >= 3);
  } else {
    QCOMPARE(button->busyIndicatorFrameCount(), quint64(0));
  }
  QVERIFY(button->busy());
}

void KeyboardShortcutTests::loadingSwitchDoesNotInterceptApplicationShortcut() {
  QWidget window;
  QVBoxLayout layout(&window);
  auto* initialFocus = new QLineEdit(&window);
  auto* control = new adqt::widgets::AdSwitch(&window);
  control->setLoading(true);
  layout.addWidget(initialFocus);
  layout.addWidget(control);

  QShortcut shortcut(QKeySequence(Qt::CTRL | Qt::Key_K), &window);
  shortcut.setContext(Qt::ApplicationShortcut);
  QSignalSpy activated(&shortcut, &QShortcut::activated);

  window.show();
  QTest::qWait(10);
  focusWithTab(initialFocus, control);

  QTest::keyClick(control, Qt::Key_K, Qt::ControlModifier);
  QTRY_COMPARE(activated.count(), 1);
}

QTEST_MAIN(KeyboardShortcutTests)

#include "keyboard_shortcut_tests.moc"
