#include "snow_shot/runtime/runtimeactivitytracker.h"

namespace snow_shot::runtime {
RuntimeActivityTracker& RuntimeActivityTracker::shared() {
    // Keep a single admission state across independently optimized libraries.
    // Detached cleanup may still finish during static teardown.
    static auto* tracker = new RuntimeActivityTracker;
    return *tracker;
}
} // namespace snow_shot::runtime
