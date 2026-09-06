#include <QAbstractButton>
#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <cmath>

#include "theme/theme.h"
#include "widgets/notification.h"

using adqt::theme::ThemeConfig;
using adqt::theme::ThemeManager;
using adqt::widgets::AdNotification;
using adqt::widgets::AdNotificationHandle;
using adqt::widgets::AdNotificationService;

namespace {

void showOwner(QWidget* owner) {
  owner->resize(960, 640);
  owner->show();
  QVERIFY(QTest::qWaitForWindowExposed(owner));
  QCoreApplication::processEvents();
}

AdNotification::Request persistentRequest(const QString& title) {
  AdNotification::Request request;
  request.title = title;
  request.description = QStringLiteral("Notification description");
  request.durationMs = 0;
  return request;
}

QList<QWidget*> visibleNotices(QWidget* owner) {
  QList<QWidget*> result;
  const auto notices = owner->findChildren<QWidget*>(QStringLiteral("ad-notification-notice"));
  for (QWidget* notice : notices) {
    if (notice->isVisible()) {
      result.append(notice);
    }
  }
  return result;
}

}  // namespace

class NotificationTest final : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    qRegisterMetaType<AdNotification::Type>();
    qRegisterMetaType<AdNotification::Placement>();
    qRegisterMetaType<AdNotification::AccessibilityRole>();
    qRegisterMetaType<AdNotification::CloseReason>();
    originalTheme_ = ThemeManager::instance().config();
  }

  void init() {
    ThemeConfig config = originalTheme_;
    config.motion = false;
    ThemeManager::instance().setConfig(config);
  }

  void cleanup() {
    AdNotificationService::destroyAll();
    AdNotificationService::setConfig({});
    ThemeManager::instance().setConfig(originalTheme_);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
  }

  void typedOpenRendersAntStructureAndCloses() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    AdNotification::Request request = persistentRequest(QStringLiteral("Saved"));
    request.type = AdNotification::Type::Success;
    bool closed = false;
    request.onClose = [&closed]() { closed = true; };

    AdNotificationHandle* handle = notifications.open(request);
    QVERIFY(handle);
    QCOMPARE(handle->type(), AdNotification::Type::Success);
    QCOMPARE(handle->title(), QStringLiteral("Saved"));
    QCOMPARE(handle->description(), QStringLiteral("Notification description"));
    QCOMPARE(notifications.count(), 1);
    QWidget* notice = handle->noticeWidget();
    QVERIFY(notice);
    QVERIFY(notice->findChild<QLabel*>(QStringLiteral("ad-notification-title")));
    QVERIFY(notice->findChild<QLabel*>(QStringLiteral("ad-notification-description")));
    QVERIFY(notice->findChild<QWidget*>(QStringLiteral("ad-notification-default-icon")));
    QAbstractButton* close =
        notice->findChild<QAbstractButton*>(QStringLiteral("ad-notification-close"));
    QVERIFY(close);
    QVERIFY(close->focusPolicy() != Qt::NoFocus);
    QVERIFY(notice->width() >= 384);
    QVERIFY(notice->geometry().center().x() > owner.width() / 2);
    QVERIFY(notice->geometry().center().y() < owner.height() / 2);

    QSignalSpy closeSpy(handle, &AdNotificationHandle::closed);
    QTest::mouseClick(close, Qt::LeftButton);
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(qvariant_cast<AdNotification::CloseReason>(closeSpy.at(0).at(0)),
             AdNotification::CloseReason::Manual);
    QVERIFY(closed);
    QCOMPARE(notifications.count(), 0);
  }

  void keyedNotificationUpdatesInPlace() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    AdNotification::Request first = persistentRequest(QStringLiteral("Loading"));
    first.key = QStringLiteral("update-key");
    first.type = AdNotification::Type::Info;
    AdNotificationHandle* handle = notifications.open(first);
    QWidget* originalNotice = handle->noticeWidget();

    AdNotification::Request second = persistentRequest(QStringLiteral("Complete"));
    second.description = QStringLiteral("The operation completed successfully.");
    second.key = first.key;
    second.type = AdNotification::Type::Success;
    AdNotificationHandle* updated = notifications.open(second);

    QCOMPARE(updated, handle);
    QCOMPARE(updated->noticeWidget(), originalNotice);
    QCOMPARE(updated->title(), second.title);
    QCOMPARE(updated->description(), second.description);
    QCOMPARE(updated->type(), AdNotification::Type::Success);
    QCOMPARE(notifications.count(), 1);
  }

  void stringOverloadsProvideTheCommonQtApi() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);

    AdNotificationHandle* local =
        notifications.success(QStringLiteral("Saved"), QStringLiteral("Preferences updated."), 0);
    QVERIFY(local);
    QCOMPARE(local->type(), AdNotification::Type::Success);
    QCOMPARE(local->title(), QStringLiteral("Saved"));
    QCOMPARE(local->description(), QStringLiteral("Preferences updated."));

    AdNotificationHandle* shared = AdNotificationService::info(
        QStringLiteral("Background sync"), QStringLiteral("Sync is still running."), 0, &owner);
    QVERIFY(shared);
    QCOMPARE(shared->type(), AdNotification::Type::Info);
    QCOMPARE(shared->title(), QStringLiteral("Background sync"));
  }

  void configurationIsNormalizedConsistently() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    AdNotification::Config invalid;
    invalid.topOffset = -10;
    invalid.bottomOffset = -20;
    invalid.defaultDurationMs = -30;
    invalid.maximumCount = -4;
    invalid.stackThreshold = 0;

    notifications.setConfig(invalid);
    const AdNotification::Config local = notifications.config();
    QCOMPARE(local.topOffset, 0);
    QCOMPARE(local.bottomOffset, 0);
    QCOMPARE(local.defaultDurationMs, 0);
    QCOMPARE(local.maximumCount, 0);
    QCOMPARE(local.stackThreshold, 1);

    AdNotificationService::setConfig(invalid);
    const AdNotification::Config shared = AdNotificationService::config();
    QCOMPARE(shared.topOffset, 0);
    QCOMPARE(shared.bottomOffset, 0);
    QCOMPARE(shared.defaultDurationMs, 0);
    QCOMPARE(shared.maximumCount, 0);
    QCOMPARE(shared.stackThreshold, 1);
  }

  void compactOwnerKeepsNoticeInItsClientArea() {
    QWidget owner;
    owner.resize(260, 220);
    owner.show();
    QVERIFY(QTest::qWaitForWindowExposed(&owner));
    QCoreApplication::processEvents();

    AdNotification notifications(&owner);
    AdNotification::Config config = notifications.config();
    config.topOffset = 10000;
    notifications.setConfig(config);
    AdNotification::Request request = persistentRequest(QStringLiteral("Compact window"));
    request.placement = AdNotification::Placement::TopRight;
    request.componentTokens.width = 500;
    request.componentTokens.edgeMargin = 500;
    AdNotificationHandle* handle = notifications.open(request);

    QVERIFY(handle && handle->noticeWidget());
    const QRect geometry = handle->noticeWidget()->geometry();
    QVERIFY(geometry.width() <= owner.width());
    QVERIFY(owner.rect().contains(geometry.center()));
  }

  void supportsAllSixPlacements() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    const QList<AdNotification::Placement> placements = {
        AdNotification::Placement::Top,        AdNotification::Placement::TopLeft,
        AdNotification::Placement::TopRight,   AdNotification::Placement::Bottom,
        AdNotification::Placement::BottomLeft, AdNotification::Placement::BottomRight};
    for (AdNotification::Placement placement : placements) {
      AdNotification::Request request = persistentRequest(QStringLiteral("Placed"));
      request.placement = placement;
      AdNotificationHandle* handle = notifications.open(request);
      QVERIFY(handle && handle->noticeWidget());
      const QPoint center = handle->noticeWidget()->geometry().center();
      if (placement == AdNotification::Placement::Top ||
          placement == AdNotification::Placement::Bottom) {
        // The outer widget includes Ant's asymmetric popup-shadow margins.
        QVERIFY(std::abs(center.x() - owner.width() / 2) < 24);
      } else if (placement == AdNotification::Placement::TopLeft ||
                 placement == AdNotification::Placement::BottomLeft) {
        QVERIFY(center.x() < owner.width() / 2);
      } else {
        QVERIFY(center.x() > owner.width() / 2);
      }
      if (placement == AdNotification::Placement::Top ||
          placement == AdNotification::Placement::TopLeft ||
          placement == AdNotification::Placement::TopRight) {
        QVERIFY(center.y() < owner.height() / 2);
      } else {
        QVERIFY(center.y() > owner.height() / 2);
      }
      handle->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
  }

  void maximumCountEvictsOldest() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    notifications.setMaximumCount(2);
    AdNotificationHandle* first = notifications.open(persistentRequest(QStringLiteral("One")));
    QSignalSpy closed(first, &AdNotificationHandle::closed);
    notifications.open(persistentRequest(QStringLiteral("Two")));
    notifications.open(persistentRequest(QStringLiteral("Three")));
    QCOMPARE(notifications.count(), 2);
    QCOMPARE(closed.count(), 1);
    QCOMPARE(qvariant_cast<AdNotification::CloseReason>(closed.at(0).at(0)),
             AdNotification::CloseReason::MaxCount);
  }

  void stackCollapsesAndExpandsOnHover() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    AdNotification::Config config = notifications.config();
    config.stackEnabled = true;
    config.stackThreshold = 2;
    notifications.setConfig(config);
    AdNotificationHandle* latest = nullptr;
    for (int i = 0; i < 4; ++i) {
      latest = notifications.open(persistentRequest(QStringLiteral("Item %1").arg(i + 1)));
    }
    QCOMPARE(notifications.count(), 4);
    QCOMPARE(visibleNotices(&owner).size(), 2);
    QVERIFY(latest && latest->noticeWidget());
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(latest->noticeWidget(), &enter);
    QCoreApplication::processEvents();
    QCOMPARE(visibleNotices(&owner).size(), 4);
  }

  void adoptsCustomContentAndActions() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    auto* title = new QLabel(QStringLiteral("Custom title"));
    auto* description = new QLabel(QStringLiteral("Custom description"));
    auto* actions = new QPushButton(QStringLiteral("Undo"));
    AdNotification::Request request = persistentRequest(QString());
    request.description.clear();
    request.titleWidget = title;
    request.descriptionWidget = description;
    request.actionsWidget = actions;
    AdNotificationHandle* handle = notifications.open(request);
    QVERIFY(handle);
    QCOMPARE(title->parentWidget()->objectName(), QStringLiteral("ad-notification-title-host"));
    QCOMPARE(description->parentWidget()->objectName(),
             QStringLiteral("ad-notification-description-host"));
    QCOMPARE(actions->parentWidget()->objectName(), QStringLiteral("ad-notification-actions"));
    QVERIFY(title->isVisible());
    QVERIFY(description->isVisible());
    QVERIFY(actions->isVisible());
  }

  void customCloseContentKeepsNativeButtonSemantics() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    auto* closeContent = new QLabel(QStringLiteral("x"));
    bool closeButtonCallbackCalled = false;
    AdNotification::Request request = persistentRequest(QStringLiteral("Keyboard close"));
    request.closeIconWidget = closeContent;
    request.onCloseButton = [&closeButtonCallbackCalled]() { closeButtonCallbackCalled = true; };
    AdNotificationHandle* handle = notifications.open(request);
    QVERIFY(handle && handle->noticeWidget());

    QAbstractButton* closeButton = handle->noticeWidget()->findChild<QAbstractButton*>(
        QStringLiteral("ad-notification-close"));
    QVERIFY(closeButton);
    QCOMPARE(closeContent->parentWidget(), closeButton);
    QCOMPARE(closeContent->focusPolicy(), Qt::NoFocus);
    QVERIFY(closeContent->testAttribute(Qt::WA_TransparentForMouseEvents));
    QVERIFY(closeButton->focusPolicy() != Qt::NoFocus);
    QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(closeButton);
    QVERIFY(interface);
    QCOMPARE(interface->role(), QAccessible::Button);

    closeButton->clearFocus();
    QCoreApplication::processEvents();
    const QImage unfocused = closeButton->grab().toImage();
    closeButton->setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    QVERIFY(closeButton->hasFocus());
    const QImage keyboardFocused = closeButton->grab().toImage();
    QVERIFY(unfocused != keyboardFocused);

    QSignalSpy closed(handle, &AdNotificationHandle::closed);
    QTest::keyClick(closeButton, Qt::Key_Space);
    QCOMPARE(closed.count(), 1);
    QVERIFY(closeButtonCallbackCalled);
  }

  void timeoutAndHoverPauseFollowRequestPolicy() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    AdNotification::Request request = persistentRequest(QStringLiteral("Pause me"));
    request.durationMs = 120;
    request.pauseOnHover = true;
    request.showProgress = true;
    AdNotificationHandle* handle = notifications.open(request);
    QSignalSpy closed(handle, &AdNotificationHandle::closed);
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(handle->noticeWidget(), &enter);
    QTest::qWait(180);
    QCOMPARE(closed.count(), 0);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(handle->noticeWidget(), &leave);
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 400);
    QCOMPARE(qvariant_cast<AdNotification::CloseReason>(closed.at(0).at(0)),
             AdNotification::CloseReason::Timeout);
  }

  void managerDestructionFinalizesAnAnimatingClose() {
    ThemeConfig theme = originalTheme_;
    theme.motion = true;
    ThemeManager::instance().setConfig(theme);

    QWidget owner;
    showOwner(&owner);
    auto* notifications = new AdNotification(&owner);
    AdNotificationHandle* handle =
        notifications->open(persistentRequest(QStringLiteral("Closing")));
    QVERIFY(handle);
    QSignalSpy closed(handle, &AdNotificationHandle::closed);

    handle->close();
    QVERIFY(!handle->isOpen());
    delete notifications;

    QCOMPARE(closed.count(), 1);
    QCOMPARE(qvariant_cast<AdNotification::CloseReason>(closed.at(0).at(0)),
             AdNotification::CloseReason::OwnerDestroyed);
  }

  void globalServiceIsScopedToTheTopLevelWindow() {
    QWidget owner;
    showOwner(&owner);
    QWidget child(&owner);
    AdNotification* fromOwner = AdNotificationService::instance(&owner);
    AdNotification* fromChild = AdNotificationService::instance(&child);
    QVERIFY(fromOwner);
    QCOMPARE(fromChild, fromOwner);

    AdNotification::Config config;
    config.maximumCount = 1;
    config.defaultPlacement = AdNotification::Placement::BottomRight;
    AdNotificationService::setConfig(config);
    QCOMPARE(fromOwner->maximumCount(), 1);
    AdNotificationHandle* first =
        AdNotificationService::info(persistentRequest(QStringLiteral("First")), &child);
    AdNotificationHandle* second =
        AdNotificationService::success(persistentRequest(QStringLiteral("Second")), &owner);
    QVERIFY(first);
    QVERIFY(second);
    QCOMPARE(fromOwner->count(), 1);
    QCOMPARE(second->noticeWidget()->geometry().center().y() > owner.height() / 2, true);
  }

  void statusRoleMapsToQtAccessibility() {
    QWidget owner;
    showOwner(&owner);
    AdNotification notifications(&owner);
    AdNotification::Request request = persistentRequest(QStringLiteral("Background sync"));
    request.accessibilityRole = AdNotification::AccessibilityRole::Status;
    AdNotificationHandle* handle = notifications.open(request);
    QVERIFY(handle && handle->noticeWidget());
    QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(handle->noticeWidget());
    QVERIFY(interface);
    QCOMPARE(interface->role(), QAccessible::StaticText);
  }

  void renderLightAndDarkSnapshots() {
    const QString snapshotDirectory = qEnvironmentVariable("ADQT_NOTIFICATION_SNAPSHOT_DIR");
    auto renderScheme = [&](adqt::theme::ThemeScheme scheme, const QString& fileName) {
      ThemeConfig theme = originalTheme_;
      theme.scheme = scheme;
      theme.motion = false;
      ThemeManager::instance().setConfig(theme);

      QWidget owner;
      owner.setObjectName(QStringLiteral("notification-snapshot-owner"));
      owner.resize(960, 640);
      showOwner(&owner);
      AdNotification notifications(&owner);

      AdNotification::Request success = persistentRequest(QStringLiteral("Update complete"));
      success.description = QStringLiteral("Your settings have been saved successfully.");
      success.type = AdNotification::Type::Success;
      success.showProgress = true;
      success.durationMs = 10000;
      notifications.open(success);

      AdNotification::Request info = persistentRequest(QStringLiteral("New activity"));
      info.description = QStringLiteral("A new comment was added to the project.");
      info.type = AdNotification::Type::Info;
      info.durationMs = 0;
      notifications.open(info);

      AdNotification::Request warning = persistentRequest(QStringLiteral("Connection unstable"));
      warning.description = QStringLiteral("Changes will sync when the connection recovers.");
      warning.type = AdNotification::Type::Warning;
      warning.placement = AdNotification::Placement::BottomLeft;
      warning.durationMs = 0;
      notifications.open(warning);
      QCoreApplication::processEvents();

      const QImage image = owner.grab().toImage().convertToFormat(QImage::Format_ARGB32);
      QVERIFY(!image.isNull());
      if (!snapshotDirectory.isEmpty()) {
        QDir().mkpath(snapshotDirectory);
        QVERIFY(image.save(QDir(snapshotDirectory).filePath(fileName)));
      }
    };
    renderScheme(adqt::theme::ThemeScheme::Light, QStringLiteral("notification-light.png"));
    renderScheme(adqt::theme::ThemeScheme::Dark, QStringLiteral("notification-dark.png"));
  }

 private:
  ThemeConfig originalTheme_;
};

QTEST_MAIN(NotificationTest)

#include "tst_notification.moc"
