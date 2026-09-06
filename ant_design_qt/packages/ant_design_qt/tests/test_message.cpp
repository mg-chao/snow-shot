#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <cmath>

#include "theme/theme.h"
#include "widgets/message.h"

using adqt::theme::ThemeConfig;
using adqt::theme::ThemeManager;
using adqt::widgets::AdMessage;
using adqt::widgets::AdMessageHandle;
using adqt::widgets::AdMessageService;

namespace {

void showOwner(QWidget* owner) {
  owner->resize(720, 480);
  owner->show();
  QVERIFY(QTest::qWaitForWindowExposed(owner));
  QCoreApplication::processEvents();
}

AdMessage::Request persistentRequest(const QString& content,
                                     AdMessage::Type type = AdMessage::Type::Info) {
  AdMessage::Request request;
  request.type = type;
  request.content = content;
  request.durationMs = 0;
  return request;
}

}  // namespace

class MessageTest final : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    qRegisterMetaType<AdMessage::Type>();
    qRegisterMetaType<AdMessage::CloseReason>();
    originalTheme_ = ThemeManager::instance().config();
  }

  void init() {
    ThemeConfig config = originalTheme_;
    config.motion = false;
    ThemeManager::instance().setConfig(config);
  }

  void cleanup() {
    adqt::widgets::AdMessageService::destroyAll();
    adqt::widgets::AdMessageService::setConfig({});
    ThemeManager::instance().setConfig(originalTheme_);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
  }

  void typedOpenAndManualClose() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    bool closeCallbackCalled = false;
    AdMessage::Request request =
        persistentRequest(QStringLiteral("Saved"), AdMessage::Type::Success);
    request.onClose = [&closeCallbackCalled]() { closeCallbackCalled = true; };
    AdMessageHandle* handle = messages.open(request);

    QVERIFY(handle);
    QCOMPARE(messages.count(), 1);
    QCOMPARE(handle->type(), AdMessage::Type::Success);
    QCOMPARE(handle->content(), QStringLiteral("Saved"));
    QVERIFY(handle->noticeWidget());
    QVERIFY(handle->noticeWidget()->isVisible());
    QWidget* defaultIcon =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-default-icon"));
    QVERIFY(defaultIcon);
    QVERIFY(defaultIcon->isVisible());
    QWidget* iconHost =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-icon"));
    QWidget* contentHost =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-content-host"));
    QVERIFY(iconHost);
    QVERIFY(contentHost);
    QVERIFY(contentHost->geometry().left() - iconHost->geometry().right() - 1 >= 8);
    QVERIFY(contentHost->height() >= 22);

    QSignalSpy closedSpy(handle, &AdMessageHandle::closed);
    QPointer<AdMessageHandle> guard = handle;
    handle->close();

    QCOMPARE(messages.count(), 0);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(qvariant_cast<AdMessage::CloseReason>(closedSpy.at(0).at(0)),
             AdMessage::CloseReason::Manual);
    QVERIFY(closeCallbackCalled);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(guard.isNull());
  }

  void keyedMessageUpdatesInPlace() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    AdMessage::Request loading =
        persistentRequest(QStringLiteral("Loading..."), AdMessage::Type::Loading);
    loading.key = QStringLiteral("job");
    AdMessageHandle* first = messages.open(loading);
    QVERIFY(first);

    AdMessage::Request loaded =
        persistentRequest(QStringLiteral("Loaded!"), AdMessage::Type::Success);
    loaded.key = QStringLiteral("job");
    AdMessageHandle* second = messages.open(loaded);

    QCOMPARE(second, first);
    QCOMPARE(messages.count(), 1);
    QCOMPARE(first->type(), AdMessage::Type::Success);
    QCOMPARE(first->content(), QStringLiteral("Loaded!"));

    messages.destroy(QStringLiteral("job"));
    QCOMPARE(messages.count(), 0);
  }

  void maximumCountEvictsOldest() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);
    messages.setMaximumCount(2);

    AdMessageHandle* first = messages.info(persistentRequest(QStringLiteral("First")));
    QVERIFY(first);
    QSignalSpy firstClosed(first, &AdMessageHandle::closed);
    messages.info(persistentRequest(QStringLiteral("Second")));
    messages.info(persistentRequest(QStringLiteral("Third")));

    QCOMPARE(messages.count(), 2);
    QCOMPARE(firstClosed.count(), 1);
    QCOMPARE(qvariant_cast<AdMessage::CloseReason>(firstClosed.at(0).at(0)),
             AdMessage::CloseReason::MaxCount);
  }

  void timeoutAndPauseOnHover() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    AdMessage::Request request;
    request.type = AdMessage::Type::Info;
    request.content = QStringLiteral("Hover me");
    request.durationMs = 80;
    request.pauseOnHover = true;
    AdMessageHandle* handle = messages.open(request);
    QVERIFY(handle);
    QWidget* notice = handle->noticeWidget();
    QVERIFY(notice);

    QEvent enterEvent(QEvent::Enter);
    QCoreApplication::sendEvent(notice, &enterEvent);
    QTest::qWait(130);
    QCOMPARE(messages.count(), 1);

    QSignalSpy closedSpy(handle, &AdMessageHandle::closed);
    QEvent leaveEvent(QEvent::Leave);
    QCoreApplication::sendEvent(notice, &leaveEvent);
    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 0, 500);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(qvariant_cast<AdMessage::CloseReason>(closedSpy.at(0).at(0)),
             AdMessage::CloseReason::Timeout);
  }

  void timeoutKeepsRunningWhenHoverPauseIsDisabled() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    AdMessage::Request request;
    request.type = AdMessage::Type::Info;
    request.content = QStringLiteral("No pause");
    request.durationMs = 70;
    request.pauseOnHover = false;
    AdMessageHandle* handle = messages.open(request);
    QVERIFY(handle);
    QSignalSpy closedSpy(handle, &AdMessageHandle::closed);
    QEvent enterEvent(QEvent::Enter);
    QCoreApplication::sendEvent(handle->noticeWidget(), &enterEvent);

    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 0, 500);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(qvariant_cast<AdMessage::CloseReason>(closedSpy.at(0).at(0)),
             AdMessage::CloseReason::Timeout);
  }

  void runtimeHoverConfigurationUpdatesActiveTimer() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);
    messages.setPauseOnHover(false);

    AdMessage::Request request;
    request.type = AdMessage::Type::Info;
    request.content = QStringLiteral("Runtime hover policy");
    request.durationMs = 180;
    AdMessageHandle* handle = messages.open(request);
    QVERIFY(handle);

    QEvent enterEvent(QEvent::Enter);
    QCoreApplication::sendEvent(handle->noticeWidget(), &enterEvent);
    QTest::qWait(50);
    messages.setPauseOnHover(true);
    QTest::qWait(220);
    QCOMPARE(messages.count(), 1);

    QSignalSpy closedSpy(handle, &AdMessageHandle::closed);
    messages.setPauseOnHover(false);
    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 0, 500);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(qvariant_cast<AdMessage::CloseReason>(closedSpy.at(0).at(0)),
             AdMessage::CloseReason::Timeout);
  }

  void clickAndCloseCallbacksRun() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    int clicks = 0;
    int closes = 0;
    AdMessage::Request request = persistentRequest(QStringLiteral("Clickable"));
    request.onClick = [&clicks]() { ++clicks; };
    request.onClose = [&closes]() { ++closes; };
    AdMessageHandle* handle = messages.open(request);
    QVERIFY(handle);
    QSignalSpy clickSpy(handle, &AdMessageHandle::clicked);

    QTest::mouseClick(handle->noticeWidget(), Qt::LeftButton, Qt::NoModifier,
                      handle->noticeWidget()->rect().center());
    QCOMPARE(clickSpy.count(), 1);
    QCOMPARE(clicks, 1);

    handle->close();
    QCOMPARE(closes, 1);
  }

  void closeCallbackCanDeleteTheMessageInstance() {
    QWidget owner;
    showOwner(&owner);
    auto* messages = new AdMessage(&owner);
    QPointer<AdMessage> messagesGuard = messages;

    AdMessage::Request request = persistentRequest(QStringLiteral("Delete on close"));
    request.onClose = [&messages]() {
      delete messages;
      messages = nullptr;
    };
    AdMessageHandle* handle = messages->open(request);
    QVERIFY(handle);
    QPointer<AdMessageHandle> handleGuard = handle;

    handle->close();
    QVERIFY(messagesGuard.isNull());
    QVERIFY(handleGuard.isNull());
  }

  void customContentIconAndSemanticStyles() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    auto* customContent = new QLabel(QStringLiteral("Custom content"));
    customContent->setAccessibleName(QStringLiteral("Custom accessible content"));
    auto* customIcon = new QLabel(QStringLiteral("C"));
    AdMessage::Request request = persistentRequest(QString(), AdMessage::Type::Success);
    request.contentWidget = customContent;
    request.iconWidget = customIcon;
    request.semanticStyles.content.textColor = QColor(QStringLiteral("#d4380d"));
    AdMessageHandle* handle = messages.open(request);

    QVERIFY(handle);
    QCOMPARE(handle->contentWidget(), customContent);
    QVERIFY(customContent->parentWidget());
    QVERIFY(customIcon->parentWidget());
    QWidget* contentHost =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-content-host"));
    QVERIFY(contentHost);
    QCOMPARE(contentHost->palette().color(QPalette::WindowText), QColor(QStringLiteral("#d4380d")));

    QPointer<QLabel> contentGuard = customContent;
    handle->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(contentGuard.isNull());
  }

  void explicitInvalidIconHidesTypeIcon() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    AdMessage::Request request = persistentRequest(QStringLiteral("No icon"));
    request.icon = adqt::icons::IconRef{};
    AdMessageHandle* handle = messages.open(request);
    QVERIFY(handle);
    QWidget* iconHost =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-icon"));
    QVERIFY(iconHost);
    QVERIFY(!iconHost->isVisible());

    AdMessageHandle* plain = messages.open(QStringLiteral("Untyped message"), 0);
    QVERIFY(plain);
    QWidget* plainIconHost =
        plain->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-icon"));
    QVERIFY(plainIconHost);
    QVERIFY(!plainIconHost->isVisible());
  }

  void loadingIconRendersACompleteSpinnerWithoutClipping() {
    ThemeConfig config = originalTheme_;
    config.motion = true;
    ThemeManager::instance().setConfig(config);

    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);

    AdMessageHandle* handle =
        messages.loading(persistentRequest(QStringLiteral("Loading"), AdMessage::Type::Loading));
    QVERIFY(handle);
    QWidget* spinner =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-default-icon"));
    QVERIFY(spinner);

    const auto isStatusBlue = [](QRgb pixel) {
      return qBlue(pixel) > qGreen(pixel) + 24 && qBlue(pixel) > qRed(pixel) + 48;
    };

    // Sample just over one whole cycle. The regression appeared only when the arc had
    // rotated into the upper-left corner, so a static frame cannot catch it.
    constexpr int kFrameCount = 34;
    constexpr int kFrameWaitMs = 30;
    for (int frame = 0; frame < kFrameCount; ++frame) {
      QTest::qWait(kFrameWaitMs);
      const QImage image = spinner->grab().toImage().convertToFormat(QImage::Format_ARGB32);
      QVERIFY(!image.isNull());

      bool hasStatusBlue = false;
      bool touchesOuterEdge = false;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          if (!isStatusBlue(image.pixel(x, y))) {
            continue;
          }
          hasStatusBlue = true;
          touchesOuterEdge = touchesOuterEdge || x == 0 || y == 0 || x == image.width() - 1 ||
                             y == image.height() - 1;
        }
      }
      QVERIFY(hasStatusBlue);
      QVERIFY(!touchesOuterEdge);
    }
  }

  void messagesPreferNaturalWidthBeforeWrapping() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);
    AdMessageHandle* handle =
        messages.success(persistentRequest(QStringLiteral("The operation completed successfully")));
    QVERIFY(handle);
    QVERIFY2(handle->noticeWidget()->width() > 280,
             "Ordinary message text should remain on one line when the window has room");
  }

  void longMessageWrapsWithinNarrowOwner() {
    QWidget owner;
    owner.resize(320, 360);
    owner.show();
    QVERIFY(QTest::qWaitForWindowExposed(&owner));
    QCoreApplication::processEvents();

    const QString content = QStringLiteral(
        "This is a longer notification that should wrap cleanly inside a narrow application "
        "window.");
    AdMessage messages(&owner);
    AdMessageHandle* handle = messages.info(persistentRequest(content));
    QVERIFY(handle);
    QWidget* notice = handle->noticeWidget();
    QVERIFY(notice);
    QLabel* label = notice->findChild<QLabel*>(QStringLiteral("ad-message-content"));
    QVERIFY(label);
    QCoreApplication::processEvents();

    QVERIFY(label->wordWrap());
    QVERIFY2(label->height() > 22, "Long narrow content should occupy multiple text lines");
    QVERIFY(notice->width() <= owner.width() - 16);
    QVERIFY(notice->geometry().left() >= 0);
    QVERIFY(notice->geometry().right() < owner.width());

    const QImage image = owner.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!image.isNull());
    QSet<QRgb> sampledColors;
    for (int y = 0; y < image.height(); y += 3) {
      const QRgb* scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(y));
      for (int x = 0; x < image.width(); x += 3) {
        sampledColors.insert(scanLine[x]);
      }
    }
    QVERIFY2(sampledColors.size() > 12,
             "Narrow render should contain the surface, icon, and wrapped text");

    const QString snapshotDirectory = qEnvironmentVariable("ADQT_MESSAGE_SNAPSHOT_DIR");
    if (!snapshotDirectory.isEmpty()) {
      QVERIFY(QDir().mkpath(snapshotDirectory));
      QVERIFY(image.save(QDir(snapshotDirectory).filePath(QStringLiteral("message-narrow.png"))));
    }
  }

  void rtlPlacesTheStatusIconAfterContent() {
    QWidget owner;
    owner.setLayoutDirection(Qt::RightToLeft);
    showOwner(&owner);
    AdMessage messages(&owner);
    AdMessageHandle* handle = messages.info(persistentRequest(QStringLiteral("RTL message")));
    QVERIFY(handle);
    QWidget* icon = handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-icon"));
    QWidget* content =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-content-host"));
    QVERIFY(icon);
    QVERIFY(content);
    QVERIFY2(icon->geometry().left() > content->geometry().left(),
             "The inherited Qt layout direction should mirror icon/content ordering");
  }

  void stackIsTopCenteredAndTracksResize() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);
    messages.setTopOffset(24);
    AdMessageHandle* handle = messages.info(persistentRequest(QStringLiteral("Positioned")));
    QVERIFY(handle);
    QWidget* notice = handle->noticeWidget();
    QVERIFY(notice);

    const int firstCenter = notice->geometry().center().x();
    QVERIFY(std::abs(firstCenter - owner.width() / 2) <= 1);
    QVERIFY(notice->geometry().top() >= 0);

    owner.resize(940, 520);
    QCoreApplication::processEvents();
    const int resizedCenter = notice->geometry().center().x();
    QVERIFY(std::abs(resizedCenter - owner.width() / 2) <= 1);
  }

  void destroyAllClosesEveryMessage() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);
    messages.info(persistentRequest(QStringLiteral("One")));
    messages.warning(persistentRequest(QStringLiteral("Two")));
    messages.error(persistentRequest(QStringLiteral("Three")));
    QCOMPARE(messages.count(), 3);

    messages.destroyAll();
    QCOMPARE(messages.count(), 0);
  }

  void animatedCloseEmitsClosedAfterMotionCompletes() {
    ThemeConfig theme = originalTheme_;
    theme.motion = true;
    ThemeManager::instance().setConfig(theme);

    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);
    AdMessageHandle* handle = messages.info(persistentRequest(QStringLiteral("Animated")));
    QVERIFY(handle);
    QPointer<AdMessageHandle> guard = handle;
    QSignalSpy closedSpy(handle, &AdMessageHandle::closed);

    handle->close();
    QCOMPARE(messages.count(), 0);
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(guard);
    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(guard.isNull());
  }

  void hidingOwnerClosesTheScope() {
    QWidget owner;
    showOwner(&owner);
    AdMessage messages(&owner);
    AdMessageHandle* handle = messages.info(persistentRequest(QStringLiteral("Scoped")));
    QVERIFY(handle);
    QSignalSpy closedSpy(handle, &AdMessageHandle::closed);

    owner.hide();
    QCoreApplication::processEvents();
    QCOMPARE(messages.count(), 0);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(qvariant_cast<AdMessage::CloseReason>(closedSpy.at(0).at(0)),
             AdMessage::CloseReason::ScopeHidden);
  }

  void destroyingOwnerClosesExternallyOwnedInstance() {
    auto* owner = new QWidget;
    owner->resize(720, 480);
    AdMessage messages(owner);
    AdMessageHandle* handle = messages.info(persistentRequest(QStringLiteral("Owner lifetime")));
    QVERIFY(handle);
    QSignalSpy closedSpy(handle, &AdMessageHandle::closed);

    delete owner;
    QCOMPARE(messages.count(), 0);
    QVERIFY(messages.ownerWindow() == nullptr);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(qvariant_cast<AdMessage::CloseReason>(closedSpy.at(0).at(0)),
             AdMessage::CloseReason::OwnerDestroyed);
  }

  void staticServiceIsSharedPerTopLevelWindow() {
    QWidget owner;
    showOwner(&owner);
    QWidget child(&owner);
    child.show();

    AdMessage* fromChild = adqt::widgets::AdMessageService::instance(&child);
    AdMessage* fromOwner = adqt::widgets::AdMessageService::instance(&owner);
    QVERIFY(fromChild);
    QCOMPARE(fromChild, fromOwner);

    AdMessage::Config config;
    config.maximumCount = 1;
    config.defaultDurationMs = 0;
    adqt::widgets::AdMessageService::setConfig(config);
    QCOMPARE(fromOwner->maximumCount(), 1);
    adqt::widgets::AdMessageService::info(QStringLiteral("First"), 0, &owner);
    adqt::widgets::AdMessageService::success(QStringLiteral("Second"), 0, &child);
    QCOMPARE(fromOwner->count(), 1);
    adqt::widgets::AdMessageService::destroyAll(&owner);
    QCOMPARE(fromOwner->count(), 0);
  }

  void staticTypedRequestHelpersPreserveRequestOptions() {
    QWidget owner;
    showOwner(&owner);

    AdMessage::Request request = persistentRequest(QStringLiteral("Typed global request"));
    request.key = QStringLiteral("typed-global");
    request.componentTokens.iconContentGap = 14;
    AdMessageHandle* handle = AdMessageService::success(request, &owner);
    QVERIFY(handle);
    QCOMPARE(handle->type(), AdMessage::Type::Success);
    QCOMPARE(handle->key(), QStringLiteral("typed-global"));

    QWidget* iconHost =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-icon"));
    QWidget* contentHost =
        handle->noticeWidget()->findChild<QWidget*>(QStringLiteral("ad-message-content-host"));
    QVERIFY(iconHost);
    QVERIFY(contentHost);
    QCOMPARE(contentHost->geometry().left() - iconHost->geometry().right() - 1, 14);
  }

  void destroyOnlyServiceCallsDoNotCreateAnInstance() {
    QWidget owner;
    showOwner(&owner);
    QCOMPARE(owner.findChildren<AdMessage*>(QString(), Qt::FindDirectChildrenOnly).size(), 0);

    AdMessageService::destroy(QStringLiteral("missing"), &owner);
    AdMessageService::destroyAll(&owner);
    QCOMPARE(owner.findChildren<AdMessage*>(QString(), Qt::FindDirectChildrenOnly).size(), 0);
  }

  void representativeStacksRenderInLightAndDarkThemes() {
    const QString snapshotDirectory = qEnvironmentVariable("ADQT_MESSAGE_SNAPSHOT_DIR");
    if (!snapshotDirectory.isEmpty()) {
      QVERIFY(QDir().mkpath(snapshotDirectory));
    }

    const auto renderScheme = [this, &snapshotDirectory](adqt::theme::ThemeScheme scheme,
                                                         const QString& fileName) {
      ThemeConfig theme = originalTheme_;
      theme.scheme = scheme;
      theme.motion = false;
      ThemeManager::instance().setConfig(theme);

      QWidget owner;
      owner.setObjectName(QStringLiteral("message-render-owner"));
      showOwner(&owner);
      QPalette ownerPalette = owner.palette();
      ownerPalette.setColor(QPalette::Window, scheme == adqt::theme::ThemeScheme::Dark
                                                  ? QColor(QStringLiteral("#141414"))
                                                  : QColor(QStringLiteral("#f5f5f5")));
      owner.setAutoFillBackground(true);
      owner.setPalette(ownerPalette);

      AdMessage messages(&owner);
      messages.info(persistentRequest(QStringLiteral("This is an info message")));
      messages.success(persistentRequest(QStringLiteral("The operation completed successfully")));
      messages.warning(
          persistentRequest(QStringLiteral("Review these settings before continuing")));
      messages.error(persistentRequest(QStringLiteral("The operation could not be completed")));
      messages.loading(persistentRequest(QStringLiteral("Action in progress...")));
      QCoreApplication::processEvents();

      const QImage image = owner.grab().toImage().convertToFormat(QImage::Format_ARGB32);
      QVERIFY(!image.isNull());
      QCOMPARE(image.size(), owner.size() * owner.devicePixelRatioF());

      QSet<QRgb> sampledColors;
      for (int y = 0; y < image.height(); y += 3) {
        const QRgb* scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += 3) {
          sampledColors.insert(scanLine[x]);
        }
      }
      QVERIFY2(sampledColors.size() > 20,
               "Rendered stack should contain text, surfaces, shadows, and status colors");

      if (!snapshotDirectory.isEmpty()) {
        QVERIFY(image.save(QDir(snapshotDirectory).filePath(fileName)));
      }
    };

    renderScheme(adqt::theme::ThemeScheme::Light, QStringLiteral("message-light.png"));
    renderScheme(adqt::theme::ThemeScheme::Dark, QStringLiteral("message-dark.png"));
  }

 private:
  ThemeConfig originalTheme_;
};

QTEST_MAIN(MessageTest)

#include "test_message.moc"
