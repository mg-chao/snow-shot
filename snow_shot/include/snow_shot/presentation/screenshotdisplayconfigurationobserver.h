#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTDISPLAYCONFIGURATIONOBSERVER_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTDISPLAYCONFIGURATIONOBSERVER_H

#include <QList>
#include <QObject>
#include <QSet>
#include <Qt>

#include <functional>
#include <memory>

#ifdef Q_OS_WIN
namespace snow_shot::platform::windows {
class DisplayRecoveryWindow;
}
#endif

class QGuiApplication;
class QRect;
class QScreen;

class ScreenshotDisplayConfigurationObserver final : public QObject {
  public:
    enum class Change { Configuration, Recovery };
    using ChangeHandler = std::function<void(Change)>;

    explicit ScreenshotDisplayConfigurationObserver(ChangeHandler onChanged,
                                                    QObject* parent = nullptr);
    ~ScreenshotDisplayConfigurationObserver() override;

    void connectApplicationSignals(QGuiApplication* application);
    void observeCurrentScreens();
    void observeScreens(const QList<QScreen*>& screens);
    void observeScreen(QScreen* screen);

    void handleScreenAdded(QScreen* screen);
    void handleScreenRemoved(QScreen* screen);
    void handleApplicationStateChanged(Qt::ApplicationState state);
    void handleObservedScreenChanged();

  private:
    void notifyChanged(Change change = Change::Configuration);

    ChangeHandler m_onChanged;
    QSet<QScreen*> m_observedScreens;
#ifdef Q_OS_WIN
    std::unique_ptr<snow_shot::platform::windows::DisplayRecoveryWindow> m_recoveryWindow;
#elif defined(Q_OS_MACOS)
    std::unique_ptr<QObject> m_displayWakeObserver;
#endif
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTDISPLAYCONFIGURATIONOBSERVER_H
