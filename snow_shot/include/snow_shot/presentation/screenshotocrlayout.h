#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTOCRLAYOUT_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTOCRLAYOUT_H

#include "snow_shot/presentation/screenshotocrpresentation.h"

namespace snow_shot::presentation {
// STranslate Smart layout adapted to image-coordinate OCR lines. The input is never modified.
[[nodiscard]] QVector<ScreenshotOcrLine>
mergeOcrLayout(const QVector<ScreenshotOcrLine>& lines, const QPointF& imageOrigin = {},
               QVector<QVector<int>>* sourceIndices = nullptr);
// Match the captured layout, independently of the current Smart Merge setting.
// For legacy captures, report rows that need their paragraph flag restored.
[[nodiscard]] std::optional<QVector<QVector<int>>>
ocrTranslationSourceIndices(const ScreenshotOcrPresentation& translation,
                            const ScreenshotOcrPresentation& source,
                            QVector<int>* paragraphsToRestore = nullptr);
} // namespace snow_shot::presentation

#endif
