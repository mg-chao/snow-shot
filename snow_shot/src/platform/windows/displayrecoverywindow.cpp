// SPDX-License-Identifier: GPL-3.0-or-later
#include "displayrecoverywindow.h"

#include <commctrl.h>
#include <dbt.h>
#include <wtsapi32.h>
#include <cstring>
#include <utility>

namespace snow_shot::platform::windows {
namespace {
constexpr UINT kSessionRetryIntervalMs = 1000;
}

DisplayRecoveryWindow::DisplayRecoveryWindow(std::function<void()> recovered)
    : DisplayRecoveryWindow(std::move(recovered), WTSRegisterSessionNotification,
                            WTSUnRegisterSessionNotification) {}

DisplayRecoveryWindow::DisplayRecoveryWindow(std::function<void()> recovered,
                                             RegisterSession registerSession,
                                             UnregisterSession unregisterSession)
    : m_recovered(std::move(recovered)), m_registerSession(registerSession),
      m_unregisterSession(unregisterSession) {
    // Display changes and resume are broadcasts to top-level windows, including hidden ones.
    m_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", nullptr, WS_POPUP, 0,
                               0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_window)
        return;
    if (!SetWindowSubclass(m_window, windowProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
        DestroyWindow(std::exchange(m_window, nullptr));
        return;
    }
    m_power = RegisterPowerSettingNotification(m_window, &GUID_SESSION_DISPLAY_STATUS,
                                               DEVICE_NOTIFY_WINDOW_HANDLE);
    registerSessionNotifications();
}

DisplayRecoveryWindow::~DisplayRecoveryWindow() {
    if (!m_window)
        return;
    KillTimer(m_window, kSessionRetryTimerId);
    if (m_power)
        UnregisterPowerSettingNotification(m_power);
    if (m_sessionRegistered)
        m_unregisterSession(m_window);
    RemoveWindowSubclass(m_window, windowProc, 1);
    DestroyWindow(m_window);
}

void DisplayRecoveryWindow::registerSessionNotifications() {
    m_sessionRegistered = m_registerSession(m_window, NOTIFY_FOR_THIS_SESSION) != FALSE;
    // The Remote Desktop Services dependencies can still be starting at sign-in.
    if (!m_sessionRegistered && GetLastError() == RPC_S_INVALID_BINDING) {
        SetTimer(m_window, kSessionRetryTimerId, kSessionRetryIntervalMs, nullptr);
    }
}

bool DisplayRecoveryWindow::isRecovery(UINT message, WPARAM event, LPARAM data) {
    if (message == WM_DISPLAYCHANGE)
        return true;
    // A completed dock or undock can leave the desktop geometry unchanged.
    if (message == WM_DEVICECHANGE)
        return event == DBT_CONFIGCHANGED;
    if (message == WM_WTSSESSION_CHANGE)
        return event == WTS_SESSION_UNLOCK || event == WTS_CONSOLE_CONNECT ||
               event == WTS_REMOTE_CONNECT;
    if (message != WM_POWERBROADCAST)
        return false;
    if (event == PBT_APMRESUMEAUTOMATIC)
        return true;
    if (event != PBT_POWERSETTINGCHANGE || !data)
        return false;
    const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(data);
    if (setting->PowerSetting != GUID_SESSION_DISPLAY_STATUS ||
        setting->DataLength != sizeof(DWORD))
        return false;
    DWORD state;
    std::memcpy(&state, setting->Data, sizeof(state));
    if (state == PowerMonitorOff)
        m_displayWasOff = true;
    // The initial notification and ordinary dim/undim cycles need no recovery.
    return state == PowerMonitorOn && std::exchange(m_displayWasOff, false);
}

LRESULT CALLBACK DisplayRecoveryWindow::windowProc(HWND window, UINT message, WPARAM event,
                                                   LPARAM data, UINT_PTR, DWORD_PTR context) {
    auto& self = *reinterpret_cast<DisplayRecoveryWindow*>(context);
    if (message == WM_CLOSE)
        return 0; // Keep receiving notifications until the owning observer is destroyed.
    if (message == WM_TIMER && event == kSessionRetryTimerId) {
        if (KillTimer(window, kSessionRetryTimerId))
            self.registerSessionNotifications();
        return 0;
    }
    if (self.isRecovery(message, event, data)) {
        self.m_recovered();
        return message == WM_POWERBROADCAST || message == WM_DEVICECHANGE ? TRUE : 0;
    }
    return DefSubclassProc(window, message, event, data);
}

} // namespace snow_shot::platform::windows
