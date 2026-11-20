#include "snow_shot/presentation/directcaptureworkflow.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

using namespace snow_shot::presentation;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

DirectCaptureFrame frame() {
    QImage image(7, 5, QImage::Format_RGB32);
    image.fill(qRgb(12, 34, 56));
    return {image, QRect(-1900, -200, 7, 5), QStringLiteral("target"), 1, {}};
}

struct Fixture {
    std::function<void(DirectCaptureFrame)> acquired;
    DirectCapturePorts::CopyCompletion copied;
    DirectCapturePorts::Completion recorded;
    QVector<DirectCaptureRequest> requests;
    QVector<DirectCaptureRequest> copyRequests;
    QStringList events;
    QImage output;
    bool acceptAcquire = true;
    bool acceptCopy = true;
    bool acceptHistory = true;
    bool stopOnCaptureRequested = false;
    bool stopOnReport = false;
    int completed = 0;
    DirectCaptureWorkflow workflow{DirectCapturePorts{
        [this](const DirectCaptureRequest& request, auto done) {
            requests.push_back(request);
            events << "acquire";
            acquired = std::move(done);
            return acceptAcquire;
        },
        [this](const auto& request, const auto& result, auto done) {
            events << "copy";
            copyRequests.push_back(request);
            output = result.image;
            copied = std::move(done);
            return acceptCopy;
        },
        [this](const auto&, const auto& result, auto done) {
            events << "history";
            output = result.image;
            recorded = std::move(done);
            return acceptHistory;
        },
        [this](const QString&, bool warning) {
            events << (warning ? "warning" : "error");
            if (stopOnReport)
                workflow.shutdown();
        },
        [this]() {
            events << "shutter";
            if (stopOnCaptureRequested)
                workflow.shutdown();
        },
        [this]() { ++completed; },
    }};
};

void shutterPlaysImmediatelyForEveryRequest() {
    for (auto target : {DirectCaptureTarget::FocusedWindow, DirectCaptureTarget::CurrentMonitor}) {
        Fixture f;
        DirectCaptureRequest request;
        request.target = target;
        f.workflow.enqueue(request);
        require(f.events == QStringList({"shutter", "acquire"}),
                "shutter waited for image acquisition instead of acknowledging the request");
        f.workflow.enqueue(request);
        require(f.events == QStringList({"shutter", "acquire", "shutter"}),
                "queued capture delayed its shutter notification");
        f.acquired(frame());
        f.copied(true, {});
        QCoreApplication::processEvents();
        f.acquired({});
        require(f.events.count("shutter") == 2 && f.workflow.pendingCount() == 0,
                "capture completion or failure repeated the shutter notification");
        f.workflow.shutdown();
        f.workflow.enqueue(request);
        require(f.events.count("shutter") == 2, "stopped workflow played a shutter sound");
    }
}

void shutterPreferenceAppliesToEachRequest() {
    for (auto target : {DirectCaptureTarget::FocusedWindow, DirectCaptureTarget::CurrentMonitor}) {
        Fixture f;
        DirectCaptureRequest request;
        request.target = target;
        request.shutterSoundNotification = false;
        f.workflow.enqueue(request);
        f.workflow.enqueue(request);
        require(f.events == QStringList({"acquire"}),
                "disabled shutter notification played for an active or queued capture");
        request.shutterSoundNotification = true;
        f.workflow.enqueue(request);
        require(f.events == QStringList({"acquire", "shutter"}),
                "reenabling shutter notification did not affect the next request immediately");
        for (int i = 0; i < 3; ++i) {
            f.acquired(frame());
            f.copied(true, {});
            QCoreApplication::processEvents();
        }
        require(f.events.count("shutter") == 1 && f.events.count("copy") == 3 &&
                    f.workflow.pendingCount() == 0,
                "shutter preference changed capture output or repeated a queued notification");
    }
}

void outputsKeepRawPixelsAndProcessEveryRequest() {
    Fixture f;
    DirectCaptureRequest first;
    first.target = DirectCaptureTarget::FocusedWindow;
    first.window = 1234;
    first.afterCopy = [&](const auto& result) {
        require(result.image == frame().image, "follow-up tasks lost the captured pixels");
        f.events << "tasks";
    };
    first.historyEnabled = true;
    first.encoding.quality = 35;
    first.encoding.compressionLevel = ScreenshotCompressionLevel::High;
    first.filenameFormat = QStringLiteral("first");
    first.captureBackend = 3;
    f.workflow.enqueue(first);
    DirectCaptureRequest second;
    require(second.encoding.compressionLevel == ScreenshotCompressionLevel::Medium,
            "direct capture requests must default to medium file compression");
    second.monitorName = QStringLiteral("second-monitor");
    second.captureBackend = 2;
    f.workflow.enqueue(second);
    second.captureBackend = 1;
    require(f.requests.size() == 1 && f.requests.front().window == 1234 &&
                f.requests.front().captureBackend == 3,
            "FIFO target snapshot lost");
    f.acquired(frame());
    require(f.output == frame().image, "capture pixels were styled or resampled");
    require(f.events.back() == "copy", "automatic saving delayed clipboard publication");
    require(!f.copyRequests.back().copyFile, "auto save incorrectly selected file clipboard mode");
    f.copied(true, {});
    require(f.workflow.pendingCount() == 2, "next capture began before history completion");
    f.recorded({});
    QCoreApplication::processEvents();
    require(f.requests.size() == 2 && f.requests.back().monitorName == second.monitorName &&
                f.requests.back().captureBackend == 2,
            "mixed direct captures were dropped or reordered");
    f.acquired(frame());
    f.copied(true, {});
    require(f.workflow.pendingCount() == 0 && f.completed == 2,
            "successful queue did not drain or release each export");
    require(f.events == QStringList({"shutter", "acquire", "shutter", "copy", "tasks", "history",
                                     "acquire", "copy"}),
            "output ordering changed");
}

void tasksRequireAConfirmedCopy() {
    for (const auto target :
         {DirectCaptureTarget::FocusedWindow, DirectCaptureTarget::CurrentMonitor}) {
        for (int outcome = 0; outcome < 4; ++outcome) {
            Fixture f;
            DirectCaptureRequest request;
            request.target = target;
            request.afterCopy = [&](const auto&) { f.events << "tasks"; };
            request.historyEnabled = true;
            f.acceptCopy = outcome != 0;
            f.workflow.enqueue(request);
            f.workflow.enqueue({});
            f.acquired(frame());
            const bool published = outcome == 3;
            if (f.acceptCopy)
                f.copied(published, outcome == 1 ? QStringLiteral("clipboard error") : QString{});
            require(f.events.count("tasks") == int(published) &&
                        f.events.count("history") == int(published),
                    "rejection, failure and supersession must skip post-copy work");
            if (published)
                f.recorded({});
            QCoreApplication::processEvents();
            require(f.requests.size() == 2, "post-copy work stalled the next capture");
        }
    }
}

void fileCopyHistoryFailureAndLateCallbacks() {
    Fixture f;
    DirectCaptureRequest request;
    request.copyFile = true;
    request.historyEnabled = true;
    f.workflow.enqueue(request);
    auto oldAcquired = f.acquired;
    f.acquired(frame());
    require(f.copyRequests.back().copyFile,
            "file copy must prepare its own temporary file without automatic saving");
    f.copied(true, {});
    auto oldRecorded = f.recorded;
    f.recorded(QStringLiteral("history failed"));
    oldRecorded({});
    oldAcquired(frame());
    require(f.workflow.pendingCount() == 0 && f.events.count("copy") == 1,
            "duplicate callbacks repeated output");
    f.workflow.enqueue({});
    auto late = f.acquired;
    f.workflow.shutdown();
    late(frame());
    f.workflow.enqueue({});
    require(f.workflow.pendingCount() == 0 && f.events.count("copy") == 1,
            "shutdown allowed late output");
}

void rejectedOperationsFinishAndDestroyedReceiversIgnoreResults() {
    for (int stage = 0; stage < 3; ++stage) {
        Fixture f;
        f.acceptAcquire = stage != 0;
        f.acceptCopy = stage != 1;
        f.acceptHistory = stage != 2;
        DirectCaptureRequest request;
        request.copyFile = true;
        request.historyEnabled = true;
        f.workflow.enqueue(request);
        if (stage > 0)
            f.acquired(frame());
        if (stage > 1)
            f.copied(true, {});
        require(f.workflow.pendingCount() == 0, "rejected operation left the request stuck");
    }
    std::function<void(DirectCaptureFrame)> late;
    {
        Fixture f;
        f.workflow.enqueue({});
        late = f.acquired;
    }
    late(frame());
}

void queuedRequestsRetainTargetsAndOutputSettings() {
    Fixture f;
    DirectCaptureRequest request;
    request.target = DirectCaptureTarget::FocusedWindow;
    request.window = 8765;
    request.requestedAt = QDateTime::fromMSecsSinceEpoch(123456).toUTC();
    request.copyFile = true;
    request.historyEnabled = true;
    request.imageFormat = QStringLiteral("pdf");
    request.encoding.quality = 42;
    request.encoding.compressionLevel = ScreenshotCompressionLevel::High;
    request.pdf.pageSize = ScreenshotPdfPageSize::LandscapeA4;
    request.filenameFormat = QStringLiteral("original-filename");
    f.workflow.enqueue({});
    for (int i = 0; i < 12; ++i) {
        request.window = static_cast<quintptr>(8765 + i);
        f.workflow.enqueue(request);
    }
    request = {};
    require(f.requests.size() == 1, "queued requests acquired pixels before reaching the head");
    f.acquired(frame());
    f.copied(true, {});
    for (int i = 0; i < 12; ++i) {
        QCoreApplication::processEvents();
        const auto& queued = f.requests.back();
        require(queued.target == DirectCaptureTarget::FocusedWindow &&
                    queued.window == static_cast<quintptr>(8765 + i) && queued.copyFile &&
                    queued.historyEnabled && queued.requestedAt.toMSecsSinceEpoch() == 123456 &&
                    queued.imageFormat == QStringLiteral("pdf") && queued.encoding.quality == 42 &&
                    queued.encoding.compressionLevel == ScreenshotCompressionLevel::High &&
                    queued.pdf.pageSize == ScreenshotPdfPageSize::LandscapeA4 &&
                    queued.pdf.quality == 100 &&
                    queued.filenameFormat == QStringLiteral("original-filename"),
                "queued target or settings changed after invocation");
        f.acquired(frame());
        f.copied(true, {});
        f.recorded({});
    }
    require(f.requests.size() == 13 && f.workflow.pendingCount() == 0,
            "burst requests were coalesced or dropped");
}

void shutdownDuringNotificationsStopsOutputs() {
    {
        Fixture f;
        f.stopOnCaptureRequested = true;
        f.workflow.enqueue({});
        require(f.workflow.pendingCount() == 0 && f.events == QStringList({"shutter"}),
                "request notification shutdown allowed acquisition or output");
    }
    for (int stage = 0; stage < 3; ++stage) {
        Fixture f;
        f.stopOnReport = true;
        DirectCaptureRequest request;
        request.historyEnabled = true;
        f.workflow.enqueue(request);
        f.workflow.enqueue({});
        if (stage == 0)
            f.acquired({});
        else {
            f.acquired(frame());
            f.copied(stage != 1, stage == 1 ? QStringLiteral("copy failed") : QString());
            if (stage == 2)
                f.recorded(QStringLiteral("history failed"));
        }
        QCoreApplication::processEvents();
        require(f.workflow.pendingCount() == 0 && f.requests.size() == 1 &&
                    (stage != 1 || !f.events.contains("save")),
                "error notification shutdown allowed further output or acquisition");
    }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    shutterPlaysImmediatelyForEveryRequest();
    shutterPreferenceAppliesToEachRequest();
    outputsKeepRawPixelsAndProcessEveryRequest();
    tasksRequireAConfirmedCopy();
    fileCopyHistoryFailureAndLateCallbacks();
    rejectedOperationsFinishAndDestroyedReceiversIgnoreResults();
    queuedRequestsRetainTargetsAndOutputSettings();
    shutdownDuringNotificationsStopsOutputs();
    return 0;
}
