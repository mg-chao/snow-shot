// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <qt_windows.h>
#include <functional>

namespace snow_shot::platform::windows {

class DisplayRecoveryWindow final {
  public:
    explicit DisplayRecoveryWindow(std::function<void()> recovered);
    ~DisplayRecoveryWindow();
    DisplayRecoveryWindow(const DisplayRecoveryWindow&) = delete;
    DisplayRecoveryWindow& operator=(const DisplayRecoveryWindow&) = delete;

    [[nodiscard]] HWND handle() const {
        return m_window;
    }

  private:
    friend struct DisplayRecoveryWindowTestAccess;
    using RegisterSession = BOOL(WINAPI*)(HWND, DWORD);
    using UnregisterSession = BOOL(WINAPI*)(HWND);
    static constexpr UINT_PTR kSessionRetryTimerId = 1;

    DisplayRecoveryWindow(std::function<void()> recovered, RegisterSession registerSession,
                          UnregisterSession unregisterSession);
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void registerSessionNotifications();
    bool isRecovery(UINT message, WPARAM event, LPARAM data);

    std::function<void()> m_recovered;
    RegisterSession m_registerSession;
    UnregisterSession m_unregisterSession;
    HWND m_window = nullptr;
    HPOWERNOTIFY m_power = nullptr;
    bool m_sessionRegistered = false;
    bool m_displayWasOff = false;
};

} // namespace snow_shot::platform::windows
