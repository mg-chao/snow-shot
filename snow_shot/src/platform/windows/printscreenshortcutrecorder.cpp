#include "snow_shot/platform/windows/printscreenshortcutrecorder.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QWidget>

#include <utility>
#include <optional>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace snow_shot::platform::windows {

class PrintScreenShortcutRecorder::Impl final : public QObject, public QAbstractNativeEventFilter {
  public:
    Impl(QWidget& targetWidget, Handler recordHandler)
        : target(&targetWidget), window(targetWidget.window()), handler(std::move(recordHandler)) {
        target->installEventFilter(this);
        if (window != target) {
            window->installEventFilter(this);
        }
        QCoreApplication::instance()->installNativeEventFilter(this);
        startHook();
    }

    ~Impl() override {
        stopHook();
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }

    bool isActive() const {
        if (target == nullptr || window == nullptr || !target->isVisible()) {
            return false;
        }
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            return GetForegroundWindow() == reinterpret_cast<HWND>(window->winId());
        }
#endif
        return window->isActiveWindow();
    }

    void record(bool pressed, bool autoRepeat, Qt::KeyboardModifiers modifiers,
                std::optional<quint32> nativeTimestamp = std::nullopt) {
        if (!isActive() || autoRepeat) {
            return;
        }
        if (pressed) {
            if (printPressed) {
                return;
            }
            printPressed = true;
        } else if (std::exchange(printPressed, false)) {
            return;
        }
        nativeCaptureTimestamp = nativeTimestamp;

        // Never validate or rebuild widgets inside a Windows keyboard hook.
        const quint64 captureGeneration = ++generation;
        QMetaObject::invokeMethod(
            this,
            [this, captureGeneration, modifiers]() {
                if (captureGeneration == generation && isActive()) {
                    handler(modifiers);
                }
            },
            Qt::QueuedConnection);
    }

    bool eventFilter(QObject*, QEvent* event) override {
        switch (event->type()) {
        case QEvent::WindowDeactivate:
            if (QGuiApplication::platformName() != QStringLiteral("windows") || !isActive()) {
                ++generation;
                printPressed = false;
                nativeCaptureTimestamp.reset();
            }
            break;
        case QEvent::Hide:
            ++generation;
            printPressed = false;
            nativeCaptureTimestamp.reset();
            stopHook();
            break;
        case QEvent::WindowActivate:
        case QEvent::Show:
            startHook();
            break;
        default:
            break;
        }
        return false;
    }

    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override {
#ifdef Q_OS_WIN
        if (message == nullptr || !isActive()) {
            return false;
        }
        const auto& native = *static_cast<const MSG*>(message);
        if (native.hwnd != reinterpret_cast<HWND>(window->winId()) ||
            native.wParam != VK_SNAPSHOT) {
            return false;
        }
        const bool pressed = native.message == WM_KEYDOWN || native.message == WM_SYSKEYDOWN;
        if (!pressed && native.message != WM_KEYUP && native.message != WM_SYSKEYUP) {
            return false;
        }

        // Qt's Windows key mapper drops non-modifier releases without a preceding press:
        // qtbase/src/plugins/platforms/windows/qwindowskeymapper.cpp (KEYUP handling).
        auto modifiers = nativeModifiers(false);
        if ((native.lParam & (LPARAM{1} << 29)) != 0) {
            modifiers |= Qt::AltModifier;
        }
        record(pressed, pressed && (native.lParam & (LPARAM{1} << 30)) != 0, modifiers,
               native.time);
        if (result != nullptr) {
            *result = 0;
        }
        return true;
#else
        Q_UNUSED(message);
        Q_UNUSED(result);
        return false;
#endif
    }

    void startHook() {
#ifdef Q_OS_WIN
        // Keep the hook for the recording session: Windows can deliver input before
        // Qt processes WindowActivate. Only the native foreground window may capture.
        if (hook != nullptr || active != nullptr || target == nullptr || !target->isVisible() ||
            QGuiApplication::platformName() != QStringLiteral("windows")) {
            return;
        }
        hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(nullptr), 0);
        if (hook == nullptr) {
            qWarning() << "Print Screen recorder keyboard hook failed:" << GetLastError();
            return;
        }
        printPressed = (GetAsyncKeyState(VK_SNAPSHOT) & 0x8000) != 0;
        active = this;
#endif
    }

    void stopHook() {
#ifdef Q_OS_WIN
        if (hook != nullptr) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
            active = nullptr;
        }
#endif
    }

#ifdef Q_OS_WIN
    static Qt::KeyboardModifiers nativeModifiers(bool asynchronous) {
        const auto down = [asynchronous](int key) {
            return ((asynchronous ? GetAsyncKeyState(key) : GetKeyState(key)) & 0x8000) != 0;
        };
        Qt::KeyboardModifiers modifiers;
        if (down(VK_CONTROL)) {
            modifiers |= Qt::ControlModifier;
        }
        if (down(VK_SHIFT)) {
            modifiers |= Qt::ShiftModifier;
        }
        if (down(VK_MENU)) {
            modifiers |= Qt::AltModifier;
        }
        if (down(VK_LWIN) || down(VK_RWIN)) {
            modifiers |= Qt::MetaModifier;
        }
        return modifiers;
    }

    static LRESULT CALLBACK keyboardHook(int code, WPARAM message, LPARAM data) {
        Impl* const recorder = active;
        if (code == HC_ACTION && recorder != nullptr && recorder->isActive()) {
            const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
            const bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            if (key.vkCode == VK_SNAPSHOT &&
                (pressed || message == WM_KEYUP || message == WM_SYSKEYUP)) {
                // Print Screen does not change modifier state; sample the modifiers now,
                // before a queued UI callback can observe their subsequent release.
                auto modifiers = nativeModifiers(true);
                if ((key.flags & LLKHF_ALTDOWN) != 0) {
                    modifiers |= Qt::AltModifier;
                }
                recorder->record(pressed, false, modifiers, key.time);
                return 1;
            }
        } else if (code == HC_ACTION && recorder != nullptr) {
            ++recorder->generation;
            recorder->printPressed = false;
            recorder->nativeCaptureTimestamp.reset();
        }
        return CallNextHookEx(nullptr, code, message, data);
    }

    static thread_local Impl* active;
    HHOOK hook = nullptr;
#endif
    QPointer<QWidget> target;
    QPointer<QWidget> window;
    Handler handler;
    std::optional<quint32> nativeCaptureTimestamp;
    quint64 generation = 0;
    bool printPressed = false;
};

#ifdef Q_OS_WIN
thread_local PrintScreenShortcutRecorder::Impl* PrintScreenShortcutRecorder::Impl::active = nullptr;
#endif

PrintScreenShortcutRecorder::PrintScreenShortcutRecorder(QWidget& target, Handler handler)
    : m_impl(std::make_unique<Impl>(target, std::move(handler))) {}

PrintScreenShortcutRecorder::~PrintScreenShortcutRecorder() = default;

bool PrintScreenShortcutRecorder::handleKeyEvent(const QKeyEvent& event) {
    if (event.key() != Qt::Key_Print && event.key() != Qt::Key_SysReq) {
        if (event.type() == QEvent::KeyPress && event.timestamp() != 0 &&
            m_impl->nativeCaptureTimestamp.has_value()) {
            const quint32 elapsed =
                static_cast<quint32>(event.timestamp()) - *m_impl->nativeCaptureTimestamp;
            const bool modifier = event.key() == Qt::Key_Control || event.key() == Qt::Key_Shift ||
                                  event.key() == Qt::Key_Alt || event.key() == Qt::Key_Meta ||
                                  event.key() == Qt::Key_AltGr || event.key() == Qt::Key_Super_L ||
                                  event.key() == Qt::Key_Super_R;
            // Hook notifications precede Qt's queued key events. Do not let an older
            // modifier press clear the captured chord, including within the same millisecond.
            return elapsed > 0x7FFFFFFFU || (elapsed == 0 && modifier);
        }
        return false;
    }
    m_impl->record(event.type() == QEvent::KeyPress, event.isAutoRepeat(), event.modifiers());
    return true;
}

void PrintScreenShortcutRecorder::cancelPendingCapture() {
    ++m_impl->generation;
}

} // namespace snow_shot::platform::windows
