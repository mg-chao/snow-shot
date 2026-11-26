#include "snow_shot/presentation/screenshotdisplayconfigurationobserver.h"

#include "icon_renderer.h"
#ifdef Q_OS_WIN
#include "../../platform/windows/displayrecoverywindow.h"
#elif defined(Q_OS_MACOS)
#include "../../platform/macos/displaywakeobserver.h"
#endif

#include <QGuiApplication>
#include <QRect>
#include <QScreen>

#include <utility>

ScreenshotDisplayConfigurationObserver::ScreenshotDisplayConfigurationObserver(
    ChangeHandler onChanged, QObject* parent)
    : QObject(parent), m_onChanged(std::move(onChanged)) {}

ScreenshotDisplayConfigurationObserver::~ScreenshotDisplayConfigurationObserver() = default;

void ScreenshotDisplayConfigurationObserver::connectApplicationSignals(
    QGuiApplication* application) {
    if (application == nullptr) {
        return;
    }
#ifdef Q_OS_WIN
    m_recoveryWindow = std::make_unique<snow_shot::platform::windows::DisplayRecoveryWindow>(
        [this] { notifyChanged(Change::Recovery); });
#elif defined(Q_OS_MACOS)
    if (QGuiApplication::platformName() == u"cocoa") {
        m_displayWakeObserver = snow_shot::platform::macos::observeDisplayWake(
            [this] { notifyChanged(Change::Recovery); });
    }
#endif

    QObject::connect(application, &QGuiApplication::screenAdded, this,
                     [this](QScreen* screen) { handleScreenAdded(screen); });
    QObject::connect(application, &QGuiApplication::screenRemoved, this,
                     [this](QScreen* screen) { handleScreenRemoved(screen); });
    QObject::connect(application, &QGuiApplication::applicationStateChanged, this,
                     [this](Qt::ApplicationState state) { handleApplicationStateChanged(state); });
}

void ScreenshotDisplayConfigurationObserver::observeCurrentScreens() {
    observeScreens(QGuiApplication::screens());
}

void ScreenshotDisplayConfigurationObserver::observeScreens(const QList<QScreen*>& screens) {
    for (QScreen* screen : screens) {
        observeScreen(screen);
    }
}

void ScreenshotDisplayConfigurationObserver::observeScreen(QScreen* screen) {
    if (screen == nullptr || m_observedScreens.contains(screen)) {
        return;
    }

    m_observedScreens.insert(screen);
    QObject::connect(screen, &QObject::destroyed, this,
                     [this, screen]() { m_observedScreens.remove(screen); });
    QObject::connect(screen, &QScreen::geometryChanged, this,
                     [this](const QRect&) { handleObservedScreenChanged(); });
    QObject::connect(screen, &QScreen::virtualGeometryChanged, this,
                     [this](const QRect&) { handleObservedScreenChanged(); });
    QObject::connect(screen, &QScreen::logicalDotsPerInchChanged, this,
                     [this](qreal) { handleObservedScreenChanged(); });
    QObject::connect(screen, &QScreen::physicalDotsPerInchChanged, this,
                     [this](qreal) { handleObservedScreenChanged(); });
}

void ScreenshotDisplayConfigurationObserver::handleScreenAdded(QScreen* screen) {
    notifyChanged();
    observeScreen(screen);
}

void ScreenshotDisplayConfigurationObserver::handleScreenRemoved(QScreen* screen) {
    m_observedScreens.remove(screen);
    notifyChanged();
}

void ScreenshotDisplayConfigurationObserver::handleApplicationStateChanged(
    Qt::ApplicationState state) {
    if (state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended) {
        // Hidden/suspended windows do not need their toolbar rasters retained at the active budget.
        adqt::icons::trimIconCache(512 * 1024);
        notifyChanged();
    }
}

void ScreenshotDisplayConfigurationObserver::handleObservedScreenChanged() {
    notifyChanged();
}

void ScreenshotDisplayConfigurationObserver::notifyChanged(Change change) {
    if (m_onChanged) {
        m_onChanged(change);
    }
}
