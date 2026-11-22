// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "snow_shot/storage/screenshotexportvariables.h"

namespace snow_shot::platform::detail {
template <typename ApplicationNameLookup, typename WindowNameLookup>
storage::ScreenshotExportContext
readScreenshotWindowNames(storage::ScreenshotWindowNameFields fields,
                          ApplicationNameLookup applicationName, WindowNameLookup windowName) {
    storage::ScreenshotExportContext result;
    // Snapshot the mutable title before potentially slower application metadata reads.
    if (fields.windowName)
        result.windowName = windowName();
    if (fields.applicationName)
        result.applicationName = applicationName();
    return result;
}
} // namespace snow_shot::platform::detail
