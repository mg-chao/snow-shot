// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "snow_shot/storage/screenshotexportvariables.h"
#include <QPointF>
#include <cstdint>
#include <span>

namespace snow_shot::platform {
// GUI thread. The ID is an HWND on Windows or a CGWindowID on macOS.
[[nodiscard]] storage::ScreenshotExportContext
screenshotWindowNamesForWindow(std::uintptr_t windowId, storage::ScreenshotWindowNameFields fields);

// GUI thread. Native desktop coordinates: physical pixels on Windows, Quartz points on macOS.
// Only requested fields are read; an empty field set performs no native lookup.
[[nodiscard]] storage::ScreenshotExportContext
screenshotWindowNamesAtPoint(const QPointF& point, std::span<const std::uintptr_t> excludedWindows,
                             storage::ScreenshotWindowNameFields fields);
} // namespace snow_shot::platform
