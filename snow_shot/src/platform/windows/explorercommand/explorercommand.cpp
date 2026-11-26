#include "explorercommand.h"
#include "supportedformats.h"
#include "snow_shot/platform/windows/shellbatchfile.h"
#include <filesystem>

#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace snow_shot::platform::windows::explorer {
namespace {

HMODULE commandModule = nullptr;
std::atomic<ULONG> objectCount{0};
std::atomic<ULONG> serverLockCount{0};
DWORD localServerThread = 0;
bool localServerActivated = false;

constexpr size_t kMaximumCommandLineLength = 32767;

void lockLocalServer(bool lock) {
    if (!localServerThread)
        return;
    if (lock) {
        CoAddRefServerProcess();
    } else if (!CoReleaseServerProcess()) {
        PostThreadMessageW(localServerThread, WM_QUIT, 0, 0);
    }
}

packaged_context_menu::Preference contextMenuPreference() {
    return packaged_context_menu::readPreference(packaged_context_menu::preferencePath());
}

wchar_t asciiLower(wchar_t character) noexcept {
    return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character + (L'a' - L'A'))
                                                  : character;
}

bool isSeparator(wchar_t character) noexcept {
    return character == L'\\' || character == L'/';
}

bool isAbsolutePath(std::wstring_view path) noexcept {
    if (path.size() >= 3 && asciiLower(path[0]) >= L'a' && asciiLower(path[0]) <= L'z' &&
        path[1] == L':' && isSeparator(path[2]))
        return true;
    if (path.size() < 5 || !isSeparator(path[0]) || !isSeparator(path[1]))
        return false;
    const size_t serverEnd = path.find_first_of(L"\\/", 2);
    if (serverEnd == std::wstring_view::npos || serverEnd <= 2 || serverEnd + 1 >= path.size() ||
        isSeparator(path[serverEnd + 1]))
        return false;
    const size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
    return shareEnd != std::wstring_view::npos && shareEnd + 1 < path.size();
}

HRESULT modulePath(HMODULE module, std::wstring* path) {
    std::wstring buffer(512, L'\0');
    for (;;) {
        const DWORD size =
            GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            return HRESULT_FROM_WIN32(GetLastError());
        if (size < buffer.size()) {
            buffer.resize(size);
            *path = std::move(buffer);
            return S_OK;
        }
        if (buffer.size() >= kMaximumCommandLineLength)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        buffer.resize((std::min)(buffer.size() * 2, kMaximumCommandLineLength));
    }
}

HRESULT executablePath(HMODULE module, std::wstring* path, bool mini) {
    HRESULT result = modulePath(module, path);
    if (FAILED(result))
        return result;
    const size_t separator = path->find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    path->resize(separator + 1);
    path->append(mini ? L"snow_shot_mini.exe" : L"snow_shot.exe");
    return S_OK;
}

// Quote every operand using the Windows CRT's backslash/quote rules. No command shell is involved.
void appendQuoted(std::wstring_view argument, std::wstring* commandLine) {
    commandLine->push_back(L'"');
    size_t backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        commandLine->append(backslashes * (character == L'"' ? 2 : 1), L'\\');
        if (character == L'"')
            commandLine->push_back(L'\\');
        backslashes = 0;
        commandLine->push_back(character);
    }
    commandLine->append(backslashes * 2, L'\\');
    commandLine->push_back(L'"');
}

bool supportedSelectionItem(IShellItemArray* items, DWORD index, ShellContextMenuMode mode,
                            std::vector<std::wstring>* paths) {
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(items->GetItemAt(index, &item)) || !item)
        return false;
    SFGAOF attributes = 0;
    if (FAILED(item->GetAttributes(SFGAO_FILESYSTEM | SFGAO_FOLDER, &attributes)) ||
        (attributes & SFGAO_FILESYSTEM) == 0 || (attributes & SFGAO_FOLDER) != 0)
        return false;
    PWSTR nativePath = nullptr;
    const HRESULT result = item->GetDisplayName(SIGDN_FILESYSPATH, &nativePath);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(nativePath, CoTaskMemFree);
    if (FAILED(result) || !path || !isSupportedPath(path.get(), mode))
        return false;
    if (paths)
        paths->emplace_back(path.get());
    return true;
}

HRESULT collectSelectionPaths(IShellItemArray* items, ShellContextMenuMode mode,
                              std::vector<std::wstring>& paths) {
    if (!items)
        return E_INVALIDARG;
    DWORD count = 0;
    const HRESULT result = items->GetCount(&count);
    if (FAILED(result) || count == 0)
        return FAILED(result) ? result : E_INVALIDARG;
    paths.reserve(count);
    for (DWORD index = 0; index < count; ++index)
        supportedSelectionItem(items, index, mode, &paths);
    return paths.empty() ? S_FALSE : S_OK;
}

HRESULT launchProcess(const std::wstring& executable, const std::wstring& commandLine) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring mutableCommandLine = commandLine;
    if (!CreateProcessW(executable.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process))
        return HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return S_OK;
}

CLSID actionClsid(ShellAction action, char edition) {
    auto clsid = kEditCommandClsid;
    clsid.Data4[7] = static_cast<unsigned char>(((edition - 'A' + 10) << 4) + actionIndex(action));
    return clsid;
}

std::wstring classValue(ShellAction action, char edition, const wchar_t* name) {
    wchar_t guid[39]{};
    StringFromGUID2(actionClsid(action, edition), guid, 39);
    const auto path = std::wstring(L"Software\\Classes\\CLSID\\") + guid;
    wchar_t value[512]{};
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name, RRF_RT_REG_SZ, nullptr, value,
                        &bytes) == ERROR_SUCCESS
               ? value
               : L"";
}

HRESULT launchSelection(const std::wstring& executable, const std::vector<std::wstring>& paths,
                        ShellAction action, char edition, ProcessLauncher launcher) {
    const auto directory = shellBatchDirectory(edition == 'C', edition == 'A');
    if (directory.empty())
        return E_FAIL;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return HRESULT_FROM_WIN32(error.value());
    GUID guid{};
    wchar_t token[39]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, token, 39))
        return E_FAIL;
    const auto filePath = directory / token;
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    auto write = [&](const std::wstring& line) {
        DWORD written = 0;
        const auto bytes = static_cast<DWORD>(line.size() * sizeof(wchar_t));
        return WriteFile(file, line.data(), bytes, &written, nullptr) && written == bytes;
    };
    bool ok = write(std::to_wstring(actionIndex(action)) + L"\n");
    for (const auto& path : paths) {
        if (!ok)
            break;
        ok = write(path) && write(L"\n");
    }
    CloseHandle(file);
    HRESULT result = E_FAIL;
    if (ok) {
        std::wstring command;
        appendQuoted(executable, &command);
        command.append(L" --explorer-selection ").append(token);
        result = launcher(executable, command);
    }
    if (FAILED(result))
        DeleteFileW(filePath.c_str());
    return result;
}

class EditCommand final : public IExplorerCommand,
                          public IExecuteCommand,
                          public IObjectWithSelection {
  public:
    EditCommand(HMODULE module, ProcessLauncher launcher, PreferenceReader preferenceReader,
                ShellAction action, char edition) noexcept
        : m_action(action), m_edition(edition), m_module(module), m_launcher(launcher),
          m_preferenceReader(preferenceReader) {
        ++objectCount;
        if (localServerThread) {
            localServerActivated = true;
            lockLocalServer(true);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IExplorerCommand)
            *object = static_cast<IExplorerCommand*>(this);
        else if (iid == IID_IExecuteCommand)
            *object = static_cast<IExecuteCommand*>(this);
        else if (iid == IID_IObjectWithSelection)
            *object = static_cast<IObjectWithSelection*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++m_references;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG references = --m_references;
        if (references == 0)
            delete this;
        return references;
    }

    HRESULT STDMETHODCALLTYPE GetTitle(IShellItemArray*, PWSTR* title) override {
        if (!title)
            return E_POINTER;
        *title = nullptr;
        if (m_edition != 'A') {
            try {
                return SHStrDupW(classValue(m_action, m_edition, nullptr).c_str(), title);
            } catch (...) {
                return E_OUTOFMEMORY;
            }
        }
        const wchar_t* text = nullptr;
        const int length = LoadStringW(m_module, static_cast<UINT>(110 + actionIndex(m_action)),
                                       reinterpret_cast<PWSTR>(&text), 0);
        if (length <= 0)
            return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
        try {
            std::wstring label;
            for (int index = 0; index < length; ++index) {
                label.push_back(text[index]);
                if (text[index] == L'&')
                    label.push_back(L'&');
            }
            const auto accessKey = m_preferenceReader().actions[actionIndex(m_action)];
            if (packaged_context_menu::isAccessKey(accessKey)) {
                label.append(L" (&");
                label.push_back(accessKey);
                label.push_back(L')');
            }
            return SHStrDupW(label.c_str(), title);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE GetIcon(IShellItemArray*, PWSTR* icon) override {
        if (!icon)
            return E_POINTER;
        *icon = nullptr;
        try {
            std::wstring path;
            HRESULT result = modulePath(m_module, &path);
            if (FAILED(result))
                return result;
            path.append(L",-102");
            return SHStrDupW(path.c_str(), icon);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE GetToolTip(IShellItemArray*, PWSTR* tooltip) override {
        if (!tooltip)
            return E_POINTER;
        *tooltip = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalName(GUID* name) override {
        if (!name)
            return E_POINTER;
        *name = actionClsid(m_action, m_edition);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetState(IShellItemArray* items, BOOL okToBeSlow,
                                       EXPCMDSTATE* state) override {
        if (!state)
            return E_POINTER;
        *state = ECS_HIDDEN;
        if (!items)
            return S_OK;
        // Checking a Store preference and inspecting a shell item can both require I/O.
        // Explorer retries E_PENDING on a background thread with okToBeSlow set.
        if (m_edition == 'A' && !okToBeSlow)
            return E_PENDING;
        try {
            const auto mode = effectiveMode();
            if (mode == ShellContextMenuMode::Disabled)
                return S_OK;
            DWORD count = 0;
            if (FAILED(items->GetCount(&count)) || count == 0)
                return S_OK;
            // Explorer already matched standalone verbs against the configured extensions.
            // Store declarations are fixed, so apply the user's current scope here as well.
            if (m_edition == 'A') {
                for (DWORD index = 0; index < count; ++index)
                    if (!supportedSelectionItem(items, index, mode, nullptr))
                        return S_OK;
            }
            *state = ECS_ENABLED;
            return S_OK;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE Invoke(IShellItemArray* items, IBindCtx*) override {
        try {
            const auto mode = effectiveMode();
            if (mode == ShellContextMenuMode::Disabled)
                return E_ACCESSDENIED;
            std::vector<std::wstring> paths;
            HRESULT result = collectSelectionPaths(items, mode, paths);
            if (result != S_OK)
                return result;
            std::wstring executable;
            result = executablePath(m_module, &executable, m_edition == 'C');
            if (FAILED(result))
                return result;
            return launchSelection(executable, paths, m_action, m_edition, m_launcher);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }

    HRESULT STDMETHODCALLTYPE GetFlags(EXPCMDFLAGS* flags) override {
        if (!flags)
            return E_POINTER;
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumSubCommands(IEnumExplorerCommand** commands) override {
        if (!commands)
            return E_POINTER;
        *commands = nullptr;
        return E_NOTIMPL;
    }

    // DelegateExecute marshals the original selection into the emulated local server.
    HRESULT STDMETHODCALLTYPE SetSelection(IShellItemArray* selection) override {
        m_selection = selection;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(REFIID iid, void** selection) override {
        if (!selection)
            return E_POINTER;
        *selection = nullptr;
        return m_selection ? m_selection.CopyTo(iid, selection) : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Execute() override {
        return Invoke(m_selection.Get(), nullptr);
    }
    HRESULT STDMETHODCALLTYPE SetKeyState(DWORD) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetParameters(PCWSTR) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPosition(POINT) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetShowWindow(int) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetNoShowUI(BOOL) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDirectory(PCWSTR) override {
        return S_OK;
    }

  private:
    ~EditCommand() {
        --objectCount;
        lockLocalServer(false);
    }

    std::atomic<ULONG> m_references{1};
    ShellContextMenuMode effectiveMode() const {
        ShellContextMenuMode mode;
        if (m_edition == 'A') {
            const auto prefs = m_preferenceReader();
            if (prefs.actions[actionIndex(m_action)] == '-')
                return ShellContextMenuMode::Disabled;
            mode = prefs.mode;
        } else {
            const auto scope = classValue(m_action, m_edition, L"Scope");
            if (scope != L"image_files" && scope != L"images_only")
                return ShellContextMenuMode::Disabled;
            mode = scope == L"images_only" ? ShellContextMenuMode::ImageFiles
                                           : ShellContextMenuMode::ImageAndDocumentFiles;
        }
        return mode == ShellContextMenuMode::ImageAndDocumentFiles && !acceptsDocuments(m_action)
                   ? ShellContextMenuMode::ImageFiles
                   : mode;
    }

    ShellAction m_action;
    char m_edition;
    HMODULE m_module;
    ProcessLauncher m_launcher;
    PreferenceReader m_preferenceReader;
    Microsoft::WRL::ComPtr<IShellItemArray> m_selection;
};

class CommandFactory final : public IClassFactory {
  public:
    CommandFactory(ShellAction action, char edition) noexcept
        : m_action(action), m_edition(edition) {
        ++objectCount;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory)
            return E_NOINTERFACE;
        *object = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++m_references;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG references = --m_references;
        if (references == 0)
            delete this;
        return references;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        Microsoft::WRL::ComPtr<IExplorerCommand> command;
        HRESULT result =
            createEditCommand(commandModule, launchProcess, &command, nullptr, m_action, m_edition);
        return FAILED(result) ? result : command->QueryInterface(iid, object);
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        lockLocalServer(lock != FALSE);
        if (lock)
            ++serverLockCount;
        else
            --serverLockCount;
        return S_OK;
    }

  private:
    ShellAction m_action;
    char m_edition;
    ~CommandFactory() {
        --objectCount;
    }

    std::atomic<ULONG> m_references{1};
};

} // namespace

bool isSupportedPath(std::wstring_view path, ShellContextMenuMode mode) noexcept {
    if (mode == ShellContextMenuMode::Disabled || !isAbsolutePath(path))
        return false;
    for (wchar_t character : path)
        if (character == L'"' || character < L' ' || (character >= 127 && character <= 159))
            return false;
    const size_t separator = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos ||
        (separator != std::wstring_view::npos && dot <= separator))
        return false;
    const auto suffix = path.substr(dot);
    const auto extensions = mode == ShellContextMenuMode::ImageFiles
                                ? std::span<const std::wstring_view>(kSupportedImageExtensions)
                                : std::span<const std::wstring_view>(kSupportedExtensions);
    for (const auto extension : extensions) {
        if (suffix.size() != extension.size())
            continue;
        bool matches = true;
        for (size_t index = 0; index < suffix.size(); ++index)
            matches = matches && asciiLower(suffix[index]) == extension[index];
        if (matches)
            return true;
    }
    return false;
}

HRESULT createEditCommand(HMODULE module, ProcessLauncher launcher, IExplorerCommand** command,
                          PreferenceReader preferenceReader, ShellAction action,
                          char edition) noexcept {
    if (!command)
        return E_POINTER;
    *command = nullptr;
    if (!launcher)
        return E_INVALIDARG;
    auto* instance = new (std::nothrow)
        EditCommand(module, launcher, preferenceReader ? preferenceReader : contextMenuPreference,
                    action, edition);
    if (!instance)
        return E_OUTOFMEMORY;
    *command = instance;
    return S_OK;
}

} // namespace snow_shot::platform::windows::explorer

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** object) {
    using namespace snow_shot::platform::windows::explorer;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    using snow_shot::platform::windows::ShellAction;
    const auto action = static_cast<ShellAction>(clsid.Data4[7] & 15);
    const auto edition = static_cast<char>('A' + (clsid.Data4[7] >> 4) - 10);
    if ((clsid.Data4[7] & 15) >= 6 || edition < 'A' || edition > 'C' ||
        clsid != actionClsid(action, edition))
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) CommandFactory(action, edition);
    if (!factory)
        return E_OUTOFMEMORY;
    const HRESULT result = factory->QueryInterface(iid, object);
    factory->Release();
    return result;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    using namespace snow_shot::platform::windows::explorer;
    return objectCount == 0 && serverLockCount == 0 ? S_OK : S_FALSE;
}

HRESULT snow_shot::platform::windows::explorer::serveExplorerCommand(REFCLSID registrationId,
                                                                     ShellAction action,
                                                                     bool mini) {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result))
        return result;
    Microsoft::WRL::ComPtr<IClassFactory> factory;
    factory.Attach(new (std::nothrow) CommandFactory(action, mini ? 'C' : 'B'));
    result = factory ? S_OK : E_OUTOFMEMORY;
    DWORD registration = 0;
    if (SUCCEEDED(result)) {
        localServerThread = GetCurrentThreadId();
        localServerActivated = false;
        result = CoRegisterClassObject(registrationId, factory.Get(), CLSCTX_LOCAL_SERVER,
                                       REGCLS_SINGLEUSE, &registration);
    }
    if (SUCCEEDED(result)) {
        // Bound an activation whose client disappears before requesting its command.
        const auto timer = SetTimer(nullptr, 0, 60000, nullptr);
        if (!timer) {
            result = HRESULT_FROM_WIN32(GetLastError());
        } else {
            MSG message{};
            for (;;) {
                const auto received = GetMessageW(&message, nullptr, 0, 0);
                if (received <= 0) {
                    if (received < 0)
                        result = HRESULT_FROM_WIN32(GetLastError());
                    break;
                }
                if (message.message == WM_TIMER && message.wParam == timer &&
                    !localServerActivated) {
                    result = CO_E_SERVER_EXEC_FAILURE;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            KillTimer(nullptr, timer);
        }
        CoRevokeClassObject(registration);
    }
    localServerThread = 0;
    factory.Reset();
    CoUninitialize();
    return result;
}

extern "C" HRESULT __stdcall SnowShotRunExplorerCommandServer(ULONG index, BOOL mini) {
    using namespace snow_shot::platform::windows;
    using namespace snow_shot::platform::windows::explorer;
    if (index >= shellActionIds.size())
        return E_INVALIDARG;
    const auto action = static_cast<ShellAction>(index);
    return serveExplorerCommand(actionClsid(action, mini ? 'C' : 'B'), action, mini != FALSE);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        snow_shot::platform::windows::explorer::commandModule = module;
    return TRUE;
}
