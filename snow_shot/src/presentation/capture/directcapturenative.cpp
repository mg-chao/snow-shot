#include "directcapturenative.h"
#include "captureframeimage.h"
#include "captureframegeometry.h"

#include <atomic>
#include <limits>
#include <memory>
#include <QCoreApplication>
#include <utility>

namespace snow_shot::presentation {
namespace {
QString nativeError() {
    return QString::fromUtf8(snow_capture_last_error_message());
}

} // namespace

DirectCaptureFrame captureDirectTarget(const DirectCaptureRequest& request) {
    DirectCaptureFrame result;
    const bool focusedWindow = request.target == DirectCaptureTarget::FocusedWindow;
    const bool fullHistory = request.historyMode == storage::CaptureHistoryMode::Full;
    if (focusedWindow ? request.window == 0 : request.monitorName.isEmpty()) {
        return result;
    }
    SnowCaptureDesktopSessionConfig config{};
    config.capture_retry_count = 1;
    if (!focusedWindow)
        config.capture_backend = request.captureBackend;
    config.pixel_format = SNOW_CAPTURE_PIXEL_FORMAT_BGRA8;
    SnowCaptureScreenshotRequest nativeRequest{};
    nativeRequest.version = SNOW_CAPTURE_SCREENSHOT_REQUEST_VERSION;
    nativeRequest.struct_size = sizeof(nativeRequest);
    if (request.captureCursor)
        nativeRequest.flags |= SNOW_CAPTURE_SCREENSHOT_REQUEST_INCLUDE_CURSOR;
    if (request.restoreOriginalScreenColors) {
        nativeRequest.flags |= SNOW_CAPTURE_SCREENSHOT_REQUEST_RESTORE_ORIGINAL_COLORS;
    }
    const QByteArray monitorIdentity = request.monitorName.toUtf8();
    SnowCaptureTargetScreenshotRequest targetRequest{};
    targetRequest.version = SNOW_CAPTURE_TARGET_SCREENSHOT_REQUEST_VERSION;
    targetRequest.struct_size = sizeof(targetRequest);
    targetRequest.screenshot = nativeRequest;
    if (focusedWindow)
        targetRequest.screenshot.focused_window = static_cast<intptr_t>(request.window);
    targetRequest.monitor_utf8 = focusedWindow ? nullptr : monitorIdentity.constData();
    targetRequest.pixel_format = config.pixel_format;
    targetRequest.capture_backend = config.capture_backend;
    struct Admission {
        const DirectCaptureRequest& request;
        std::atomic_bool rejected{false};
    } admission{request};
    if (request.admitRaster) {
        targetRequest.admission_context = &admission;
        targetRequest.admit_raster = [](void* context, uint32_t width,
                                        uint32_t height) noexcept -> uint8_t {
            auto& state = *static_cast<Admission*>(context);
            if (state.rejected.load(std::memory_order_acquire))
                return 0;
            bool accepted = false;
            try {
                if (width > 0 && height > 0 &&
                    width <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
                    height <= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
                    accepted = state.request.admitRaster(
                        QSize(static_cast<int>(width), static_cast<int>(height)));
                }
            } catch (...) {
                // Exceptions must never unwind through the native C ABI.
            }
            if (!accepted)
                state.rejected.store(true, std::memory_order_release);
            return accepted ? 1 : 0;
        };
    }
    using Snapshot = std::unique_ptr<SnowCaptureScreenshotResult,
                                     decltype(&snow_capture_screenshot_result_destroy)>;
    Snapshot targetSnapshot(nullptr, snow_capture_screenshot_result_destroy);
    if (focusedWindow || !fullHistory) {
        // Freeze the focused window before creating the desktop workers for history.
        targetSnapshot.reset(snow_capture_target_screenshot(&targetRequest));
        const bool rejected = admission.rejected.load(std::memory_order_acquire);
        if (!targetSnapshot || rejected) {
            result.error = rejected ? QStringLiteral("resource_limit") : nativeError();
            return result;
        }
    }
    Snapshot desktopSnapshot(nullptr, snow_capture_screenshot_result_destroy);
    if (fullHistory) {
        const std::unique_ptr<SnowCaptureDesktopSession,
                              decltype(&snow_capture_desktop_session_destroy)>
            session(snow_capture_desktop_session_create(&config),
                    snow_capture_desktop_session_destroy);
        if (session)
            desktopSnapshot.reset(
                snow_capture_desktop_session_capture(session.get(), &nativeRequest));
        if (!desktopSnapshot) {
            result.error = nativeError();
            return result;
        }
    }
    const auto* displaySnapshot = fullHistory ? desktopSnapshot.get() : targetSnapshot.get();
    const size_t count = snow_capture_screenshot_result_display_count(displaySnapshot);
    for (size_t index = 0; index < count; ++index) {
        SnowCaptureFrameInfo info{};
        if (!snow_capture_screenshot_result_display_info(displaySnapshot, index, &info) ||
            !capture::validFrameInfo(info)) {
            result.error = nativeError();
            break;
        }
        DirectCaptureDisplay display;
        display.image = capture::imageFromFrameLease(
            snow_capture_screenshot_result_display_retain(displaySnapshot, index), info.rgba_bytes,
            info.rgba_len, info.width, info.height, info.stride_bytes, info.pixel_format,
            capture::FrameAlphaMode::Opaque);
        display.physicalBounds =
            QRect(info.x, info.y, static_cast<int>(info.width), static_cast<int>(info.height));
#ifdef Q_OS_MACOS
        SnowCaptureFrameGeometry geometry{};
        geometry.version = SNOW_CAPTURE_FRAME_GEOMETRY_VERSION;
        geometry.struct_size = sizeof(geometry);
        if (!snow_capture_screenshot_result_display_geometry(displaySnapshot, index, &geometry)) {
            result.error = nativeError();
            break;
        }
        const auto logicalBounds = capture::logicalFrameRect(geometry);
        if (!logicalBounds) {
            result.error = QCoreApplication::translate("DirectCaptureController",
                                                       "The captured display geometry is invalid");
            break;
        }
        display.logicalBounds = *logicalBounds;
        display.nativeDisplayId = geometry.display_id;
#endif
        display.stableId = QString::fromUtf8(info.stable_id);
        display.name = QString::fromUtf8(info.name);
        bool currentMonitor =
            display.name == request.monitorName || display.stableId == request.monitorName;
#ifdef Q_OS_MACOS
        currentMonitor =
            currentMonitor ||
            request.monitorName == QStringLiteral("display:%1").arg(display.nativeDisplayId);
#endif
        if (!focusedWindow && currentMonitor) {
            result.image = display.image;
            result.logicalBounds = display.logicalBounds;
            result.physicalBounds = display.physicalBounds;
            result.identity = display.stableId;
            result.backend = info.backend_kind;
        }
        if (display.image.isNull())
            break;
        result.displays.push_back(std::move(display));
    }
    if (((fullHistory || !focusedWindow) && result.displays.isEmpty()) ||
        static_cast<size_t>(result.displays.size()) != count) {
        result.image = {};
        result.displays.clear();
        return result;
    }
    if (focusedWindow) {
        SnowCaptureWindowFrameInfo info{};
        info.version = SNOW_CAPTURE_WINDOW_FRAME_INFO_VERSION;
        info.struct_size = sizeof(info);
        if (!snow_capture_screenshot_result_focused_window_info(targetSnapshot.get(), &info)) {
            result.error = nativeError();
            return result;
        }
        result.image = capture::imageFromFrameLease(
            snow_capture_screenshot_result_focused_window_retain(targetSnapshot.get()),
            info.rgba_bytes, info.rgba_len, info.width, info.height, info.stride_bytes,
            info.pixel_format,
            info.backend_kind == SNOW_CAPTURE_BACKEND_SCREEN_CAPTURE_KIT
                ? capture::FrameAlphaMode::Premultiplied
                : capture::FrameAlphaMode::Preserve);
        result.physicalBounds =
            QRect(info.x, info.y, static_cast<int>(info.width), static_cast<int>(info.height));
#ifdef Q_OS_MACOS
        SnowCaptureFrameGeometry geometry{};
        geometry.version = SNOW_CAPTURE_FRAME_GEOMETRY_VERSION;
        geometry.struct_size = sizeof(geometry);
        if (!snow_capture_screenshot_result_focused_window_geometry(targetSnapshot.get(),
                                                                    &geometry)) {
            result.error = nativeError();
            return result;
        }
        const auto logicalBounds = capture::logicalFrameRect(geometry);
        if (!logicalBounds) {
            result.error = QCoreApplication::translate("DirectCaptureController",
                                                       "The captured window geometry is invalid");
            return result;
        }
        result.logicalBounds = *logicalBounds;
#endif
        result.identity = QStringLiteral("window:%1").arg(request.window);
        result.backend = info.backend_kind;
    }
    return result;
}
} // namespace snow_shot::presentation
