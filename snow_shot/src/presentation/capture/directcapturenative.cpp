#include "directcapturenative.h"
#include "captureframeimage.h"

#include <memory>

namespace snow_shot::presentation {
namespace {
QString nativeError() {
    return QString::fromUtf8(snow_capture_last_error_message());
}

} // namespace

DirectCaptureFrame captureDirectTarget(const DirectCaptureRequest& request) {
    DirectCaptureFrame result;
    if (request.target == DirectCaptureTarget::FocusedWindow) {
        SnowCaptureWindowSessionConfig config{};
        config.hwnd = static_cast<intptr_t>(request.window);
        config.capture_retry_count = 1;
        config.pixel_format = SNOW_CAPTURE_PIXEL_FORMAT_BGRA8;
        const std::unique_ptr<SnowCaptureWindowSession,
                              decltype(&snow_capture_window_session_destroy)>
            session(snow_capture_window_session_create(&config),
                    snow_capture_window_session_destroy);
        SnowCaptureWindowFrameInfo info{};
        info.version = SNOW_CAPTURE_WINDOW_FRAME_INFO_VERSION;
        info.struct_size = sizeof(info);
        if (!session || !snow_capture_window_session_capture(session.get(), &info)) {
            result.error = nativeError();
            return result;
        }
        result.image = capture::imageFromFrameLease(
            snow_capture_window_session_frame_retain(session.get()), info.rgba_bytes, info.rgba_len,
            info.width, info.height, info.stride_bytes, info.pixel_format);
        result.physicalBounds =
            QRect(info.x, info.y, static_cast<int>(info.width), static_cast<int>(info.height));
        result.identity = QStringLiteral("window:%1").arg(request.window);
        result.backend = info.backend_kind;
    } else {
        const QByteArray name = request.monitorName.toUtf8();
        SnowCaptureMonitorSessionConfig config{};
        config.device_name_utf8 = name.constData();
        config.capture_retry_count = 1;
        config.pixel_format = SNOW_CAPTURE_PIXEL_FORMAT_BGRA8;
        const std::unique_ptr<SnowCaptureMonitorSession,
                              decltype(&snow_capture_monitor_session_destroy)>
            session(snow_capture_monitor_session_create(&config),
                    snow_capture_monitor_session_destroy);
        SnowCaptureFrameInfo info{};
        if (!session || !snow_capture_monitor_session_capture(session.get(), &info)) {
            result.error = nativeError();
            return result;
        }
        result.image = capture::imageFromFrameLease(
            snow_capture_monitor_session_frame_retain(session.get()), info.rgba_bytes,
            info.rgba_len, info.width, info.height, info.stride_bytes, info.pixel_format);
        result.physicalBounds =
            QRect(info.x, info.y, static_cast<int>(info.width), static_cast<int>(info.height));
        result.identity = QString::fromUtf8(info.stable_id);
        result.backend = info.backend_kind;
    }
    return result;
}
} // namespace snow_shot::presentation
