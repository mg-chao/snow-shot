#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTSAVEDIALOGOWNER_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTSAVEDIALOGOWNER_H

#include "snow_shot/presentation/screenshotdisplaysession.h"
#include "snow_shot/presentation/screenshotgeometry.h"

[[nodiscard]] inline ScreenshotOverlayWindow*
screenshotSaveDialogOwner(const ScreenshotDisplaySession& displays,
                          const ScreenshotGeometryMapper& geometry, const QRectF& selection,
                          ScreenshotOverlayWindow* keyboardOwner) {
    if (!selection.isEmpty()) {
        const auto* display = geometry.displayForCanvasRect(displays, selection);
        if (auto* overlay = displays.overlayForDisplay(display)) {
            return overlay;
        }
    }
    return keyboardOwner;
}

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTSAVEDIALOGOWNER_H
