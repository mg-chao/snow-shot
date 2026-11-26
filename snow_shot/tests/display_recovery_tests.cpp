// SPDX-License-Identifier: GPL-3.0-or-later
#include "displayrecoverywindow.h"

#include <dbt.h>
#include <wtsapi32.h>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <new>

namespace snow_shot::platform::windows {
struct DisplayRecoveryWindowTestAccess {
    static DisplayRecoveryWindow make(DisplayRecoveryWindow::RegisterSession registerSession,
                                      DisplayRecoveryWindow::UnregisterSession unregisterSession) {
        return DisplayRecoveryWindow([] {}, registerSession, unregisterSession);
    }

    static void fireSessionRetryTimer(const DisplayRecoveryWindow& observer) {
        SendMessageW(observer.handle(), WM_TIMER, DisplayRecoveryWindow::kSessionRetryTimerId, 0);
    }
};
} // namespace snow_shot::platform::windows

namespace {
using snow_shot::platform::windows::DisplayRecoveryWindowTestAccess;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

void displayState(HWND window, DWORD value, DWORD size = sizeof(DWORD)) {
    // POWERBROADCAST_SETTING has a variable-sized Data tail.
    alignas(POWERBROADCAST_SETTING) unsigned char
        bytes[sizeof(POWERBROADCAST_SETTING) + sizeof(DWORD)]{};
    auto* setting = new (bytes) POWERBROADCAST_SETTING{};
    setting->PowerSetting = GUID_SESSION_DISPLAY_STATUS;
    setting->DataLength = size;
    std::memcpy(bytes + offsetof(POWERBROADCAST_SETTING, Data), &value, sizeof(value));
    SendMessageW(window, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                 reinterpret_cast<LPARAM>(setting));
}

struct SessionRegistration {
    DWORD error = RPC_S_INVALID_BINDING;
    int attempts = 0;
    int unregistrations = 0;
    HWND subscribedWindow = nullptr;
};

SessionRegistration session;

BOOL WINAPI registerSession(HWND window, DWORD flags) {
    require(flags == NOTIFY_FOR_THIS_SESSION, "register only for the current Windows session");
    ++session.attempts;
    if (session.error != ERROR_SUCCESS) {
        SetLastError(session.error);
        return FALSE;
    }
    session.subscribedWindow = window;
    return TRUE;
}

BOOL WINAPI unregisterSession(HWND window) {
    require(window == session.subscribedWindow && IsWindow(window),
            "unregister the successful subscription before destroying its HWND");
    ++session.unregistrations;
    return TRUE;
}

void sessionRegistrationRetriesWhileServiceStarts() {
    session = {};
    {
        const auto observer =
            DisplayRecoveryWindowTestAccess::make(registerSession, unregisterSession);
        require(session.attempts == 1, "try to register immediately at startup");
        DisplayRecoveryWindowTestAccess::fireSessionRetryTimer(observer);
        require(session.attempts == 2, "retry while session services are still starting");
        session.error = ERROR_SUCCESS;
        DisplayRecoveryWindowTestAccess::fireSessionRetryTimer(observer);
        require(session.attempts == 3 && session.subscribedWindow == observer.handle(),
                "register the existing window when session services become ready");
        DisplayRecoveryWindowTestAccess::fireSessionRetryTimer(observer);
        require(session.attempts == 3, "successful registration stops the retry timer");
    }
    require(session.unregistrations == 1, "unregister a retried subscription exactly once");
}

void sessionRegistrationStopsAfterNonStartupFailure() {
    session = {};
    {
        const auto observer =
            DisplayRecoveryWindowTestAccess::make(registerSession, unregisterSession);
        session.error = ERROR_ACCESS_DENIED;
        DisplayRecoveryWindowTestAccess::fireSessionRetryTimer(observer);
        DisplayRecoveryWindowTestAccess::fireSessionRetryTimer(observer);
        require(session.attempts == 2, "a non-startup failure stops the retry timer");
    }
    require(session.unregistrations == 0, "unregister only after successful registration");
}
} // namespace

int main() {
    int recoveries = 0;
    HWND window = nullptr;
    {
        snow_shot::platform::windows::DisplayRecoveryWindow observer([&] { ++recoveries; });
        window = observer.handle();
        require(window && !IsWindowVisible(window), "display listener must have a hidden HWND");
        displayState(window, PowerMonitorOn);
        displayState(window, PowerMonitorDim);
        displayState(window, PowerMonitorOn);
        SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0);
        SendMessageW(window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
        SendMessageW(window, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0);
        SendMessageW(window, WM_DEVICECHANGE, DBT_CONFIGCHANGECANCELED, 0);
        require(recoveries == 0, "initial, dimmed, locked and suspended states must stay idle");
        displayState(window, PowerMonitorOff);
        displayState(window, PowerMonitorOn, 1);
        require(recoveries == 0, "an incomplete power payload must not resume the display");
        displayState(window, PowerMonitorOn);
        displayState(window, PowerMonitorOn);
        require(recoveries == 1, "one off-to-on transition should request recovery once");
        SendMessageW(window, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
        SendMessageW(window, WM_POWERBROADCAST, PBT_APMRESUMESUSPEND, 0);
        SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0);
        SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_REMOTE_CONNECT, 0);
        SendMessageW(window, WM_WTSSESSION_CHANGE, WTS_CONSOLE_CONNECT, 0);
        SendMessageW(window, WM_DEVICECHANGE, DBT_CONFIGCHANGED, 0);
        SendMessageW(window, WM_CLOSE, 0, 0);
        SendMessageW(window, WM_DISPLAYCHANGE, 0, 0);
        require(recoveries == 7,
                "resume, unlock, reconnect, docking and mode change must reach capture");
    }
    require(!IsWindow(window), "observer teardown must release the subscription window");
    sessionRegistrationRetriesWhileServiceStarts();
    sessionRegistrationStopsAfterNonStartupFailure();
}
