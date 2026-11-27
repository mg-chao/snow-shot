#ifndef SNOW_SHOT_NATIVEPRINTBACKEND_H
#define SNOW_SHOT_NATIVEPRINTBACKEND_H

#include "snow_shot/presentation/screenshotprintservice.h"

[[nodiscard]] ScreenshotPrintService::NativeBackend screenshotNativePrintBackend(bool legacy);
#ifdef Q_OS_WIN
// Compatibility alias for the Windows legacy Photo Printing Wizard backend.
[[nodiscard]] ScreenshotPrintService::NativeBackend screenshotClassicWindowsPrintBackend();
#endif

#endif
