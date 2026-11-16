#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTCAPTUREPIXELS_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTCAPTUREPIXELS_H

#include <QImage>

#include <algorithm>

struct ScreenshotCapturePixel {
    int red;
    int green;
    int blue;
    int alpha;
};

[[nodiscard]] inline QPoint screenshotClampedCaptureImagePoint(const QImage& image,
                                                               const QPoint& imagePoint) {
    return {std::clamp(imagePoint.x(), 0, image.width() - 1),
            std::clamp(imagePoint.y(), 0, image.height() - 1)};
}

[[nodiscard]] inline bool screenshotCaptureCursorPatchFits(const QImage& image,
                                                           const QImage& cursorPatch,
                                                           const QRect& cursorPixelRect) {
    return !cursorPatch.isNull() && cursorPatch.size() == cursorPixelRect.size() &&
           image.rect().contains(cursorPixelRect);
}

[[nodiscard]] inline ScreenshotCapturePixel screenshotCapturePixelAt(const QImage& image,
                                                                     const QPoint& point) {
    if (image.format() == QImage::Format_RGBA8888) {
        const uchar* pixel = image.constScanLine(point.y()) + point.x() * 4;
        return {pixel[0], pixel[1], pixel[2], pixel[3]};
    }

    const QColor color = image.pixelColor(point).toRgb();
    return {color.red(), color.green(), color.blue(), color.alpha()};
}

[[nodiscard]] inline ScreenshotCapturePixel screenshotCapturePixelAt(const QImage& image,
                                                                     const QImage& cursorPatch,
                                                                     const QRect& cursorPixelRect,
                                                                     const QPoint& imagePoint) {
    // The validated patch already contains the cursor blended into the capture.
    if (!cursorPatch.isNull() && cursorPixelRect.contains(imagePoint)) {
        return screenshotCapturePixelAt(cursorPatch, imagePoint - cursorPixelRect.topLeft());
    }
    return screenshotCapturePixelAt(image, imagePoint);
}

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTCAPTUREPIXELS_H
