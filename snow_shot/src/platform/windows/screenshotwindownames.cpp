// SPDX-License-Identifier: GPL-3.0-only
#include "snow_shot/platform/screenshotwindownames.h"
#include "../screenshotwindownamereader_p.h"

#ifdef Q_OS_WIN
#include <QFileInfo>
#include <QtMath>
#include <qt_windows.h>
#include <dwmapi.h>
#include <winver.h>
#include <algorithm>
#include <vector>

namespace {
QString applicationName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    std::vector<wchar_t> path(32768);
    DWORD length = static_cast<DWORD>(path.size());
    const bool found = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
    CloseHandle(process);
    if (!found)
        return {};
    const QString executable = QString::fromWCharArray(path.data(), static_cast<int>(length));
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.data(), &ignored);
    std::vector<BYTE> version(size);
    if (size && GetFileVersionInfoW(path.data(), 0, size, version.data())) {
        struct Translation {
            WORD language;
            WORD codePage;
        };
        Translation* translations = nullptr;
        UINT bytes = 0;
        if (VerQueryValueW(version.data(), L"\\VarFileInfo\\Translation",
                           reinterpret_cast<void**>(&translations), &bytes)) {
            for (UINT i = 0; i < bytes / sizeof(Translation); ++i) {
                const QString key = QStringLiteral("\\StringFileInfo\\%1%2\\FileDescription")
                                        .arg(translations[i].language, 4, 16, QLatin1Char('0'))
                                        .arg(translations[i].codePage, 4, 16, QLatin1Char('0'));
                wchar_t* description = nullptr;
                UINT characters = 0;
                if (VerQueryValueW(version.data(), reinterpret_cast<LPCWSTR>(key.utf16()),
                                   reinterpret_cast<void**>(&description), &characters) &&
                    characters > 1) {
                    const QString name =
                        QString::fromWCharArray(description, static_cast<int>(characters - 1))
                            .trimmed();
                    if (!name.isEmpty())
                        return name;
                }
            }
        }
    }
    return QFileInfo(executable).completeBaseName();
}
} // namespace
#endif

namespace snow_shot::platform {
storage::ScreenshotExportContext
screenshotWindowNamesForWindow(std::uintptr_t windowId,
                               storage::ScreenshotWindowNameFields fields) {
#ifdef Q_OS_WIN
    if (windowId == 0 || !fields.any())
        return {};
    const auto window = reinterpret_cast<HWND>(windowId);
    return detail::readScreenshotWindowNames(
        fields,
        [window] {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            return applicationName(pid);
        },
        [window] {
            // GetWindowText reads another process's caption without messaging its UI thread.
            std::vector<wchar_t> title(static_cast<size_t>(GetWindowTextLengthW(window)) + 1);
            const int length = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
            return QString::fromWCharArray(title.data(), length);
        });
#else
    Q_UNUSED(windowId);
    Q_UNUSED(fields);
    return {};
#endif
}

storage::ScreenshotExportContext
screenshotWindowNamesAtPoint(const QPointF& point, std::span<const std::uintptr_t> excludedWindows,
                             storage::ScreenshotWindowNameFields fields) {
    if (!fields.any())
        return {};
#ifdef Q_OS_WIN
    const POINT position{qFloor(point.x()), qFloor(point.y())};
    for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
        const auto id = reinterpret_cast<std::uintptr_t>(window);
        if (std::find(excludedWindows.begin(), excludedWindows.end(), id) !=
                excludedWindows.end() ||
            !IsWindowVisible(window) || IsIconic(window))
            continue;
        constexpr LONG_PTR passThrough = WS_EX_LAYERED | WS_EX_TRANSPARENT;
        if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & passThrough) == passThrough)
            continue;
        RECT bounds{};
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds,
                                         sizeof(bounds))) &&
            !GetWindowRect(window, &bounds))
            continue;
        if (!PtInRect(&bounds, position))
            continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
            cloaked)
            continue;
        return screenshotWindowNamesForWindow(id, fields);
    }
#else
    Q_UNUSED(point);
    Q_UNUSED(excludedWindows);
#endif
    return {};
}
} // namespace snow_shot::platform
