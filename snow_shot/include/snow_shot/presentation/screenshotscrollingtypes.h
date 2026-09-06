#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTSCROLLINGTYPES_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTSCROLLINGTYPES_H

enum class ScreenshotScrollingRecognitionMode {
    Vertical,
    Horizontal,
};

enum class ScreenshotScrollingStitchChange {
    Initial,
    AppendedDown,
    PrependedUp,
    AppendedRight,
    PrependedLeft,
    Replaced,
};

struct ScreenshotScrollingTrimRange {
    int top = 0;
    int bottom = 0;

    [[nodiscard]] bool isValid() const {
        return bottom > top;
    }
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTSCROLLINGTYPES_H
