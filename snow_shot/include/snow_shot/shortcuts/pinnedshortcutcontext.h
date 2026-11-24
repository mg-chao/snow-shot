#pragma once

#include <QString>

namespace snow_shot::shortcuts {
// Animated pins and static-content editing never handle these commands at the same time.
inline bool isPinnedPlaybackShortcut(QStringView id) {
    return id == QStringLiteral("toggle_playback") || id == QStringLiteral("next_frame") ||
           id == QStringLiteral("previous_frame") || id.endsWith(QStringLiteral("playback_speed"));
}

inline bool pinnedShortcutActionsMayConflict(QStringView first, QStringView second) {
    const auto staticContent = [](QStringView id) {
        return id == QStringLiteral("drawing_mode") || id == QStringLiteral("resize_window");
    };
    return !((isPinnedPlaybackShortcut(first) && staticContent(second)) ||
             (isPinnedPlaybackShortcut(second) && staticContent(first)));
}
} // namespace snow_shot::shortcuts
