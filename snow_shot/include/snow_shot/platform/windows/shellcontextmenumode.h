#ifndef SNOW_SHOT_PLATFORM_WINDOWS_SHELLCONTEXTMENUMODE_H
#define SNOW_SHOT_PLATFORM_WINDOWS_SHELLCONTEXTMENUMODE_H

#include <array>
#include <string_view>

namespace snow_shot::platform::windows {

// Keep this Qt-independent: the packaged Explorer command also consumes this mode.
enum class ShellContextMenuMode { Disabled, ImageAndDocumentFiles, ImageFiles };

// These ordinals are shared by the COM class IDs, preference file and selection batches.
enum class ShellAction { Pin, Annotate, Stitch, Split, Edit, Compress };
inline constexpr std::array<const char*, 6> shellActionIds{
    "pin_to_screen",  "pin_to_screen_and_annotate", "stitch_images", "split_images", "edit_images",
    "compress_images"};
inline constexpr size_t actionIndex(ShellAction action) {
    return static_cast<size_t>(action);
}
inline constexpr bool acceptsDocuments(ShellAction action) {
    return action == ShellAction::Pin || action == ShellAction::Annotate;
}
struct ShellMenuPreferences {
    ShellContextMenuMode mode = ShellContextMenuMode::ImageAndDocumentFiles;
    // '-' hides an action, '.' shows it without a mnemonic, otherwise the byte is its key.
    std::array<char, 6> actions{'.', '-', '-', '-', '-', '-'};
    friend bool operator==(const ShellMenuPreferences&, const ShellMenuPreferences&) = default;
};
} // namespace snow_shot::platform::windows

#endif
