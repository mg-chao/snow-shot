#pragma once

#include <QString>

namespace snow_shot::shortcuts {
// These commands own mutually exclusive selection editing contexts, even when remapped.
inline bool screenshotShortcutActionsMayConflict(QStringView first, QStringView second) {
    const auto history = [](QStringView id) {
        return id == QStringLiteral("select_previously_selected_area") ||
               id == QStringLiteral("select_next_previously_selected_area");
    };
    return !((first == QStringLiteral("duplicate_latest_region") && history(second)) ||
             (second == QStringLiteral("duplicate_latest_region") && history(first)));
}
} // namespace snow_shot::shortcuts
