#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTCAPTUREDISPLAYMODELRECONCILER_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTCAPTUREDISPLAYMODELRECONCILER_H

#include "snow_shot/presentation/screenshottypes.h"

#include <QVector>

class ScreenshotDisplaySession;

class ScreenshotCaptureDisplayModelReconciler final {
  public:
    ScreenshotCaptureDisplayModelReconciler() = delete;

    static void applySnapshots(ScreenshotDisplaySession& displaySession,
                               const QVector<CapturedDisplayModel>& snapshots);
    static void clearCaptureMetadata(CapturedDisplayModel& display);
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTCAPTUREDISPLAYMODELRECONCILER_H
