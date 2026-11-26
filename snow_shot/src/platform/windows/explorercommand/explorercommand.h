#ifndef SNOW_SHOT_EXPLORER_COMMAND_H
#define SNOW_SHOT_EXPLORER_COMMAND_H

#include "snow_shot/platform/windows/packagedcontextmenupreference.h"

#include <windows.h>
#include <shobjidl.h>

#include <string>
#include <string_view>
#include <vector>

namespace snow_shot::platform::windows::explorer {

// Package registration must use this CLSID and bin/SnowShotExplorerCommand.dll.
inline constexpr CLSID kEditCommandClsid = {
    0x3f1e90a2, 0x7d4b, 0x4f21, {0x9d, 0x8a, 0x6b, 0x0c, 0x5e, 0x2f, 0x70, 0xa0}};

using ProcessLauncher = HRESULT (*)(const std::wstring& executable,
                                    const std::wstring& commandLine);
using PreferenceReader = packaged_context_menu::Preference (*)();

[[nodiscard]] bool
isSupportedPath(std::wstring_view path,
                ShellContextMenuMode mode = ShellContextMenuMode::ImageAndDocumentFiles) noexcept;
[[nodiscard]] HRESULT createEditCommand(HMODULE module, ProcessLauncher launcher,
                                        IExplorerCommand** command,
                                        PreferenceReader preferenceReader = nullptr,
                                        ShellAction action = ShellAction::Pin,
                                        char edition = 'A') noexcept;
[[nodiscard]] HRESULT serveExplorerCommand(REFCLSID registration, ShellAction action, bool mini);

} // namespace snow_shot::platform::windows::explorer

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** object);
extern "C" HRESULT __stdcall DllCanUnloadNow();
extern "C" HRESULT __stdcall SnowShotRunExplorerCommandServer(ULONG index, BOOL mini);

#endif
