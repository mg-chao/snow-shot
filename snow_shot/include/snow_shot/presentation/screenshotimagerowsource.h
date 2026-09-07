#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTIMAGEROWSOURCE_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTIMAGEROWSOURCE_H

#include <QImage>
#include <QSize>
#include <QtGlobal>

#include <functional>

struct ScreenshotImageRowSource final {
    QSize size;
    // Rows contain straight-alpha RGBA8888 pixels in sRGB, ordered top to bottom.
    std::function<bool(int firstRow, int rowCount, qsizetype destinationStride, uchar* destination,
                       qsizetype destinationSize)>
        readRows;
    std::function<bool()> cancellationRequested;
    // Optional immutable storage for the same pixels exposed by readRows. A null image denotes a
    // genuinely row-backed source that should not be materialized merely to provide a preview.
    QImage backingImage;

    [[nodiscard]] bool isValid() const {
        return size.isValid() && !size.isEmpty() && static_cast<bool>(readRows);
    }
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTIMAGEROWSOURCE_H
