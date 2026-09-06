#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTSOURCEIMAGECOMPOSER_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTSOURCEIMAGECOMPOSER_H

#include <QImage>
#include <QRect>

class ScreenshotDisplaySession;

[[nodiscard]] QImage
composeScreenshotSourceSelection(const ScreenshotDisplaySession& displaySession,
                                 const QRect& selection);

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTSOURCEIMAGECOMPOSER_H
