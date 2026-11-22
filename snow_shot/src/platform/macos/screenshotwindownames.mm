// SPDX-License-Identifier: GPL-3.0-only
#include "snow_shot/platform/screenshotwindownames.h"
#include "selectorwindowtarget_p.h"
#include "../screenshotwindownamereader_p.h"

namespace snow_shot::platform {
storage::ScreenshotExportContext
screenshotWindowNamesAtPoint(const QPointF& point, std::span<const std::uintptr_t> excludedWindows,
                             storage::ScreenshotWindowNameFields fields) {
    if (!fields.any() || QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return {};
    return screenshotWindowNamesForWindow(
        detail::selectorWindowAtDesktopPoint(point, excludedWindows), fields);
}

storage::ScreenshotExportContext
screenshotWindowNamesForWindow(std::uintptr_t windowId,
                               storage::ScreenshotWindowNameFields fields) {
    if (windowId == 0 || !fields.any() ||
        QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return {};
    const auto hit = static_cast<CGWindowID>(windowId);
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, hit);
    const auto target = detail::recaptureWindowTarget(windows, hit);
    storage::ScreenshotExportContext result;
    if (target.id) {
        result = detail::readScreenshotWindowNames(
            fields,
            [pid = target.pid] {
                NSRunningApplication* application =
                    [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
                QString name = QString::fromNSString(application.localizedName);
                if (name.trimmed().isEmpty())
                    name = QString::fromNSString(
                        application.executableURL.lastPathComponent.stringByDeletingPathExtension);
                return name;
            },
            [windows, hit] {
                for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
                    const auto window =
                        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
                    CGWindowID id = 0;
                    const auto number =
                        static_cast<CFNumberRef>(CFDictionaryGetValue(window, kCGWindowNumber));
                    if (!number || !CFNumberGetValue(number, kCFNumberIntType, &id) || id != hit)
                        continue;
                    const auto title = CFDictionaryGetValue(window, kCGWindowName);
                    if (title && CFGetTypeID(title) == CFStringGetTypeID())
                        return QString::fromCFString(static_cast<CFStringRef>(title));
                    break;
                }
                return QString();
            });
    }
    if (windows)
        CFRelease(windows);
    return result;
}
} // namespace snow_shot::platform
