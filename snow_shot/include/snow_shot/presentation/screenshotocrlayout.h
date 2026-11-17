#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTOCRLAYOUT_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTOCRLAYOUT_H

#include "snow_shot/presentation/screenshotocrpresentation.h"

namespace snow_shot::presentation {
// Membership comes exclusively from the paragraph operation. Spatial ordering is independent.
[[nodiscard]] std::optional<QVector<ScreenshotOcrLine>>
mergeOcrParagraphs(const QVector<ScreenshotOcrLine>& lines, const QVector<QVector<int>>& groups,
                   QVector<QVector<int>>* sourceIndices = nullptr);
// STranslate Smart layout adapted to image-coordinate OCR lines. The input is never modified.
// Legacy saved-translation compatibility only. New requests use mergeOcrParagraphs.
[[nodiscard]] QVector<ScreenshotOcrLine>
mergeOcrLayout(const QVector<ScreenshotOcrLine>& lines, const QPointF& imageOrigin = {},
               QVector<QVector<int>>* sourceIndices = nullptr);
// Match the captured layout, independently of the current Smart Merge setting.
// For legacy captures, report rows that need their paragraph flag restored.
[[nodiscard]] std::optional<QVector<QVector<int>>> ocrTranslationSourceIndices(
    const ScreenshotOcrPresentation& translation, const ScreenshotOcrPresentation& source,
    QVector<int>* paragraphsToRestore = nullptr, bool* smartMerge = nullptr);
} // namespace snow_shot::presentation

#endif
