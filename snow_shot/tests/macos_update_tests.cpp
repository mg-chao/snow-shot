#include "../src/update/macosupdatedriver_p.h"
#include "snow_shot/update/startupupdate.h"
#include <QCoreApplication>
#include <QEvent>
#include <cstdio>
#include <cstdlib>

using namespace snow_shot::update;
namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
class Driver final : public detail::MacOSUpdateDriver {
  public:
    QString mode;
    int starts = 0;
    int checks = 0;
    int downloads = 0;
    int cancellations = 0;
    int installs = 0;
    int translations = 0;
    void start(const QString& value) override {
        ++starts;
        mode = value;
        publish(UpdateState::Idle);
    }
    void setMode(const QString& value) override {
        mode = value;
    }
    void check(bool) override {
        ++checks;
        publish(UpdateState::Checking);
    }
    void download() override {
        ++downloads;
        publish(UpdateState::Downloading, true);
    }
    void cancel() override {
        ++cancellations;
        publish(UpdateState::Available);
    }
    void install() override {
        ++installs;
    }
    void retranslate() override {
        ++translations;
    }
    void publish(UpdateState state, bool cancellable = false, bool onQuit = false) {
        changed({state, QStringLiteral("2.0.0"), {}, 12, 100},
                state == UpdateState::Idle || state == UpdateState::Available ||
                    state == UpdateState::Failed,
                cancellable, onQuit);
    }
};
void policiesAndLifecycle() {
    Driver* driver = nullptr;
    UpdateService::Options options;
    options.macOSDriverFactory = [&] {
        auto value = std::make_unique<Driver>();
        driver = value.get();
        return value;
    };
    UpdateService service(options);
    int restarts = 0, commits = 0, ready = 0, finished = 0;
    QObject::connect(&service, &UpdateService::restartRequested, [&] { ++restarts; });
    QObject::connect(&service, &UpdateService::handoffCommitted, [&] { ++commits; });
    QObject::connect(&service, &UpdateService::updateReady, [&] { ++ready; });
    QObject::connect(&service, &UpdateService::operationFinished,
                     [&](const QString&, const QString&) { ++finished; });
    service.start();
    service.start();
    require(!service.setRelaunchArguments({QStringLiteral("--open-file")}),
            "native relaunch cannot silently accept unsupported command-line intent");
    require(driver->starts == 1 && driver->mode == u"next_launch",
            "native default is next launch; start is idempotent");
    require(service.capabilities().check && service.capabilities().download &&
                service.capabilities().install,
            "native updater supports the complete update flow");
    service.setMode(QStringLiteral("manual"));
    service.check(false);
    require(driver->checks == 0, "manual disables automatic checks");
    service.check();
    service.check();
    require(driver->checks == 1 && service.busy(),
            "user checks work in manual mode and do not overlap");
    driver->publish(UpdateState::Available);
    service.download();
    require(service.canCancel() && service.status().received == 12 && service.status().total == 100,
            "native download progress and cancellation reach Qt");
    service.cancel();
    require(driver->cancellations == 1, "supported cancellation reaches the native driver");
    driver->publish(UpdateState::Downloading);
    service.cancel();
    require(driver->cancellations == 1,
            "silent native downloads do not advertise unsupported cancellation");
    driver->publish(UpdateState::Verifying);
    driver->publish(UpdateState::Ready, false, true);
    require(ready == 1 && !service.busy() && service.installsOnQuit(),
            "prepared updates are ready, not applying");
    require(!restarts && !commits && !driver->installs,
            "background preparation cannot terminate the session");
    for (const QString& mode :
         {QStringLiteral("manual"), QStringLiteral("check"), QStringLiteral("next_launch")}) {
        service.setMode(mode);
        require(driver->mode == mode && service.installsOnQuit(),
                "mode changes retain prepared installation");
    }
    service.setMode(QStringLiteral("download"));
    require(driver->mode == u"check", "legacy download retains check-only behavior");
    service.setMode(QStringLiteral("invalid"));
    require(driver->mode == u"check", "invalid modes cannot alter native preferences");
    service.applyAtStartup();
    require(finished == 1 && !driver->installs && !restarts,
            "macOS never applies through the startup gate");
    service.beginApply();
    require(!driver->installs, "missing handoff approval refuses explicit installation");
    QObject context;
    service.setHandoffApproval(&context, [] { return true; });
    service.setHandoffValidation(&context, [] { return false; });
    service.requestRestart();
    service.beginApply();
    require(restarts == 1 && !service.handoffPending() && !driver->installs,
            "explicit restart still requires final handoff validation");
    service.setHandoffValidation(&context, [] { return true; });
    service.beginApply();
    require(driver->installs == 1 && service.handoffPending() && commits == 0,
            "quit waits until Sparkle actually arms the relaunch");
    driver->committed();
    driver->committed();
    require(commits == 1 && !service.acceptsRelaunchArguments({}),
            "native commitment closes admission exactly once");
    QEvent language(QEvent::LanguageChange);
    QCoreApplication::sendEvent(&service, &language);
    require(driver->translations == 1, "native error presentation retranslates");
}
void failureAndRefusal() {
    Driver* driver = nullptr;
    UpdateService::Options options;
    options.macOSDriverFactory = [&] {
        auto value = std::make_unique<Driver>();
        driver = value.get();
        return value;
    };
    UpdateService service(options);
    service.start();
    driver->publish(UpdateState::Failed);
    require(!service.busy() && service.canCheck(),
            "failed checks leave normal operation and retry available");
    driver->publish(UpdateState::Ready, false, true);
    QObject context;
    service.setHandoffApproval(&context, []() -> bool { throw 1; });
    service.setHandoffValidation(&context, [] { return true; });
    service.beginApply();
    require(!service.handoffPending() && !driver->installs,
            "throwing approval cannot terminate the app");
    service.setHandoffApproval(&context, [] { return true; });
    service.beginApply();
    driver->publish(UpdateState::Failed);
    require(!service.handoffPending(), "installation failure releases input suspension");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    policiesAndLifecycle();
    failureAndRefusal();
    return 0;
}
