#include "snow_shot/presentation/screenshotclipboardpolicy.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void onlyEligibleScenariosUseCompatibleDib() {
    using Scenario = ScreenshotClipboardScenario;
    using Format = ScreenshotClipboardFormatMode;
    struct StyleCase {
        ScreenshotResultStyle style;
        Format expected;
    };
    const StyleCase cases[] = {
        {{0, 0, QColor()}, Format::CompatibleDib},   {{8, 0, QColor()}, Format::DibV5},
        {{0, 6, QColor()}, Format::DibV5},           {{8, 6, QColor()}, Format::DibV5},
        {{-8, -6, QColor()}, Format::CompatibleDib}, {{-8, 6, QColor()}, Format::DibV5},
        {{8, -6, QColor()}, Format::DibV5},          {{0, 6, QColor(0, 0, 0, 0)}, Format::DibV5},
    };
    for (const auto& test : cases) {
        require(ScreenshotClipboardPolicy::formatForScenario(Scenario::OrdinarySelection,
                                                             test.style) == test.expected,
                "ordinary selection format did not follow normalized effects");
        for (const auto scenario : {Scenario::ScrollingCapture, Scenario::CurrentMonitor}) {
            require(ScreenshotClipboardPolicy::formatForScenario(scenario, test.style) ==
                            Format::CompatibleDib &&
                        ScreenshotClipboardPolicy::formatForScenario(scenario) ==
                            Format::CompatibleDib,
                    "scrolling or monitor copy did not use compatible DIB");
        }
        require(ScreenshotClipboardPolicy::formatForScenario(Scenario::Other, test.style) ==
                    Format::DibV5,
                "another bitmap workflow opted into compatible DIB based on style");
    }
    require(ScreenshotClipboardPolicy::formatForScenario() == Format::DibV5 &&
                ScreenshotClipboardPolicy::formatForScenario(Scenario::Other) == Format::DibV5 &&
                ScreenshotClipboardPolicy::formatForScenario(Scenario::OrdinarySelection) ==
                    Format::DibV5,
            "missing scenario or selection style must default to DIBV5");
}
} // namespace

int main() {
    onlyEligibleScenariosUseCompatibleDib();
    return EXIT_SUCCESS;
}
