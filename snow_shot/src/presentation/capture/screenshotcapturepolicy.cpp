#include "snow_shot/presentation/capture/screenshotcapturepolicy.h"

#include <QGuiApplication>
#include <QVector>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include "screenshotcapturepolicy_p.h"
#endif

#include "snow_capture.h"

namespace snow_shot::presentation::capture {

#if defined(Q_OS_WIN) || defined(_WIN32)
namespace {
detail::AutoScreenshotModeCache autoModeCache;

std::optional<ScreenshotApiMode> queryAutoScreenshotMode() {
    UINT pathCount = 0;
    UINT modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) !=
            ERROR_SUCCESS ||
        pathCount == 0)
        return std::nullopt;
    QVector<DISPLAYCONFIG_PATH_INFO> paths(static_cast<qsizetype>(pathCount));
    QVector<DISPLAYCONFIG_MODE_INFO> modes(static_cast<qsizetype>(modeCount));
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                           modes.data(), nullptr) != ERROR_SUCCESS ||
        pathCount == 0)
        return std::nullopt;
    bool allTargetsKnown = true;
    for (UINT index = 0; index < pathCount; ++index) {
        const auto hdr = detail::displayHdrEnabled(paths[index].targetInfo);
        if (hdr.value_or(false))
            return detail::autoScreenshotApiMode(true);
        allTargetsKnown &= hdr.has_value();
    }
    if (!allTargetsKnown)
        return std::nullopt;
    return detail::autoScreenshotApiMode(false);
}
} // namespace

ScreenshotApiMode detail::autoScreenshotApiMode(bool anyActiveHdr) noexcept {
    return anyActiveHdr ? ScreenshotApiMode::Wgc : ScreenshotApiMode::Gdi;
}

std::optional<bool> detail::displayHdrEnabled(const DISPLAYCONFIG_PATH_TARGET_INFO& target,
                                              DisplayColorInfoQuery query) noexcept {
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 current{};
    current.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
    current.header.size = sizeof(current);
    current.header.adapterId = target.adapterId;
    current.header.id = target.id;
    if (query(&current.header) == ERROR_SUCCESS) {
        // Advanced Color also covers SDR automatic color management. Only active HDR prefers WGC.
        return current.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
    }

    // Older Windows versions expose only the legacy flags; WCG is not HDR there either.
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info{};
    info.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    info.header.size = sizeof(info);
    info.header.adapterId = target.adapterId;
    info.header.id = target.id;
    const auto status = query(&info.header);
    if (status == ERROR_NOT_SUPPORTED)
        return false;
    if (status != ERROR_SUCCESS)
        return std::nullopt;
    return info.advancedColorSupported && info.advancedColorEnabled && !info.wideColorEnforced;
}
#endif

ScreenshotApiMode screenshotApiModeFromValue(const char* value) noexcept {
    if (value != nullptr) {
        const QString mode = QString::fromLatin1(value);
        if (mode == QStringLiteral("dxgi"))
            return ScreenshotApiMode::Dxgi;
        if (mode == QStringLiteral("wgc"))
            return ScreenshotApiMode::Wgc;
        if (mode == QStringLiteral("gdi"))
            return ScreenshotApiMode::Gdi;
    }
    return ScreenshotApiMode::Auto;
}

std::uint8_t nativeBackendForNormalScreenshot(ScreenshotApiMode mode) noexcept {
#ifdef Q_OS_MACOS
    Q_UNUSED(mode);
    return SNOW_CAPTURE_BACKEND_SCREEN_CAPTURE_KIT;
#else
    if (mode == ScreenshotApiMode::Auto)
        mode = resolveAutoScreenshotApiMode();
    switch (mode) {
    case ScreenshotApiMode::Dxgi:
        return SNOW_CAPTURE_BACKEND_DXGI;
    case ScreenshotApiMode::Wgc:
        return SNOW_CAPTURE_BACKEND_WGC;
    case ScreenshotApiMode::Gdi:
        return SNOW_CAPTURE_BACKEND_GDI;
    case ScreenshotApiMode::Auto:
        break;
    }
    return SNOW_CAPTURE_BACKEND_AUTO;
#endif
}

ScreenshotApiMode resolveAutoScreenshotApiMode() noexcept {
#ifdef Q_OS_MACOS
    return ScreenshotApiMode::Auto;
#elif defined(Q_OS_WIN) || defined(_WIN32)
    return autoModeCache.resolve(queryAutoScreenshotMode);
#else
    return ScreenshotApiMode::Gdi;
#endif
}

void invalidateAutoScreenshotApiMode() noexcept {
#if defined(Q_OS_WIN) || defined(_WIN32)
    autoModeCache.invalidate();
#endif
}

} // namespace snow_shot::presentation::capture
