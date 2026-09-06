#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTCLIPBOARDPOLICY_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTCLIPBOARDPOLICY_H

#include "snow_shot/presentation/screenshotclipboardservice.h"
#include "snow_shot/presentation/screenshotresultcompositor.h"

enum class ScreenshotClipboardScenario {
    OrdinarySelection,
    ScrollingCapture,
    CurrentMonitor,
    Other,
};

class ScreenshotClipboardPolicy final {
  public:
    // Classify the copy action, not the image's opacity or original capture source.
    // An ordinary selection requires its style to opt into compatible DIB output.
    [[nodiscard]] static constexpr ScreenshotClipboardFormatMode
    formatForScenario(ScreenshotClipboardScenario scenario = ScreenshotClipboardScenario::Other) {
        return scenario == ScreenshotClipboardScenario::ScrollingCapture ||
                       scenario == ScreenshotClipboardScenario::CurrentMonitor
                   ? ScreenshotClipboardFormatMode::CompatibleDib
                   : ScreenshotClipboardFormatMode::DibV5;
    }

    [[nodiscard]] static ScreenshotClipboardFormatMode
    formatForScenario(ScreenshotClipboardScenario scenario, const ScreenshotResultStyle& style) {
        if (scenario == ScreenshotClipboardScenario::OrdinarySelection) {
            const auto normalized = ScreenshotResultCompositor::normalizedStyle(style);
            return normalized.cornerRadius == 0 && normalized.shadowWidth == 0
                       ? ScreenshotClipboardFormatMode::CompatibleDib
                       : ScreenshotClipboardFormatMode::DibV5;
        }
        return formatForScenario(scenario);
    }
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTCLIPBOARDPOLICY_H
