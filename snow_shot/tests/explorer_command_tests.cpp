#include "explorercommand.h"
#include "supportedformats.h"
#include "snow_shot/platform/windows/shellbatchfile.h"
#include <chrono>
#include <fstream>

#include <shellapi.h>
#include <shlwapi.h>
#ifdef _MSC_VER
#pragma warning(push)
// Windows SDK WRL wrappers intentionally own their objects through COM Release().
#pragma warning(disable : 4265)
#endif
#include <wrl.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

namespace {
using namespace snow_shot::platform::windows::explorer;
using Microsoft::WRL::ComPtr;
using snow_shot::platform::windows::ShellContextMenuMode;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Entry {
    std::wstring path;
    SFGAOF attributes = SFGAO_FILESYSTEM;
    HRESULT displayResult = S_OK;
    HRESULT attributeResult = S_OK;
};

class ShellItem final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IShellItem> {
  public:
    explicit ShellItem(Entry entry) : m_entry(std::move(entry)) {}

    HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx*, REFGUID, REFIID, void**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetParent(IShellItem**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetDisplayName(SIGDN name, PWSTR* path) override {
        require(name == SIGDN_FILESYSPATH, "command requests a filesystem path from the shell");
        if (FAILED(m_entry.displayResult))
            return m_entry.displayResult;
        return SHStrDupW(m_entry.path.c_str(), path);
    }

    HRESULT STDMETHODCALLTYPE GetAttributes(SFGAOF mask, SFGAOF* attributes) override {
        *attributes = m_entry.attributes & mask;
        return m_entry.attributeResult;
    }

    HRESULT STDMETHODCALLTYPE Compare(IShellItem*, SICHINTF, int*) override {
        return E_NOTIMPL;
    }

  private:
    Entry m_entry;
};

class ShellItems final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IShellItemArray> {
  public:
    explicit ShellItems(std::vector<Entry> entries) : m_entries(std::move(entries)) {}

    HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx*, REFGUID, REFIID, void**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyStore(GETPROPERTYSTOREFLAGS, REFIID, void**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyDescriptionList(REFPROPERTYKEY, REFIID, void**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetAttributes(SIATTRIBFLAGS, SFGAOF, SFGAOF*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetCount(DWORD* count) override {
        *count = static_cast<DWORD>(m_entries.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetItemAt(DWORD index, IShellItem** item) override {
        ++itemQueries;
        if (index >= m_entries.size())
            return E_INVALIDARG;
        return Microsoft::WRL::Make<ShellItem>(m_entries[index]).CopyTo(item);
    }

    HRESULT STDMETHODCALLTYPE EnumItems(IEnumShellItems**) override {
        return E_NOTIMPL;
    }
    DWORD itemQueries = 0;

  private:
    std::vector<Entry> m_entries;
};

int launchCount = 0;
std::wstring launchedExecutable;
std::wstring launchedCommandLine;
HRESULT launchResult = S_OK;
snow_shot::platform::windows::packaged_context_menu::Preference menuPreference;

auto readMenuPreference() {
    return menuPreference;
}

HRESULT captureLaunch(const std::wstring& executable, const std::wstring& commandLine) {
    ++launchCount;
    launchedExecutable = executable;
    launchedCommandLine = commandLine;
    return launchResult;
}

std::vector<std::wstring> parseCommandLine(const std::wstring& commandLine) {
    int count = 0;
    auto* arguments = CommandLineToArgvW(commandLine.c_str(), &count);
    require(arguments != nullptr, "Windows parses the generated command line");
    std::vector<std::wstring> result;
    for (int index = 0; index < count; ++index)
        result.emplace_back(arguments[index]);
    LocalFree(arguments);
    return result;
}

void pathAndCommandLinePolicy() {
    for (auto suffix : kSupportedExtensions)
        require(isSupportedPath(std::wstring(L"C:\\images\\file") + std::wstring(suffix)),
                "every declared package extension is enabled by the Explorer command");
    for (const std::wstring path :
         {L"C:\\images\\图片 one.PNG", L"\\\\server\\share\\photo.JpEg",
          L"C:/images/notes.Markdown", L"\\\\?\\C:\\long path\\image.avif", L"C:\\images\\scan.TIF",
          L"C:\\images\\scan.TiFf"})
        require(isSupportedPath(path), "Unicode, UNC, long and mixed-case supported paths work");
    for (const std::wstring path :
         {L"", L"relative.png", L"C:relative.png", L"/rooted.png", L"\\\\server\\photo.png",
          L"C:\\images\\archive.zip", L"C:\\images\\directory.png\\no-suffix",
          L"C:\\images\\quoted\".png", L"C:\\images\\line\n.png", L"C:\\images\\control\x7f.png"})
        require(!isSupportedPath(path), "unsupported and malformed shell paths are rejected");
    std::wstring embeddedNul = L"C:\\images\\nul";
    embeddedNul.push_back(L'\0');
    embeddedNul.append(L".png");
    require(!isSupportedPath(embeddedNul), "embedded NUL paths cannot truncate a shell operand");
}

void commandBehavior() {
    ComPtr<IExplorerCommand> command;
    require(createEditCommand(GetModuleHandleW(nullptr), captureLaunch, &command,
                              readMenuPreference) == S_OK,
            "native command can be constructed without application dependencies");
    const std::vector<Entry> selection{
        {L"C:\\images\\one.png"}, {L"\\\\server\\share\\图片 two.JPEG"}, {L"C:\\notes\\three.md"}};
    const auto valid = Microsoft::WRL::Make<ShellItems>(selection);
    EXPCMDSTATE state = ECS_HIDDEN;
    require(command->GetState(valid.Get(), FALSE, &state) == E_PENDING && !valid->itemQueries,
            "a fast Store query does not inspect shell items on the Explorer UI thread");
    require(command->GetState(valid.Get(), TRUE, &state) == S_OK && state == ECS_ENABLED,
            "modern menu enables supported mixed image and document selections");
    auto supplied = selection;
    supplied.insert(supplied.begin(), Entry{L"C:\\files\\ignored.bin"});
    supplied.insert(supplied.begin() + 2,
                    Entry{L"C:\\files\\unavailable.png", SFGAO_FILESYSTEM, E_FAIL});
    const auto invocation = Microsoft::WRL::Make<ShellItems>(std::move(supplied));
    require(
        command->Invoke(invocation.Get(), nullptr) == S_OK && launchCount == 1,
        "one invocation skips unsupported and unavailable files and launches the remaining batch");
    std::wstring expectedExecutable(32768, L'\0');
    expectedExecutable.resize(GetModuleFileNameW(nullptr, expectedExecutable.data(),
                                                 static_cast<DWORD>(expectedExecutable.size())));
    expectedExecutable.resize(expectedExecutable.find_last_of(L"\\/") + 1);
    expectedExecutable.append(L"snow_shot.exe");
    require(launchedExecutable == expectedExecutable,
            "launch targets snow_shot.exe beside the extension instead of the COM surrogate");
    const auto arguments = parseCommandLine(launchedCommandLine);
    require(arguments.size() == 3 && arguments[1] == L"--explorer-selection",
            "Explorer transfers one selection token");
    const auto batch =
        snow_shot::platform::windows::shellBatchDirectory(false, true) / arguments[2];
    std::ifstream input(batch, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    const auto expectedBatch =
        L"0\n" + selection[0].path + L"\n" + selection[1].path + L"\n" + selection[2].path + L"\n";
    require(bytes.size() == expectedBatch.size() * sizeof(wchar_t) &&
                std::memcmp(bytes.data(), expectedBatch.data(), bytes.size()) == 0,
            "the batch preserves ordered Unicode paths and action");
    input.close();
    std::filesystem::remove(batch);

    menuPreference.mode = ShellContextMenuMode::Disabled;
    require(command->GetState(valid.Get(), TRUE, &state) == S_OK && state == ECS_HIDDEN &&
                command->Invoke(valid.Get(), nullptr) == E_ACCESSDENIED && launchCount == 1,
            "disabling hides an existing command and prevents stale menu invocations");
    menuPreference.mode = ShellContextMenuMode::ImageAndDocumentFiles;
    require(command->GetState(valid.Get(), TRUE, &state) == S_OK && state == ECS_ENABLED,
            "reenabling is visible without recreating the command or restarting Explorer");

    menuPreference.mode = ShellContextMenuMode::ImageFiles;
    const auto images = Microsoft::WRL::Make<ShellItems>(
        std::vector<Entry>{{L"C:\\images\\one.PNG"}, {L"C:\\images\\two.JPEG"}});
    require(command->GetState(images.Get(), TRUE, &state) == S_OK && state == ECS_ENABLED,
            "image-only mode accepts multiple images case-insensitively");
    require(command->GetState(valid.Get(), TRUE, &state) == S_OK && state == ECS_HIDDEN,
            "the Store command respects image-only scope for every selected file");
    for (const auto extension : kSupportedExtensions) {
        const auto path = std::wstring(L"C:\\files\\item") + std::wstring(extension);
        const bool image =
            std::find(std::begin(kSupportedImageExtensions), std::end(kSupportedImageExtensions),
                      extension) != std::end(kSupportedImageExtensions);
        require(isSupportedPath(path, ShellContextMenuMode::ImageFiles) == image,
                "image-only mode excludes every supported document suffix");
    }
    menuPreference.mode = ShellContextMenuMode::ImageAndDocumentFiles;

    for (const auto& entries :
         {std::vector<Entry>{}, std::vector<Entry>{{L"C:\\bad.exe"}},
          std::vector<Entry>{{L"C:\\images\\directory.png", SFGAO_FILESYSTEM | SFGAO_FOLDER}},
          std::vector<Entry>{{L"C:\\images\\virtual.png", 0}},
          std::vector<Entry>{{L"C:\\images\\missing.png", SFGAO_FILESYSTEM, E_ACCESSDENIED}},
          std::vector<Entry>{
              {L"C:\\images\\blocked.png", SFGAO_FILESYSTEM, S_OK, E_ACCESSDENIED}}}) {
        const auto invalid = Microsoft::WRL::Make<ShellItems>(entries);
        state = ECS_ENABLED;
        require(command->GetState(invalid.Get(), TRUE, &state) == S_OK && state == ECS_HIDDEN,
                "invalid, folder, virtual, unavailable, and empty selections hide the command");
        require(command->Invoke(invalid.Get(), nullptr) != S_OK && launchCount == 1,
                "invocation revalidates the selection before any process starts");
    }
    state = ECS_ENABLED;
    require(command->GetState(nullptr, FALSE, &state) == S_OK && state == ECS_HIDDEN &&
                command->Invoke(nullptr, nullptr) == E_INVALIDARG,
            "folder backgrounds cannot invoke a file command");
    launchResult = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    require(command->Invoke(valid.Get(), nullptr) == launchResult,
            "process launch failure propagates to Explorer");

    PWSTR title = nullptr;
    require(command->GetTitle(nullptr, &title) == S_OK && title && title[0] != L'\0',
            "command title loads from the package's localized native resources");
    const std::wstring unassignedTitle(title);
    CoTaskMemFree(title);
    for (const wchar_t accessKey : {L'E', L'7', L'\0'}) {
        menuPreference.actions[0] = accessKey ? static_cast<char>(accessKey) : '.';
        const auto expected = unassignedTitle + (accessKey ? std::wstring(L" (&") + accessKey + L")"
                                                           : std::wstring());
        require(command->GetTitle(nullptr, &title) == S_OK && title && title == expected,
                "existing commands reflect assigned, changed and cleared classic menu mnemonics");
        CoTaskMemFree(title);
    }
    PWSTR icon = nullptr;
    require(command->GetIcon(nullptr, &icon) == S_OK && icon &&
                std::wstring_view(icon).ends_with(L",-102"),
            "modern menu obtains the native extension's embedded application icon");
    CoTaskMemFree(icon);
    GUID canonical{};
    EXPCMDFLAGS flags = ECF_HASSUBCOMMANDS;
    require(command->GetCanonicalName(&canonical) == S_OK && canonical == kEditCommandClsid &&
                command->GetFlags(&flags) == S_OK && flags == ECF_DEFAULT,
            "one stable, non-flyout command is exposed to Explorer");
    require(command->GetTitle(nullptr, nullptr) == E_POINTER &&
                command->GetIcon(nullptr, nullptr) == E_POINTER &&
                command->GetState(nullptr, FALSE, nullptr) == E_POINTER &&
                command->GetFlags(nullptr) == E_POINTER &&
                command->GetCanonicalName(nullptr) == E_POINTER,
            "COM output pointer contracts are respected");
}

void comLifecycle() {
    require(DllCanUnloadNow() == S_OK, "an idle extension can unload");
    ComPtr<IClassFactory> factory;
    require(DllGetClassObject(kEditCommandClsid, IID_PPV_ARGS(&factory)) == S_OK &&
                DllCanUnloadNow() == S_FALSE,
            "manifest CLSID activates a tracked COM class factory");
    require(factory->LockServer(TRUE) == S_OK, "COM can lock the extension server");
    ComPtr<IExplorerCommand> command;
    require(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)) == S_OK,
            "factory exposes IExplorerCommand for packaged Explorer activation");
    ComPtr<IUnknown> identity;
    require(command.As(&identity) == S_OK, "command has a canonical IUnknown identity");
    IExplorerCommand* aggregated = nullptr;
    require(factory->CreateInstance(identity.Get(), IID_PPV_ARGS(&aggregated)) ==
                    CLASS_E_NOAGGREGATION &&
                aggregated == nullptr,
            "COM aggregation is rejected without creating an object");
    identity.Reset();
    command.Reset();
    require(factory->LockServer(FALSE) == S_OK, "server lock is released");
    factory.Reset();
    require(DllCanUnloadNow() == S_OK, "factory and command releases allow DLL unload");
    IClassFactory* unknown = nullptr;
    require(DllGetClassObject(GUID_NULL, IID_PPV_ARGS(&unknown)) == CLASS_E_CLASSNOTAVAILABLE &&
                unknown == nullptr,
            "unknown class registration cannot instantiate the command");
}

template <typename Function> Function exportedFunction(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    require(address != nullptr, "native DLL exports the undecorated COM activation entry points");
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    memcpy(&function, &address, sizeof(function));
    return function;
}

std::wstring titleResource(HMODULE module, WORD language) {
    const HRSRC resource = FindResourceExW(module, RT_STRING, MAKEINTRESOURCEW(7), language);
    require(resource != nullptr, "the native title has a resource for each package language");
    const auto* text = static_cast<const wchar_t*>(LockResource(LoadResource(module, resource)));
    require(text != nullptr, "the native string table can be read");
    for (int index = 0; index < (110 % 16); ++index)
        text += 1 + static_cast<size_t>(*text);
    return std::wstring(text + 1, static_cast<size_t>(*text));
}

void localizedTitles(HMODULE module, IExplorerCommand* command) {
    ULONG count = 0;
    ULONG size = 0;
    require(GetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) != FALSE,
            "the test saves the thread's original UI language preferences");
    std::vector<wchar_t> originalLanguages(size);
    require(GetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, &count, originalLanguages.data(),
                                          &size) != FALSE,
            "the test reads the thread's original UI language preferences");
    struct PreferredLanguage {
        const wchar_t* names;
        WORD language;
    };
    const PreferredLanguage languages[]{
        {L"en-US\0", MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)},
        {L"zh-CN\0", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)},
        {L"zh-TW\0", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)},
        {L"ru-RU\0", MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)},
        {L"de-DE\0en-US\0", MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)}};
    require(titleResource(module, MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)) ==
                L"Закрепить на экране",
            "the DLL embeds the Russian edit command title");
    for (const auto& [names, language] : languages) {
        require(SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, names, &count) != FALSE,
                "the test selects a Windows shell UI language preference");
        PWSTR title = nullptr;
        require(command->GetTitle(nullptr, &title) == S_OK && title &&
                    title == titleResource(module, language),
                "the actual command follows Windows preferred UI language and fallback ordering");
        CoTaskMemFree(title);
        ComPtr<IExplorerCommand> assigned;
        require(createEditCommand(module, captureLaunch, &assigned, readMenuPreference) == S_OK,
                "create a command with the saved access key and the DLL's native resources");
        menuPreference.actions[0] = 'E';
        require(assigned->GetTitle(nullptr, &title) == S_OK && title &&
                    title == titleResource(module, language) + L" (&E)",
                "every localized menu title keeps the assigned access key");
        CoTaskMemFree(title);
        menuPreference.actions[0] = '.';
    }
    require(SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, originalLanguages.data(), &count) !=
                FALSE,
            "the test restores the original thread UI language preferences");
}

void packagedDll(const wchar_t* path) {
    const HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    require(module != nullptr, "the packaged native DLL loads without the Qt application runtime");
    const auto getClassObject =
        exportedFunction<decltype(&DllGetClassObject)>(module, "DllGetClassObject");
    const auto canUnloadNow =
        exportedFunction<decltype(&DllCanUnloadNow)>(module, "DllCanUnloadNow");
    const auto runServer = exportedFunction<decltype(&SnowShotRunExplorerCommandServer)>(
        module, "SnowShotRunExplorerCommandServer");
    require(runServer(6, FALSE) == E_INVALIDARG,
            "the packaged DLL supplies the local-server entry point used by the application");
    require(canUnloadNow() == S_OK, "newly loaded DLL is initially idle");
    {
        ComPtr<IClassFactory> factory;
        require(getClassObject(kEditCommandClsid, IID_PPV_ARGS(&factory)) == S_OK,
                "actual DLL resolves the class registered by the package manifest");
        ComPtr<IExplorerCommand> command;
        require(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)) == S_OK,
                "actual DLL creates the Windows Explorer command");
        std::wstring expectedIcon(32768, L'\0');
        expectedIcon.resize(GetModuleFileNameW(module, expectedIcon.data(),
                                               static_cast<DWORD>(expectedIcon.size())));
        expectedIcon.append(L",-102");
        PWSTR icon = nullptr;
        require(command->GetIcon(nullptr, &icon) == S_OK && icon && icon == expectedIcon,
                "DllMain captures the DLL's own module rather than the shell host executable");
        CoTaskMemFree(icon);
        PWSTR title = nullptr;
        require(command->GetTitle(nullptr, &title) == S_OK && title && title[0] != L'\0',
                "actual DLL includes localized command title resources");
        CoTaskMemFree(title);
        localizedTitles(module, command.Get());
        require(canUnloadNow() == S_FALSE, "Explorer's live command keeps the actual DLL loaded");
    }
    require(canUnloadNow() == S_OK, "actual DLL unloads after Explorer releases its COM objects");
    FreeLibrary(module);
}

void legacyStoreProjection() {
    namespace fs = std::filesystem;
    GUID id{};
    wchar_t guid[39]{};
    require(SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(id, guid, 39), "unique Store fixture");
    const auto root = fs::temp_directory_path() / guid;
    const auto local = root / L"ExamplePackage" / L"LocalState";
    require(fs::create_directory(root), "create a private Store fixture");
    fs::create_directories(local);
    const auto preference = local / L"context-menu-enabled";
    std::ofstream(local / L"config.json")
        << R"({"system":{"annotation_mode_when_opening_for_editing":false}})";
    std::ofstream(preference) << "2K";
    const auto legacy =
        snow_shot::platform::windows::packaged_context_menu::readPreference(preference.wstring());
    require(legacy.mode == ShellContextMenuMode::ImageFiles && legacy.actions[0] == '-' &&
                legacy.actions[1] == 'K',
            "legacy scope and access key come from the projection alone");
    std::ofstream(preference) << "2K-----";
    const auto migrated =
        snow_shot::platform::windows::packaged_context_menu::readPreference(preference.wstring());
    require(migrated.mode == legacy.mode && migrated.actions[0] == 'K' &&
                migrated.actions[1] == '-',
            "the app's migrated projection replaces the legacy fallback");
    fs::remove_all(root);
}

void expiredSelectionBatches() {
    namespace fs = std::filesystem;
    GUID id{};
    wchar_t guid[39]{};
    require(SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(id, guid, 39), "unique batch fixture");
    const auto directory = fs::temp_directory_path() / guid;
    require(fs::create_directory(directory), "create private batch fixture");
    const auto stale = directory / L"{FB6BC742-5B83-4E49-B26F-4B6CE72CA1A2}";
    const auto fresh = directory / L"{FB6BC742-5B83-4E49-B26F-4B6CE72CA1A3}";
    const auto busy = directory / L"{FB6BC742-5B83-4E49-B26F-4B6CE72CA1A4}";
    const auto foreign = directory / L"{unrelated}";
    const auto nullToken = directory / L"{00000000-0000-0000-0000-000000000000}";
    const auto folder = directory / L"{FB6BC742-5B83-4E49-B26F-4B6CE72CA1A5}";
    const auto expired = fs::file_time_type::clock::now() - std::chrono::hours(24 * 8);
    for (const auto& file : {stale, fresh, busy, foreign, nullToken}) {
        std::ofstream(file) << "fixture";
        if (file != fresh)
            fs::last_write_time(file, expired);
    }
    fs::create_directory(folder);
    fs::last_write_time(folder, expired);
    HANDLE reader = CreateFileW(busy.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    require(reader != INVALID_HANDLE_VALUE, "hold an active batch open");
    snow_shot::platform::windows::removeExpiredShellBatches(directory);
    require(!fs::exists(stale) && fs::exists(fresh) && fs::exists(busy) && fs::exists(foreign) &&
                fs::exists(nullToken) && fs::is_directory(folder),
            "cleanup only retires expired, unused files with batch tokens");
    CloseHandle(reader);
    snow_shot::platform::windows::removeExpiredShellBatches(directory);
    require(!fs::exists(busy), "a formerly busy orphan is collected after its reader closes");
    fs::remove_all(directory);
}

void delegatedSelection() {
    using namespace snow_shot::platform::windows;
    ComPtr<IExplorerCommand> command;
    menuPreference.mode = ShellContextMenuMode::ImageAndDocumentFiles;
    menuPreference.actions[actionIndex(ShellAction::Edit)] = '.';
    launchResult = S_OK;
    require(createEditCommand(GetModuleHandleW(nullptr), captureLaunch, &command,
                              readMenuPreference, ShellAction::Edit) == S_OK,
            "create delegate");
    ComPtr<IObjectWithSelection> object;
    ComPtr<IExecuteCommand> execute;
    require(SUCCEEDED(command.As(&object)) && SUCCEEDED(command.As(&execute)),
            "the local server exposes the standard selection and execution interfaces");
    const auto selection = Microsoft::WRL::Make<ShellItems>(
        std::vector<Entry>{{L"C:\\one.png"}, {L"C:\\ignored.md"}, {L"C:\\第二张.jpg"}});
    ComPtr<IShellItemArray> roundTrip;
    require(object->SetSelection(selection.Get()) == S_OK &&
                object->GetSelection(IID_PPV_ARGS(&roundTrip)) == S_OK &&
                roundTrip.Get() == selection.Get() && execute->Execute() == S_OK,
            "DelegateExecute retains and dispatches the complete shell selection");
    const auto arguments = parseCommandLine(launchedCommandLine);
    const auto batch = shellBatchDirectory(false, true) / arguments.at(2);
    std::ifstream input(batch, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    const std::wstring expected = L"4\nC:\\one.png\nC:\\第二张.jpg\n";
    require(bytes.size() == expected.size() * sizeof(wchar_t) &&
                std::memcmp(bytes.data(), expected.data(), bytes.size()) == 0,
            "the delegated action writes one batch with the ordered, supported files");
    input.close();
    std::filesystem::remove(batch);
}

void delegatedProcess() {
    GUID clsid{};
    wchar_t id[39]{};
    require(SUCCEEDED(CoCreateGuid(&clsid)) && StringFromGUID2(clsid, id, 39),
            "a private COM class keeps the process test independent of installed editions");
    std::wstring executable(32768, L'\0');
    executable.resize(
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    auto arguments = L"\"" + executable + L"\" --serve-private-command " + id;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(executable.c_str(), arguments.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process),
            "start a private out-of-process command server");
    CloseHandle(process.hThread);
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "initialize the COM client");
    {
        ComPtr<IExecuteCommand> execute;
        const auto deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline && !execute) {
            const auto activation =
                CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&execute));
            if (SUCCEEDED(activation))
                break;
            require(activation == REGDB_E_CLASSNOTREG &&
                        WaitForSingleObject(process.hProcess, 20) == WAIT_TIMEOUT,
                    "the private class can activate through the standard COM proxy");
        }
        require(execute && SUCCEEDED(execute->SetShowWindow(SW_SHOWNORMAL)),
                "IExecuteCommand calls reach the helper process");
        ComPtr<IObjectWithSelection> delegate;
        ComPtr<IShellItem> item;
        ComPtr<IShellItemArray> selection;
        ComPtr<IShellItemArray> returned;
        DWORD count = 0;
        require(SUCCEEDED(execute.As(&delegate)) &&
                    SUCCEEDED(SHCreateItemFromParsingName(executable.c_str(), nullptr,
                                                          IID_PPV_ARGS(&item))) &&
                    SUCCEEDED(SHCreateShellItemArrayFromShellItem(item.Get(),
                                                                  IID_PPV_ARGS(&selection))) &&
                    SUCCEEDED(delegate->SetSelection(selection.Get())) &&
                    SUCCEEDED(delegate->GetSelection(IID_PPV_ARGS(&returned))) &&
                    SUCCEEDED(returned->GetCount(&count)) && count == 1,
                "the standard proxy marshals shell items into and out of the helper process");
    }
    CoUninitialize();
    DWORD exitCode = 0;
    require(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0 &&
                GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == 0,
            "releasing the command exits the single-use server");
    CloseHandle(process.hProcess);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--serve-private-command") {
        GUID clsid{};
        using snow_shot::platform::windows::ShellAction;
        return SUCCEEDED(IIDFromString(argv[2], &clsid)) &&
                       SUCCEEDED(serveExplorerCommand(clsid, ShellAction::Edit, false))
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    require(argc == 2, "pass the built Explorer command DLL for activation smoke tests");
    legacyStoreProjection();
    expiredSelectionBatches();
    pathAndCommandLinePolicy();
    commandBehavior();
    delegatedSelection();
    delegatedProcess();
    using namespace snow_shot::platform::windows;
    const auto docs =
        Microsoft::WRL::Make<ShellItems>(std::vector<Entry>{{L"C:\\selection\\notes.md"}});
    const auto mixed = Microsoft::WRL::Make<ShellItems>(std::vector<Entry>{
        {L"C:\\selection\\notes.md"}, {L"C:\\selection\\one.png"}, {L"C:\\selection\\file.bin"}});
    for (size_t i = 0; i < shellActionIds.size(); ++i) {
        const auto action = static_cast<ShellAction>(i);
        ComPtr<IExplorerCommand> cmd;
        require(createEditCommand(GetModuleHandleW(nullptr), captureLaunch, &cmd,
                                  readMenuPreference, action) == S_OK,
                "construct each Explorer action");
        menuPreference.actions.fill('-');
        EXPCMDSTATE state = ECS_ENABLED;
        require(cmd->GetState(mixed.Get(), TRUE, &state) == S_OK && state == ECS_HIDDEN &&
                    cmd->Invoke(mixed.Get(), nullptr) == E_ACCESSDENIED,
                "unchecked actions never activate");
        menuPreference.actions[i] = 'K';
        menuPreference.mode = ShellContextMenuMode::ImageAndDocumentFiles;
        require(cmd->GetState(docs.Get(), TRUE, &state) == S_OK &&
                    state == static_cast<EXPCMDSTATE>(acceptsDocuments(action) ? ECS_ENABLED
                                                                               : ECS_HIDDEN),
                "document-only selection exposes only pin actions");
        menuPreference.mode = ShellContextMenuMode::ImageFiles;
        require(cmd->GetState(docs.Get(), TRUE, &state) == S_OK && state == ECS_HIDDEN &&
                    cmd->GetState(mixed.Get(), TRUE, &state) == S_OK && state == ECS_HIDDEN,
                "Store commands retain their configured file scope");
    }
    menuPreference = {};
    comLifecycle();
    packagedDll(argv[1]);
    return EXIT_SUCCESS;
}
