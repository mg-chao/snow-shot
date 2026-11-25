#pragma once

#include "snow_shot/presentation/screenshotocrpresentation.h"

namespace snow_shot::ocr {
// Fresh detector output uses source pixels. Horizontal rows and vertical columns
// contribute to separate orientation estimates before mapping to the canvas.
void prepareDetectedLineGeometry(QVector<ScreenshotOcrLine>& lines, const QSize& imageSize,
                                 const QRectF& canvasRect);
} // namespace snow_shot::ocr
