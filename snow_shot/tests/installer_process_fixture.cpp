#include <windows.h>
#include <cwchar>

namespace {
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_QUERYENDSESSION) {
        return TRUE;
    }
    if (message == WM_ENDSESSION && wParam) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const WNDCLASSW windowClass{.lpfnWndProc = windowProcedure,
                                .hInstance = instance,
                                .lpszClassName = L"SnowShotInstallerTest"};
    if (!RegisterClassW(&windowClass) ||
        !CreateWindowW(windowClass.lpszClassName, L"Snow Shot installer test", WS_OVERLAPPED, 0, 0,
                       100, 100, nullptr, nullptr, instance, nullptr)) {
        return 1;
    }
    wchar_t readyName[80]{};
    swprintf_s(readyName, L"Local\\SnowShotInstallerTest-%lu", GetCurrentProcessId());
    const HANDLE ready = CreateEventW(nullptr, TRUE, TRUE, readyName);
    if (!ready) {
        return 1;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(ready);
    return 0;
}
