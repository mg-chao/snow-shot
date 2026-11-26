#ifndef SNOW_SHOT_PLATFORM_WINDOWS_PACKAGEDCONTEXTMENUPREFERENCE_H
#define SNOW_SHOT_PLATFORM_WINDOWS_PACKAGEDCONTEXTMENUPREFERENCE_H

#include "snow_shot/platform/windows/shellcontextmenumode.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <appmodel.h>

#include <string>

namespace snow_shot::platform::windows::packaged_context_menu {

using Preference = ShellMenuPreferences;

inline bool isAccessKey(wchar_t key) {
    return (key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9');
}

// A small, Qt-independent preference shared with the packaged COM surrogate.
// LocalState survives package updates and is independent of custom app storage.
inline std::wstring preferencePath() {
    UINT32 familyLength = 0;
    if (GetCurrentPackageFamilyName(&familyLength, nullptr) != ERROR_INSUFFICIENT_BUFFER)
        return {};
    std::wstring family(familyLength, L'\0');
    if (GetCurrentPackageFamilyName(&familyLength, family.data()) != ERROR_SUCCESS)
        return {};
    family.resize(familyLength - 1);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (length == 0)
        return {};
    std::wstring local(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), length);
    if (written == 0 || written >= length)
        return {};
    local.resize(written);
    return local + L"\\Packages\\" + family + L"\\LocalState\\context-menu-enabled";
}

inline Preference readPreference(const std::wstring& path) {
    if (path.empty())
        return {ShellContextMenuMode::Disabled};
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        // Preserve the existing enabled-by-default behavior before the first app launch.
        return {error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                    ? ShellContextMenuMode::ImageAndDocumentFiles
                    : ShellContextMenuMode::Disabled};
    }
    char bytes[8]{};
    DWORD read = 0;
    const bool ok = ReadFile(file, bytes, sizeof(bytes), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok || (read != 7 && read != 1 && read != 2) ||
        (bytes[0] != '0' && bytes[0] != '1' && bytes[0] != '2'))
        return {ShellContextMenuMode::Disabled};
    Preference result;
    result.mode = bytes[0] == '2'   ? ShellContextMenuMode::ImageFiles
                  : bytes[0] == '1' ? ShellContextMenuMode::ImageAndDocumentFiles
                                    : ShellContextMenuMode::Disabled;
    if (read < 7) {
        // The earlier preference stored only the scope and optional key. The app replaces
        // this projection from its migrated configuration during its first startup.
        if (read == 2 && !isAccessKey(bytes[1]))
            return {ShellContextMenuMode::Disabled};
        result.actions.fill('-');
        result.actions[actionIndex(ShellAction::Annotate)] = read == 2 ? bytes[1] : '.';
    } else {
        for (size_t i = 0; i < result.actions.size(); ++i) {
            const char key = bytes[i + 1];
            if (key != '.' && key != '-' && !isAccessKey(key))
                return {ShellContextMenuMode::Disabled};
            result.actions[i] = key;
        }
    }
    return result;
}

inline bool isEnabled(const std::wstring& path) {
    return readPreference(path).mode != ShellContextMenuMode::Disabled;
}

} // namespace snow_shot::platform::windows::packaged_context_menu

#endif
