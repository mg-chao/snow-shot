#include "windowshortcutinput_p.h"
#include "snow_shot/presentation/mouseoperationinput.h"

#import <AppKit/AppKit.h>

#include <QApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QWidget>

#include <array>
#include <utility>

namespace snow_shot::platform::macos {
namespace {

QWidget* receiverForNativeWindow(quintptr nativeWindow) {
    QWidget* receiver = QApplication::focusWidget();
    if (receiver == nullptr || nativeWindow == 0)
        return nullptr;
    NSView* view = reinterpret_cast<NSView*>(receiver->window()->winId());
    return reinterpret_cast<quintptr>(view.window) == nativeWindow ? receiver : nullptr;
}

class WindowShortcutInputFilter final : public QObject, public QAbstractNativeEventFilter {
  public:
    WindowShortcutInputFilter(std::function<bool(QWidget*, QKeyEvent&)> dispatch,
                              std::function<QWidget*(quintptr)> receiverForWindow)
        : m_dispatch(std::move(dispatch)),
          m_receiverForWindow(receiverForWindow ? std::move(receiverForWindow)
                                                : receiverForNativeWindow) {
        if (auto* application = QCoreApplication::instance())
            application->installNativeEventFilter(this);
    }

    ~WindowShortcutInputFilter() override {
        if (auto* application = QCoreApplication::instance())
            application->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
        if (eventType != QByteArrayLiteral("mac_generic_NSEvent") || message == nullptr)
            return false;
        NSEvent* native = static_cast<NSEvent*>(message);
        const bool release = native.type == NSEventTypeKeyUp;
        if ((native.type != NSEventTypeKeyDown && !release) || native.keyCode != 48)
            return false;

        // Cocoa handles Control+Tab as a key equivalent. Qt sends only a
        // ShortcutOverride for it, which cannot dispatch our configured bindings,
        // and AppKit then consumes the keyDown. Route this native input through
        // the same manager as ordinary key presses before AppKit handles it.
        if (!release && !(native.modifierFlags & NSEventModifierFlagControl))
            return false;
        QWidget* receiver = release
                                ? m_pressReceiver.data()
                                : m_receiverForWindow(reinterpret_cast<quintptr>(native.window));
        if (receiver == nullptr)
            return false;

        Qt::KeyboardModifiers modifiers;
        if (native.modifierFlags & NSEventModifierFlagControl)
            modifiers |= Qt::MetaModifier;
        if (native.modifierFlags & NSEventModifierFlagCommand)
            modifiers |= Qt::ControlModifier;
        if (native.modifierFlags & NSEventModifierFlagOption)
            modifiers |= Qt::AltModifier;
        if (native.modifierFlags & NSEventModifierFlagShift)
            modifiers |= Qt::ShiftModifier;
        QKeyEvent event(release ? QEvent::KeyRelease : QEvent::KeyPress,
                        modifiers.testFlag(Qt::ShiftModifier) ? Qt::Key_Backtab : Qt::Key_Tab,
                        modifiers, 0, native.keyCode, static_cast<quint32>(native.modifierFlags),
                        QString::fromNSString(native.characters), native.ARepeat);
        event.setTimestamp(static_cast<ulong>(native.timestamp * 1000));
        event.ignore();

        const QPointer<WindowShortcutInputFilter> guard(this);
        const QPointer<QWidget> receiverGuard(receiver);
        const auto dispatch = m_dispatch;
        if (release)
            m_pressReceiver.clear();
        const bool handled = dispatch(receiver, event);
        if (!guard)
            return handled;
        if (!release && handled)
            m_pressReceiver = receiverGuard;
        // Retain ownership through repeats and release even for non-repeating
        // bindings, or if the shortcut was disabled while its key was held.
        return release || handled || (native.ARepeat && m_pressReceiver != nullptr);
    }

  private:
    std::function<bool(QWidget*, QKeyEvent&)> m_dispatch;
    std::function<QWidget*(quintptr)> m_receiverForWindow;
    QPointer<QWidget> m_pressReceiver;
};

class NativeMousePresses final : public QObject, public QAbstractNativeEventFilter {
  public:
    explicit NativeMousePresses(QCoreApplication* application) : QObject(application) {
        application->installNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
        if (eventType != QByteArrayLiteral("mac_generic_NSEvent") || message == nullptr)
            return false;
        NSEvent* event = static_cast<NSEvent*>(message);
        if ((event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown) &&
            (event.modifierFlags & NSEventModifierFlagControl)) {
            m_presses[m_next] = {static_cast<quint64>(event.timestamp * 1000), event.windowNumber,
                                 event.type == NSEventTypeLeftMouseDown ? Qt::LeftButton
                                                                        : Qt::RightButton};
            m_next = (m_next + 1) % m_presses.size();
        }
        return false;
    }

    Qt::MouseButton button(quint64 timestamp, qint64 windowNumber) const {
        for (size_t offset = 1; offset <= m_presses.size(); ++offset) {
            const auto& press = m_presses[(m_next + m_presses.size() - offset) % m_presses.size()];
            if (press.timestamp == timestamp && press.windowNumber == windowNumber &&
                press.button != Qt::NoButton)
                return press.button;
        }
        return Qt::RightButton;
    }

  private:
    struct NativePress {
        quint64 timestamp = 0;
        qint64 windowNumber = 0;
        Qt::MouseButton button = Qt::NoButton;
    };
    std::array<NativePress, 32> m_presses{};
    size_t m_next = 0;
};

QPointer<NativeMousePresses> nativeMousePresses;

} // namespace

std::unique_ptr<QAbstractNativeEventFilter>
makeWindowShortcutInputFilter(std::function<bool(QWidget*, QKeyEvent&)> dispatch,
                              std::function<QWidget*(quintptr)> receiverForWindow) {
    return std::make_unique<WindowShortcutInputFilter>(std::move(dispatch),
                                                       std::move(receiverForWindow));
}

Qt::MouseButton physicalMousePressButton(const QMouseEvent& event, qint64 windowNumber) {
    if (nativeMousePresses && event.button() == Qt::RightButton &&
        event.modifiers().testFlag(Qt::MetaModifier))
        return nativeMousePresses->button(event.timestamp(), windowNumber);
    return event.button();
}

} // namespace snow_shot::platform::macos

namespace snow_shot::mouse {
void initializeNativeButtonInput() {
    if (!platform::macos::nativeMousePresses && QCoreApplication::instance())
        platform::macos::nativeMousePresses =
            new platform::macos::NativeMousePresses(QCoreApplication::instance());
}

Qt::MouseButton physicalButton(const QMouseEvent& event, const QWidget& surface) {
    // Match the native press captured before Cocoa queues Qt's translated event.
    if (event.button() == Qt::RightButton && event.modifiers().testFlag(Qt::MetaModifier) &&
        QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        auto* view = reinterpret_cast<NSView*>(surface.window()->internalWinId());
        if (view)
            return platform::macos::physicalMousePressButton(event, view.window.windowNumber);
    }
    return event.button();
}
} // namespace snow_shot::mouse
