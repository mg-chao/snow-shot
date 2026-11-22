// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "snow_shot/platform/screenshotwindownames.h"
#include "snow_shot/presentation/screenshotdisplaysession.h"
#include "snow_shot/presentation/screenshotgeometry.h"

#include <QGuiApplication>
#include <QScreen>

// Match in native desktop units (Quartz points on macOS, physical pixels on Windows).
[[nodiscard]] inline QString screenshotScreenNameForRegion(const QRect& region,
                                                           const QList<QRect>& screens) {
    qsizetype match = -1;
    for (qsizetype i = 0; i < screens.size(); ++i) {
        if (!region.intersects(screens[i]))
            continue;
        if (match >= 0)
            return QStringLiteral("Multi");
        match = i;
    }
    return match < 0 ? QString{} : QStringLiteral("Display-%1").arg(match + 1);
}

[[nodiscard]] inline QString screenshotScreenNameForNativeRegion(const QRect& region) {
    QList<QRect> bounds;
    for (auto* screen : QGuiApplication::screens()) {
#ifdef Q_OS_MACOS
        bounds.append(screen->geometry());
#else
        bounds.append(ScreenshotGeometryMapper::physicalRectForScreen(*screen));
#endif
    }
    return screenshotScreenNameForRegion(region, bounds);
}

// Resolve once at the export boundary, while the selection and native exclusions still exist.
// The callback also lets offscreen tests exercise targeting without querying the user's desktop.
template <typename Lookup>
snow_shot::storage::ScreenshotExportContext
screenshotWindowNamesForExport(const QString& directory, const QString& filename,
                               const QRect& selection, const ScreenshotDisplaySession& displays,
                               const ScreenshotGeometryMapper& geometry,
                               std::span<const std::uintptr_t> excludedWindows, Lookup lookup) {
    const auto fields = snow_shot::storage::screenshotWindowNameFields(directory, filename);
    if (!fields.any() || selection.isEmpty() || displays.hasImageSources())
        return {};
    const QPointF center = ScreenshotHalfOpenRect::fromRect(selection).center();
    const auto* display = geometry.displayForCanvasPoint(displays, center);
    // A gap between displays is not a window on the nearest display.
    if (!display)
        return {};
    const QPointF point = display->canvasUsesPoints
                              ? geometry.logicalPositionForCanvasPoint(*display, center)
                              : geometry.physicalPositionForCanvasPoint(displays, center);
    return lookup(point, excludedWindows, fields);
}
