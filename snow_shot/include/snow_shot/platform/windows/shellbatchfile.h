// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/platform/windows/packagedcontextmenupreference.h"
#include <objbase.h>
#include <filesystem>
#include <memory>

namespace snow_shot::platform::windows {
// Both Explorer and the receiving application resolve the token inside this user's directory.
inline std::filesystem::path shellBatchDirectory(bool mini, bool packaged) {
    if (packaged) {
        const auto preference = packaged_context_menu::preferencePath();
        if (!preference.empty())
            return std::filesystem::path(preference).parent_path() / L"ExplorerSelections";
    }
    std::wstring local(32768, L'\0');
    const auto count =
        GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (!count || count >= local.size())
        return {};
    local.resize(count);
    return std::filesystem::path(local) / (mini ? L"SnowShotMini" : L"SnowShot") /
           L"ExplorerSelections";
}

// A secondary process can exit before the primary receives its token. Leave time for update
// relaunches and storage migrations, then remove only closed, regular GUID batches by handle.
inline void removeExpiredShellBatches(const std::filesystem::path& directory) {
    if (directory.empty())
        return;
    // Pin the inbox while resolving its children. A renamed directory or a replaced junction
    // must never redirect maintenance into another tree.
    const HANDLE root = CreateFileW(
        directory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (root == INVALID_HANDLE_VALUE)
        return;
    const std::unique_ptr<void, decltype(&CloseHandle)> rootHandle(root, CloseHandle);
    BY_HANDLE_FILE_INFORMATION directoryInfo{};
    if (!GetFileInformationByHandle(rootHandle.get(), &directoryInfo) ||
        !(directoryInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (directoryInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER cutoff{};
    cutoff.HighPart = now.dwHighDateTime;
    cutoff.LowPart = now.dwLowDateTime;
    cutoff.QuadPart -= 7ULL * 24 * 60 * 60 * 10'000'000; // FILETIME counts 100 ns intervals.
    const FILETIME expiredBefore{cutoff.LowPart, cutoff.HighPart};
    WIN32_FIND_DATAW entry{};
    const auto pattern = directory / L"{*}";
    const HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        GUID token{};
        if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            CompareFileTime(&entry.ftLastWriteTime, &expiredBefore) >= 0 ||
            FAILED(IIDFromString(entry.cFileName, &token)) || token == GUID_NULL)
            continue;
        const auto path = directory / entry.cFileName;
        const HANDLE file = CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, 0, nullptr,
                                        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileInformationByHandle(file, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            CompareFileTime(&info.ftLastWriteTime, &expiredBefore) < 0) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
                                       sizeof(disposition));
        }
        CloseHandle(file);
    } while (FindNextFileW(search, &entry));
    FindClose(search);
}
} // namespace snow_shot::platform::windows
