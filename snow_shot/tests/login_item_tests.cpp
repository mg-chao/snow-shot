#include "login_item_test_support.h"
#include "snow_shot/platform/macos/loginitemservice.h"
#include <QCoreApplication>
#include <QSemaphore>
#include <QTimer>
#include <atomic>
#include <memory>
#include <cstdlib>
#include <iostream>

using namespace snow_shot::platform::macos;
namespace {
void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
struct Fixture {
    LoginItemSnapshot native{LoginItemStatus::Unregistered, {}};
    bool preferenceWritable = true;
    bool preference = false;
    bool failNative = false;
    bool approval = false;
    int queries = 0;
    int changes = 0;
    LoginItemService service{operations()};
    Fixture() {
        login_item_test::idle(service);
    }
    LoginItemOperations operations() {
        return {[this] {
                    require(QThread::currentThread() != QCoreApplication::instance()->thread(),
                            "status must run off the GUI thread");
                    ++queries;
                    return native;
                },
                [this](bool enabled) -> LoginItemResult {
                    ++changes;
                    require(QThread::currentThread() != QCoreApplication::instance()->thread(),
                            "native mutation must run off the GUI thread");
                    if (failNative)
                        return {false, QStringLiteral("native failure")};
                    native.status = enabled ? (approval ? LoginItemStatus::ApprovalRequired
                                                        : LoginItemStatus::Enabled)
                                            : LoginItemStatus::Unregistered;
                    return {};
                },
                [this](bool value) {
                    require(QThread::currentThread() == QCoreApplication::instance()->thread(),
                            "preference publication must run on the GUI thread");
                    preference = value;
                    return preferenceWritable;
                }};
    }
};
void refreshNotifications() {
    Fixture f;
    int notifications = 0;
    int busyNotifications = 0;
    QObject::connect(&f.service, &LoginItemService::changed, [&] {
        ++notifications;
        if (f.service.pending())
            ++busyNotifications;
    });
    const int beforeRefresh = f.queries;
    login_item_test::refresh(f.service);
    login_item_test::refresh(f.service);
    require(f.queries == beforeRefresh + 2, "refresh must still observe current native state");
    require(notifications == 0, "unchanged refresh must not notify settings consumers");
    f.native.status = LoginItemStatus::Unavailable;
    login_item_test::refresh(f.service);
    require(notifications == 1 && !f.service.available(), "status change must notify consumers");
    f.native.error = QStringLiteral("install app");
    login_item_test::refresh(f.service);
    require(notifications == 2 && f.service.hint() == f.native.error,
            "hint change must notify even when status is unchanged");
    login_item_test::refresh(f.service);
    require(notifications == 2, "unchanged status and hint must not notify twice");
    f.native = {LoginItemStatus::Unregistered, {}};
    login_item_test::refresh(f.service);
    require(notifications == 3 && f.service.hint().isEmpty(), "cleared hint must notify consumers");
    notifications = 0;
    require(login_item_test::change(f.service, true).success, "registration succeeds");
    require(notifications == 2 && busyNotifications == 1 && !f.service.pending(),
            "registration must still notify both busy and idle transitions");
    notifications = 0;
    busyNotifications = 0;
    f.failNative = true;
    require(!login_item_test::change(f.service, false).success, "native mutation fails");
    require(notifications == 2 && busyNotifications == 1 && !f.service.pending() &&
                f.service.snapshot().requested(),
            "failed mutation must notify busy and idle even with an unchanged snapshot");
}
void registration() {
    Fixture f;
    require(f.changes == 0 && !f.service.snapshot().requested(),
            "fresh service leaves login registration disabled");
    require(login_item_test::change(f.service, true).success && f.changes == 1 && f.preference,
            "explicit enable registers and saves the preference");
    require(login_item_test::change(f.service, true).success && f.changes == 1,
            "enable is idempotent");
    f.native.status = LoginItemStatus::ApprovalRequired;
    login_item_test::refresh(f.service);
    require(f.service.snapshot().requested() && !f.service.hint().isEmpty() && f.changes == 1,
            "revoked approval is reported without changing registration");
    require(login_item_test::change(f.service, false).success && !f.preference && f.changes == 2,
            "disable unregisters approval-pending item");
    require(login_item_test::change(f.service, false).success && f.changes == 2,
            "disable is idempotent");
    f.approval = true;
    require(login_item_test::change(f.service, true).success && f.preference &&
                f.service.snapshot().status == LoginItemStatus::ApprovalRequired,
            "registration awaiting consent shows a requested registration with a hint");
    f.native.status = LoginItemStatus::Unregistered;
    login_item_test::refresh(f.service);
    require(!f.service.snapshot().requested() && f.preference && f.changes == 3,
            "external removal is respected even while the saved preference is enabled");
    f.approval = false;
    require(login_item_test::change(f.service, true).success && f.changes == 4,
            "an explicit toggle can re-register after external removal");
}
void startupPreservesNativeState() {
    Fixture f;
    f.preference = true;
    for (const auto status : {LoginItemStatus::Unregistered, LoginItemStatus::Enabled,
                              LoginItemStatus::ApprovalRequired}) {
        f.native.status = status;
        LoginItemService restarted(f.operations());
        login_item_test::idle(restarted);
        login_item_test::refresh(restarted);
        require(restarted.snapshot().status == status && f.changes == 0 && f.preference,
                "startup and refresh preserve native registration regardless of saved preference");
    }
}
void failures() {
    Fixture unavailable;
    unavailable.native = {LoginItemStatus::Unavailable,
                          QStringLiteral("native status unavailable")};
    require(!login_item_test::change(unavailable.service, true).success && unavailable.changes == 0,
            "unavailable native status is reported");
    unavailable.native = {LoginItemStatus::Unregistered, {}};
    require(login_item_test::change(unavailable.service, true).success && unavailable.changes == 1,
            "explicit retry can recover from unavailable native status");
    Fixture native;
    native.failNative = true;
    require(!login_item_test::change(native.service, true).success && native.changes == 1 &&
                !native.service.snapshot().requested(),
            "failed registration keeps the native disabled state");
    login_item_test::refresh(native.service);
    require(native.changes == 1, "refresh does not retry a failed registration");
    native.failNative = false;
    require(login_item_test::change(native.service, true).success,
            "explicit retry recovers native failure");
    native.failNative = true;
    require(!login_item_test::change(native.service, false).success &&
                native.service.snapshot().requested(),
            "failed disable retains actual native status");
    Fixture persistence;
    persistence.preferenceWritable = false;
    require(!login_item_test::change(persistence.service, true).success &&
                persistence.service.snapshot().requested() && !persistence.service.pending(),
            "preference failure retains successful native status and clears pending");
}
struct BlockingState {
    std::atomic_int queries{0};
    std::atomic_int changes{0};
    std::atomic_int queryExits{0};
    std::atomic_bool blockQuery{true};
    std::atomic_bool blockChange{false};
    std::atomic_bool enabled{false};
    QSemaphore entered;
    QSemaphore release;
    int saved = 0;
    LoginItemOperations operations() {
        return {[this] {
                    require(QThread::currentThread() != QCoreApplication::instance()->thread(),
                            "blocked status query is off the GUI thread");
                    ++queries;
                    if (blockQuery.exchange(false)) {
                        entered.release();
                        release.acquire();
                    }
                    const LoginItemSnapshot snapshot{
                        enabled ? LoginItemStatus::Enabled : LoginItemStatus::Unregistered, {}};
                    ++queryExits;
                    return snapshot;
                },
                [this](bool value) {
                    require(QThread::currentThread() != QCoreApplication::instance()->thread(),
                            "blocked registration is off the GUI thread");
                    ++changes;
                    if (blockChange.exchange(false)) {
                        entered.release();
                        release.acquire();
                    }
                    enabled = value;
                    return LoginItemResult{};
                },
                [this](bool) {
                    require(QThread::currentThread() == QCoreApplication::instance()->thread(),
                            "completion persists preferences on the GUI thread");
                    ++saved;
                    return true;
                }};
    }
    void waitUntilBlocked() {
        login_item_test::waitFor([&] { return entered.tryAcquire(); });
        bool delivered = false;
        QTimer::singleShot(0, QCoreApplication::instance(), [&] { delivered = true; });
        login_item_test::waitFor([&] { return delivered; });
    }
};
void blockedRefreshAndChanges() {
    BlockingState state;
    LoginItemService service(state.operations());
    require(state.queries == 0, "construction never queries native status inline");
    state.waitUntilBlocked();
    for (int i = 0; i < 100; ++i)
        service.refresh();
    require(state.queries == 1, "activation bursts cannot overlap a blocked query");
    state.release.release();
    login_item_test::idle(service);
    require(state.queries == 2, "one trailing refresh observes changes during the first query");
    for (int i = 0; i < 100; ++i)
        service.refresh();
    login_item_test::idle(service);
    require(state.queries == 3, "same-event refresh requests are coalesced");

    state.blockQuery = true;
    service.refresh();
    state.waitUntilBlocked();
    state.blockChange = true;
    bool completed = false;
    require(service.setEnabled(true,
                               [&](LoginItemResult result) {
                                   require(QThread::currentThread() == service.thread(),
                                           "GUI completion callback");
                                   completed = result.success;
                               })
                    .success &&
                service.pending(),
            "a mutation can queue behind a refresh");
    const auto future = service.completionFuture();
    require(!service.setEnabled(false).success, "overlapping mutations are rejected");
    for (int i = 0; i < 100; ++i)
        service.refresh();
    require(state.changes == 0 && !completed && state.saved == 0,
            "no registration or persistence may overtake the query");
    state.release.release();
    state.waitUntilBlocked();
    require(service.pending() && !completed && !service.snapshot().requested(),
            "blocked registration retains the old snapshot and pending state");
    state.release.release();
    login_item_test::idle(service);
    require(completed && future.get().success && service.snapshot().requested() && state.saved == 1,
            "native completion publishes state and persists once");

    state.blockChange = true;
    require(service.setEnabled(false).success, "unregistration admitted");
    state.waitUntilBlocked();
    require(service.pending() && service.snapshot().requested(), "unregister remains responsive");
    state.release.release();
    login_item_test::idle(service);
    require(!service.snapshot().requested() && state.changes == 2 && state.saved == 2,
            "unregistration completes asynchronously");
}
void asynchronousDestruction() {
    BlockingState state;
    auto service = std::make_unique<LoginItemService>(state.operations());
    state.waitUntilBlocked();
    bool completed = false;
    require(service->setEnabled(true, [&](LoginItemResult) { completed = true; }).success,
            "queued mutation admitted before destruction");
    const auto future = service->completionFuture();
    const int before = state.queryExits;
    service.reset();
    require(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready &&
                !future.get().success,
            "destruction resolves outstanding completion without waiting for native work");
    state.release.release();
    login_item_test::waitFor([&] { return state.queryExits > before; });
    QCoreApplication::processEvents();
    require(!completed && state.changes == 0 && state.saved == 0,
            "destroyed receiver cannot apply queued registration, publish, or persist");
}
void launchArguments() {
    const QStringList manual{QStringLiteral("snow_shot"), QStringLiteral("--show-main-window")};
    require(loginItemLaunchArguments(manual, false) == manual, "manual launch preserved");
    const auto login = loginItemLaunchArguments(manual, true);
    require(login.contains(QStringLiteral("--autostart")) &&
                loginItemLaunchArguments(login, true) == login,
            "native login maps idempotently to quiet forwarding");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    refreshNotifications();
    registration();
    startupPreservesNativeState();
    failures();
    blockedRefreshAndChanges();
    asynchronousDestruction();
    launchArguments();
    std::cout << "Login item tests passed\n";
}
