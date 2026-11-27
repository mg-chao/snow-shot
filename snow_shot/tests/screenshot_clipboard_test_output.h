// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QUrl>
#ifdef Q_OS_WIN
#include <QDeadlineTimer>
#include <QScopeGuard>
#include <QThread>
#pragma push_macro("CopyFile")
#include <qt_windows.h>
#include <shellapi.h>
#pragma pop_macro("CopyFile")
#endif

struct ScreenshotTestClipboardOutput {
    QImage image;
    QString filePath;
    bool operator==(const ScreenshotTestClipboardOutput&) const = default;
};

// Image exports publish to the native Windows clipboard even with the offscreen QPA plugin.
inline ScreenshotTestClipboardOutput readScreenshotClipboardOutput() {
#ifdef Q_OS_WIN
    QDeadlineTimer deadline(2000);
    while (!OpenClipboard(nullptr)) {
        if (deadline.hasExpired())
            return {};
        QThread::msleep(1);
    }
    const auto close = qScopeGuard([] { CloseClipboard(); });
    const auto handle = static_cast<HGLOBAL>(GetClipboardData(RegisterClipboardFormatW(L"PNG")));
    const auto* data = handle ? static_cast<const char*>(GlobalLock(handle)) : nullptr;
    QImage image;
    if (data) {
        image =
            QImage::fromData(QByteArray(data, static_cast<qsizetype>(GlobalSize(handle))), "PNG");
        GlobalUnlock(handle);
    }
    QString path;
    const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (drop && DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) > 0) {
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        path.resize(length + 1);
        DragQueryFileW(drop, 0, reinterpret_cast<LPWSTR>(path.data()), length + 1);
        path.resize(length);
    }
    return {image, path};
#else
    const auto* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return {};
    const auto png = mime->data(QStringLiteral("image/png"));
    return {png.isEmpty() ? QGuiApplication::clipboard()->image() : QImage::fromData(png, "PNG"),
            mime->hasUrls() ? mime->urls().constFirst().toLocalFile() : QString{}};
#endif
}
