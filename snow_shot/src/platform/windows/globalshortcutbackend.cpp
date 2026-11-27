#include "globalshortcutbackend_p.h"
#include "shortcutvirtualkey.h"
#include <QHash>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QKeySequence>
#include <QSet>

#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace snow_shot::presentation {
namespace {

using platform::windows::NativeShortcut;
using platform::windows::parseNativeShortcut;

GlobalShortcutValidationResult validation(const shortcuts::ShortcutBinding& binding,
                                          bool supported) {
    GlobalShortcutValidationResult result;
    result.binding = binding;
    result.shortcut = binding.portableText;
    result.supported = supported;
    result.failureReason = supported ? GlobalShortcutFailureReason::None
                                     : GlobalShortcutFailureReason::InvalidShortcut;
    return result;
}

class WindowsGlobalShortcutBackend final : public GlobalShortcutBackend,
                                           public QAbstractNativeEventFilter {
  public:
    explicit WindowsGlobalShortcutBackend(WindowsHotKeyInputApi api) : m_inputApi(std::move(api)) {
        if (!m_inputApi.available) {
            m_inputApi.available = [] {
                HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
                if (desktop == nullptr)
                    return false;
                CloseDesktop(desktop);
                return true;
            };
        }
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::instance()->installNativeEventFilter(this);
            m_filterInstalled = true;
        }
    }
    ~WindowsGlobalShortcutBackend() override {
        for (int registrationId : std::as_const(m_registeredIds)) {
            m_inputApi.unregisterHotKey(nullptr, registrationId);
        }
        if (m_filterInstalled && QCoreApplication::instance() != nullptr) {
            QCoreApplication::instance()->removeNativeEventFilter(this);
        }
    }
    void setActivationHandler(ActivationHandler handler) override {
        m_handler = std::move(handler);
    }
    GlobalShortcutValidationResult
    validateShortcut(const shortcuts::ShortcutBinding& binding) const override {
        NativeShortcut native;
        return validation(binding, parseNativeShortcut(binding, &native));
    }
    GlobalShortcutBackendResult
    registerShortcut(int registrationId, const shortcuts::ShortcutBinding& binding) override {
        NativeShortcut native;
        if (!parseNativeShortcut(binding, &native)) {
            return {false, GlobalShortcutFailureReason::InvalidShortcut, 0};
        }
        SetLastError(ERROR_SUCCESS);
        if (m_inputApi.registerHotKey(nullptr, registrationId, native.modifiers,
                                      native.virtualKey) != FALSE) {
            m_registeredIds.insert(registrationId);
            m_nativeShortcuts.insert(registrationId, native);
            return {true, GlobalShortcutFailureReason::None, 0};
        }
        const DWORD error = GetLastError();
        return {false,
                error == ERROR_HOTKEY_ALREADY_REGISTERED ? GlobalShortcutFailureReason::AlreadyInUse
                                                         : GlobalShortcutFailureReason::SystemError,
                static_cast<qint64>(error)};
    }
    void unregisterShortcut(int registrationId) override {
        m_nativeShortcuts.remove(registrationId);
        if (m_registeredIds.remove(registrationId)) {
            m_inputApi.unregisterHotKey(nullptr, registrationId);
        }
    }
    std::optional<GlobalShortcutInputState> inputState(int id) const override {
        if (!m_inputApi.available() || (id != 0 && !m_nativeShortcuts.contains(id)))
            return std::nullopt;
        const auto down = [this](int key) { return (m_inputApi.keyState(key) & 0x8000) != 0; };
        const auto native = m_nativeShortcuts.value(id);
        GlobalShortcutInputState state;
        state.escapeDown = down(VK_ESCAPE);
        state.escapeIsShortcutKey = native.virtualKey == VK_ESCAPE;
        state.anyShortcutKeyDown =
            id != 0 && (down(static_cast<int>(native.virtualKey)) ||
                        ((native.modifiers & MOD_CONTROL) != 0 && down(VK_CONTROL)) ||
                        ((native.modifiers & MOD_SHIFT) != 0 && down(VK_SHIFT)) ||
                        ((native.modifiers & MOD_ALT) != 0 && down(VK_MENU)) ||
                        ((native.modifiers & MOD_WIN) != 0 && (down(VK_LWIN) || down(VK_RWIN))));
        return state;
    }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        if (message != nullptr) {
            const auto* native = static_cast<const MSG*>(message);
            if (native->message == WM_HOTKEY && m_handler) {
                m_handler(static_cast<int>(native->wParam));
            }
        }
        return false;
    }

  private:
    WindowsHotKeyInputApi m_inputApi;
    QHash<int, NativeShortcut> m_nativeShortcuts;
    ActivationHandler m_handler;
    QSet<int> m_registeredIds;
    bool m_filterInstalled = false;
};

} // namespace

std::unique_ptr<GlobalShortcutBackend> createWindowsGlobalShortcutBackend() {
    return createWindowsGlobalShortcutBackend(WindowsHotKeyInputApi{});
}

std::unique_ptr<GlobalShortcutBackend>
createWindowsGlobalShortcutBackend(WindowsHotKeyInputApi api) {
    return std::make_unique<WindowsGlobalShortcutBackend>(std::move(api));
}

} // namespace snow_shot::presentation
