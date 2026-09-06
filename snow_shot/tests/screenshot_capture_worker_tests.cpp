#include "screenshotcaptureworker.h"
#include "snow_shot/presentation/capture/screenshotcapturepolicy.h"
#include "snow_shot/presentation/screenshotcapturecoordinator.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/settingsadapters.h"

#include "snow_capture.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>

struct SnowCaptureDesktopSessionImpl {
    SnowCaptureDesktopSessionConfig config{};
    bool prepared = false;
};

struct SnowCaptureFrameLeaseImpl {
    std::shared_ptr<std::array<uint8_t, 4>> pixels =
        std::make_shared<std::array<uint8_t, 4>>(std::array<uint8_t, 4>{10, 20, 30, 255});
};

struct SnowCaptureScreenshotResultImpl {
    uint8_t backend = SNOW_CAPTURE_BACKEND_AUTO;
    uint8_t pixelFormat = SNOW_CAPTURE_PIXEL_FORMAT_RGBA8;
    SnowCaptureFrameLease frame;
};

struct SnowCaptureCancellationTokenImpl {};

namespace {
int created = 0;
int destroyed = 0;
int captured = 0;
int leases = 0;
bool failCreation = false;
bool failCapture = false;
uint8_t preparedBackend = SNOW_CAPTURE_BACKEND_AUTO;
uint8_t refreshedBackend = SNOW_CAPTURE_BACKEND_AUTO;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void setMode(const char* mode) {
    require(snow_shot::storage::ScreenshotSettings().setApiMode(QString::fromLatin1(mode)),
            "failed to update screenshot API mode");
}

ScreenshotCaptureResult capture(ScreenshotCaptureWorker& worker) {
    ScreenshotCaptureCoordinator coordinator;
    ScreenshotCaptureResult result;
    bool received = false;
    QObject::connect(&coordinator, &ScreenshotCaptureCoordinator::captureFinished, &coordinator,
                     [&](const ScreenshotCaptureResult& value) {
                         result = value;
                         received = true;
                     });
    ScreenshotCaptureRequest request;
    request.requestId = 42;
    worker.capture(request, &coordinator, nullptr);
    QCoreApplication::sendPostedEvents(&coordinator);
    require(received && result.requestId == request.requestId, "capture result was not delivered");
    return result;
}

void requireBackend(ScreenshotCaptureWorker& worker, uint8_t expected) {
    const auto result = capture(worker);
    require(result.succeeded && result.displays.size() == 1, "capture did not return a display");
    require(static_cast<uint8_t>(result.displays.front().backend) == expected,
            "regular screenshot used the previous API mode's backend");
    require(!result.displays.front().image.isNull(), "capture did not retain the frame");
}

void changedModeReplacesPrewarmedSession() {
    setMode("dxgi");
    ScreenshotCaptureWorker worker;
    worker.prepare(1, {});
    require(preparedBackend == SNOW_CAPTURE_BACKEND_DXGI, "DXGI session was not prewarmed");
    setMode("gdi");
    requireBackend(worker, SNOW_CAPTURE_BACKEND_GDI);
}

void allModeTransitionsApplyWithoutRestart() {
    namespace policy = snow_shot::presentation::capture;
    const std::array<const char*, 4> modes{"dxgi", "wgc", "gdi", "auto"};
    const auto backendFor = [](const char* mode) {
        auto requested = policy::screenshotApiModeFromValue(mode);
        if (requested == policy::ScreenshotApiMode::Auto) {
            requested = policy::resolveAutoScreenshotApiMode();
        }
        return policy::nativeBackendForNormalScreenshot(requested);
    };
    for (const auto* before : modes) {
        for (const auto* after : modes) {
            setMode(before);
            ScreenshotCaptureWorker worker;
            requireBackend(worker, backendFor(before));
            const int previousCreated = created;
            const int previousDestroyed = destroyed;
            setMode(after);
            requireBackend(worker, backendFor(after));
            const int replacements = backendFor(before) == backendFor(after) ? 0 : 1;
            require(created == previousCreated + replacements &&
                        destroyed == previousDestroyed + replacements,
                    "session reuse must depend on the resolved capture backend");
            requireBackend(worker, backendFor(after));
            require(created == previousCreated + replacements,
                    "unchanged API mode discarded the warm capture session");
        }
    }
}

void preparationAndLayoutRefreshUseCurrentMode() {
    setMode("dxgi");
    ScreenshotCaptureWorker worker;
    worker.prepare(1, {});
    setMode("gdi");
    worker.prepare(2, {});
    require(preparedBackend == SNOW_CAPTURE_BACKEND_GDI,
            "preparation reused a session with an obsolete backend");
    setMode("wgc");
    worker.refreshLayout(3, {});
    require(refreshedBackend == SNOW_CAPTURE_BACKEND_WGC,
            "layout refresh reused a session with an obsolete backend");
    requireBackend(worker, SNOW_CAPTURE_BACKEND_WGC);
}

void failedReplacementDoesNotCaptureWithOldBackend() {
    setMode("dxgi");
    ScreenshotCaptureWorker worker;
    requireBackend(worker, SNOW_CAPTURE_BACKEND_DXGI);
    setMode("gdi");
    const int previousCaptured = captured;
    failCreation = true;
    const auto failed = capture(worker);
    failCreation = false;
    require(!failed.succeeded && !failed.errorMessage.isEmpty() && failed.displays.isEmpty(),
            "failed backend replacement was not reported");
    require(captured == previousCaptured, "failed replacement captured with the stale backend");
    requireBackend(worker, SNOW_CAPTURE_BACKEND_GDI);
}

void modeChangeAfterCaptureFailurePreservesRetainedFrame() {
    setMode("dxgi");
    ScreenshotCaptureWorker worker;
    const auto retained = capture(worker);
    require(retained.succeeded, "initial capture failed");
    failCapture = true;
    require(!capture(worker).succeeded, "native capture failure was not reported");
    failCapture = false;
    setMode("gdi");
    requireBackend(worker, SNOW_CAPTURE_BACKEND_GDI);
    require(retained.displays.front().image.constBits()[0] == 10,
            "replacing a session invalidated an already delivered image");
}
} // namespace

// Substitute only the native API; settings, policy, worker, and result delivery are production
// code.
extern "C" {
SnowCaptureDesktopSession*
snow_capture_desktop_session_create(const SnowCaptureDesktopSessionConfig* config) {
    if (failCreation) {
        return nullptr;
    }
    ++created;
    return new SnowCaptureDesktopSession{*config, false};
}

void snow_capture_desktop_session_destroy(SnowCaptureDesktopSession* session) {
    ++destroyed;
    delete session;
}

uint8_t snow_capture_desktop_session_prepare(SnowCaptureDesktopSession* session) {
    preparedBackend = session->config.capture_backend;
    session->prepared = true;
    return 1;
}

uint8_t snow_capture_desktop_session_state(SnowCaptureDesktopSession* session,
                                           SnowCaptureDesktopSessionState* state) {
    *state = {};
    state->prepared = session->prepared ? 1 : 0;
    return 1;
}

uint8_t snow_capture_desktop_session_refresh_layout(SnowCaptureDesktopSession* session) {
    refreshedBackend = session->config.capture_backend;
    return 1;
}

uint8_t snow_capture_desktop_session_reset_to_prepared(SnowCaptureDesktopSession* session) {
    session->prepared = true;
    return 1;
}

SnowCaptureScreenshotResult*
snow_capture_desktop_session_capture(SnowCaptureDesktopSession* session,
                                     const SnowCaptureScreenshotRequest*) {
    ++captured;
    if (failCapture) {
        return nullptr;
    }
    return new SnowCaptureScreenshotResult{
        session->config.capture_backend, session->config.pixel_format, {}};
}

size_t snow_capture_screenshot_result_display_count(const SnowCaptureScreenshotResult*) {
    return 1;
}

uint8_t snow_capture_screenshot_result_display_info(const SnowCaptureScreenshotResult* result,
                                                    size_t, SnowCaptureFrameInfo* info) {
    *info = {};
    info->width = 1;
    info->height = 1;
    info->stride_bytes = 4;
    info->rgba_bytes = result->frame.pixels->data();
    info->rgba_len = result->frame.pixels->size();
    info->backend_kind = result->backend;
    info->pixel_format = result->pixelFormat;
    return 1;
}

SnowCaptureFrameLease*
snow_capture_screenshot_result_display_retain(const SnowCaptureScreenshotResult* result, size_t) {
    ++leases;
    return new SnowCaptureFrameLease(result->frame);
}

void snow_capture_frame_lease_release(SnowCaptureFrameLease* lease) {
    --leases;
    delete lease;
}

void snow_capture_screenshot_result_destroy(SnowCaptureScreenshotResult* result) {
    delete result;
}

const char* snow_capture_last_error_message() {
    return "injected native capture failure";
}

SnowCaptureCancellationToken* snow_capture_cancellation_token_create() {
    return new SnowCaptureCancellationToken;
}
void snow_capture_cancellation_token_cancel(SnowCaptureCancellationToken*) {}
void snow_capture_cancellation_token_destroy(SnowCaptureCancellationToken* token) {
    delete token;
}
} // extern "C"

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "failed to create isolated settings directory");
    auto& storage = snow_shot::storage::ApplicationStorage::instance();
    static_cast<void>(storage.initialize({temporary.filePath(QStringLiteral("bin")),
                                          temporary.filePath(QStringLiteral("data")), 60000}));
    changedModeReplacesPrewarmedSession();
    allModeTransitionsApplyWithoutRestart();
    preparationAndLayoutRefreshUseCurrentMode();
    failedReplacementDoesNotCaptureWithOldBackend();
    modeChangeAfterCaptureFailurePreservesRetainedFrame();
    require(created == destroyed, "worker leaked a native capture session");
    require(leases == 0, "worker leaked a native frame lease");
    storage.shutdown();
    return 0;
}
