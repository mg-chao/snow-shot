// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/shortcuts/pinnedshortcutcontext.h"
#include "snow_shot/shortcuts/screenshotshortcutcontext.h"

namespace snow_shot::shortcuts {
inline bool localShortcutKeysMayConflict(QStringView first, QStringView second) {
    const auto firstSplit = first.indexOf(u'/');
    const auto secondSplit = second.indexOf(u'/');
    const auto firstScope = first.first(firstSplit);
    const auto secondScope = second.first(secondSplit);
    const auto firstId = first.sliced(firstSplit + 1);
    const auto secondId = second.sliced(secondSplit + 1);
    if (firstScope == secondScope) {
        if (firstScope == u"screenshot_shortcuts")
            return screenshotShortcutActionsMayConflict(firstId, secondId);
        if (firstScope == u"pin_to_screen_shortcuts")
            return pinnedShortcutActionsMayConflict(firstId, secondId);
        return true;
    }
    const auto sharedOnPin = [](QStringView key, QStringView otherScope, QStringView otherId) {
        return (key == u"screenshot_shortcuts/text_recognition" ||
                key == u"screenshot_shortcuts/text_recognition_copy") &&
               otherScope == u"pin_to_screen_shortcuts" && !isPinnedPlaybackShortcut(otherId);
    };
    return sharedOnPin(first, secondScope, secondId) || sharedOnPin(second, firstScope, firstId);
}
} // namespace snow_shot::shortcuts
