#include "snow_shot/presentation/systemnotificationcontroller.h"

#include <QCoreApplication>
#include <QThread>
#include <QVector>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

namespace {
using snow_shot::platform::SystemNotificationAction;
using snow_shot::platform::SystemNotificationCompletion;
using snow_shot::platform::SystemNotificationRequest;
using snow_shot::platform::SystemNotificationResult;
using snow_shot::platform::SystemNotificationSeverity;
using snow_shot::presentation::SystemNotificationController;
using Status = SystemNotificationResult::Status;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void requestCompletion() {
    QVector<SystemNotificationCompletion> pending;
    SystemNotificationController controller(
        [&](const SystemNotificationRequest&, SystemNotificationCompletion completed) {
            pending.append(std::move(completed));
        });
    QVector<SystemNotificationRequest> delivered;
    QVector<SystemNotificationResult> results;
    QObject::connect(
        &controller, &SystemNotificationController::deliveryFinished, &controller,
        [&](const SystemNotificationRequest& request, const SystemNotificationResult& result) {
            require(QThread::currentThread() == controller.thread(),
                    "delivery results must return to the controller's thread");
            delivered.append(request);
            results.append(result);
        });
    controller.show({QStringLiteral("Export complete"), QStringLiteral("/first.mp4"),
                     SystemNotificationSeverity::Information,
                     SystemNotificationAction::OpenRecording, QStringLiteral("/first.mp4")});
    controller.show({QStringLiteral("Export complete"), QStringLiteral("/second.mp4"),
                     SystemNotificationSeverity::Information,
                     SystemNotificationAction::OpenRecording, QStringLiteral("/second.mp4")});
    controller.show({QStringLiteral("Pin failed"), QStringLiteral("Invalid image"),
                     SystemNotificationSeverity::Warning});
    require(pending.size() == 3, "each notification must have its own completion");
    // A backend can finish concurrent requests in any order and on any thread.
    std::thread worker([completed = pending.at(1)] { completed({}); });
    worker.join();
    QCoreApplication::processEvents();
    require(results.size() == 1 && results.first().accepted(),
            "accepted delivery must emit exactly one result");
    pending.at(2)({Status::Denied, QStringLiteral("internal permission diagnostic")});
    pending.at(0)({Status::Failed, QStringLiteral("internal transport diagnostic")});
    require(results.size() == 1, "synchronous backend failures must also queue their results");
    QCoreApplication::processEvents();
    require(results.size() == 3, "each rejected request must emit its own result");
    require(delivered.at(0).filePath == QStringLiteral("/second.mp4") &&
                delivered.at(0).action == SystemNotificationAction::OpenRecording &&
                delivered.at(1).title == QStringLiteral("Pin failed") &&
                delivered.at(1).severity == SystemNotificationSeverity::Warning &&
                results.at(1).status == Status::Denied &&
                delivered.at(2).filePath == QStringLiteral("/first.mp4") &&
                results.at(2).status == Status::Failed,
            "out-of-order results must preserve each request's message, severity and action");
}

void unavailableAndShutdown() {
    SystemNotificationController unavailable({});
    int unavailableResults = 0;
    QObject::connect(
        &unavailable, &SystemNotificationController::deliveryFinished, &unavailable,
        [&](const SystemNotificationRequest& request, const SystemNotificationResult& result) {
            require(request.body == QStringLiteral("Export complete") &&
                        result.status == Status::Unavailable,
                    "a missing backend must report the original undelivered request");
            ++unavailableResults;
        });
    unavailable.show({{}, QStringLiteral("Export complete")});
    require(unavailableResults == 0, "unavailable delivery must also queue its result");
    QCoreApplication::processEvents();
    require(unavailableResults == 1, "unavailable delivery must produce one result");

    SystemNotificationCompletion lateCompletion;
    int shutdownResults = 0;
    auto controller = std::make_unique<SystemNotificationController>(
        [&](const SystemNotificationRequest&, SystemNotificationCompletion completed) {
            lateCompletion = std::move(completed);
        });
    QObject::connect(controller.get(), &SystemNotificationController::deliveryFinished,
                     &unavailable, [&](const auto&, const auto&) { ++shutdownResults; });
    controller->show({{}, QStringLiteral("Pending shutdown")});
    lateCompletion({Status::Denied, {}});
    controller.reset();
    QCoreApplication::processEvents();
    std::thread worker([completed = lateCompletion] { completed({Status::Failed, {}}); });
    worker.join();
    QCoreApplication::processEvents();
    require(shutdownResults == 0,
            "queued and late native completions must not use a destroyed controller");
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    requestCompletion();
    unavailableAndShutdown();
    return 0;
}
