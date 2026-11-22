// SPDX-License-Identifier: GPL-3.0-only
#include "snow_shot/platform/screenshotwindownames.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QScreen>
#include <QThread>
#include <QWidget>
#include <cstdlib>
#include <iostream>

#ifdef Q_OS_MACOS
#include <AppKit/AppKit.h>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::uintptr_t nativeId(QWidget& widget) {
#ifdef Q_OS_MACOS
    return static_cast<std::uintptr_t>(
        reinterpret_cast<NSView*>(widget.winId()).window.windowNumber);
#else
    return static_cast<std::uintptr_t>(widget.winId());
#endif
}

QPointF nativeCenter(QWidget& widget) {
#ifdef Q_OS_WIN
    RECT bounds{};
    require(GetWindowRect(reinterpret_cast<HWND>(widget.winId()), &bounds),
            "fixture bounds missing");
    return QPointF((bounds.left + bounds.right) / 2.0, (bounds.top + bounds.bottom) / 2.0);
#elif defined(Q_OS_MACOS)
    const NSRect frame = reinterpret_cast<NSView*>(widget.winId()).window.frame;
    return QPointF(NSMidX(frame), NSMaxY(NSScreen.screens.firstObject.frame) - NSMidY(frame));
#else
    return widget.frameGeometry().center();
#endif
}

void settle() {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 150) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_MACOS
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    const auto locked = session ? static_cast<CFBooleanRef>(CFDictionaryGetValue(
                                      session, CFSTR("CGSSessionScreenIsLocked")))
                                : nullptr;
    const bool unavailable = !session || (locked && CFGetTypeID(locked) == CFBooleanGetTypeID() &&
                                          CFBooleanGetValue(locked));
    if (session)
        CFRelease(session);
    if (unavailable) {
        std::cout << "SKIP: native window naming fixture requires an unlocked desktop\n";
        return 77;
    }
#endif
    QWidget target(nullptr, Qt::Window | Qt::WindowStaysOnTopHint);
    target.setWindowTitle(QStringLiteral("Snow Shot naming target / 文档"));
    const QPoint center = app.primaryScreen()->availableGeometry().center();
    target.setGeometry(QRect(center - QPoint(160, 100), QSize(320, 200)));
    target.show();
    target.raise();
#ifdef Q_OS_MACOS
    NSWindow* nativeTarget = reinterpret_cast<NSView*>(target.winId()).window;
    [nativeTarget orderFrontRegardless];
#endif
    settle();
    const QPointF point = nativeCenter(target);
    const snow_shot::storage::ScreenshotWindowNameFields both{true, true};
    const auto original = snow_shot::platform::screenshotWindowNamesAtPoint(point, {}, both);
    require(original.windowName == target.windowTitle() && !original.applicationName.isEmpty(),
            "native lookup must return the exact window title and a friendly or executable name");
    require(snow_shot::platform::screenshotWindowNamesAtPoint(point, {}, {false, true}) ==
                snow_shot::storage::ScreenshotExportContext{{}, original.windowName},
            "title-only lookups must leave the application name empty");
    require(snow_shot::platform::screenshotWindowNamesAtPoint(point, {}, {true, false}) ==
                snow_shot::storage::ScreenshotExportContext{original.applicationName, {}},
            "application-only lookups must leave the window title empty");
    require(snow_shot::platform::screenshotWindowNamesAtPoint(point, {}, {}) ==
                snow_shot::storage::ScreenshotExportContext{},
            "requests without fields must return no native metadata");
    QWidget overlay(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
    overlay.setWindowTitle(QStringLiteral("Snow Shot naming overlay"));
    overlay.setGeometry(target.geometry());
    overlay.show();
    overlay.raise();
#ifdef Q_OS_MACOS
    NSWindow* nativeOverlay = reinterpret_cast<NSView*>(overlay.winId()).window;
    [nativeOverlay orderFrontRegardless];
#endif
    settle();
    require(snow_shot::platform::screenshotWindowNamesAtPoint(point, {}, both).windowName ==
                overlay.windowTitle(),
            "the frontmost window must own the target point");
    require(snow_shot::platform::screenshotWindowNamesForWindow(nativeId(target), both) == original,
            "focused capture must read the specified window even when another window covers it");
    const std::uintptr_t excluded[] = {nativeId(overlay)};
    require(snow_shot::platform::screenshotWindowNamesAtPoint(point, excluded, both) == original,
            "excluding a capture overlay must reveal the same underlying window");
    require(snow_shot::platform::screenshotWindowNamesAtPoint(point, excluded, {false, true}) ==
                snow_shot::storage::ScreenshotExportContext{{}, original.windowName},
            "selective reads must preserve capture overlay exclusions");
    return EXIT_SUCCESS;
}
