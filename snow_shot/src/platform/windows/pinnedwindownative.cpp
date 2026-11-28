#include "pinnedwindownative.h"

#include <QCursor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QWindow>

#include <algorithm>
#include <limits>
#include <optional>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qwindowsysteminterface.h>
#include <qt_windows.h>
// clang-format off
#include <commctrl.h>
// clang-format on

namespace {
constexpr UINT_PTR kSynchronizedResizeSubclassId = 0x5353525A; // "SSRZ"

HWND toNativeHwnd(WId windowId) {
    return reinterpret_cast<HWND>(windowId); // NOLINT(performance-no-int-to-ptr)
}

std::optional<QPoint> windowPlacementOffset(HWND hwnd, HMONITOR monitor) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR extendedStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (extendedStyle == 0 && GetLastError() != ERROR_SUCCESS)
        return std::nullopt;
    // Tool-window placement is already in screen coordinates. Other top-level
    // windows store normal placement relative to their monitor's work area.
    if ((extendedStyle & WS_EX_TOOLWINDOW) != 0)
        return QPoint();
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE)
        return std::nullopt;
    const qint64 x = static_cast<qint64>(info.rcWork.left) - info.rcMonitor.left;
    const qint64 y = static_cast<qint64>(info.rcWork.top) - info.rcMonitor.top;
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
        return std::nullopt;
    return QPoint(static_cast<int>(x), static_cast<int>(y));
}

std::optional<RECT> nativeRect(const QRect& geometry, const QPoint& offset = {}) {
    const qint64 left = static_cast<qint64>(geometry.x()) - offset.x();
    const qint64 top = static_cast<qint64>(geometry.y()) - offset.y();
    const qint64 right = left + geometry.width();
    const qint64 bottom = top + geometry.height();
    if (!geometry.isValid() || left < std::numeric_limits<LONG>::min() ||
        right > std::numeric_limits<LONG>::max() || top < std::numeric_limits<LONG>::min() ||
        bottom > std::numeric_limits<LONG>::max())
        return std::nullopt;
    return RECT{static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right),
                static_cast<LONG>(bottom)};
}

QRect normalClientGeometry(HWND hwnd) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement) == FALSE)
        return {};
    // MonitorFromWindow uses the pre-minimize rectangle for an iconic HWND.
    const auto offset =
        windowPlacementOffset(hwnd, MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
    if (!offset)
        return {};
    const RECT& rect = placement.rcNormalPosition;
    const qint64 left = static_cast<qint64>(rect.left) + offset->x();
    const qint64 top = static_cast<qint64>(rect.top) + offset->y();
    const qint64 width = static_cast<qint64>(rect.right) - rect.left;
    const qint64 height = static_cast<qint64>(rect.bottom) - rect.top;
    if (left < std::numeric_limits<int>::min() || left > std::numeric_limits<int>::max() ||
        top < std::numeric_limits<int>::min() || top > std::numeric_limits<int>::max() ||
        width <= 0 || width > std::numeric_limits<int>::max() || height <= 0 ||
        height > std::numeric_limits<int>::max() ||
        left + width > std::numeric_limits<int>::max() ||
        top + height > std::numeric_limits<int>::max())
        return {};
    // Snow Shot's pinned HWND is frameless: normal window placement is also
    // the normal client rectangle, including the app-drawn shadow margin.
    return QRect(static_cast<int>(left), static_cast<int>(top), static_cast<int>(width),
                 static_cast<int>(height));
}

bool applyMinimizedClientGeometry(HWND hwnd, const QRect& geometry) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement) == FALSE)
        return false;
    const auto screenRect = nativeRect(geometry);
    if (!screenRect)
        return false;
    const auto offset =
        windowPlacementOffset(hwnd, MonitorFromRect(&*screenRect, MONITOR_DEFAULTTONEAREST));
    if (!offset)
        return false;
    const auto normalRect = nativeRect(geometry, *offset);
    if (!normalRect)
        return false;

    const bool visible = IsWindowVisible(hwnd) != FALSE;
    // GetWindowPlacement does not encode SW_HIDE. Explicitly preserve hidden
    // state, and avoid activation when updating a visible minimized surface.
    placement.showCmd = visible ? SW_SHOWMINNOACTIVE : SW_HIDE;
    const WINDOWPLACEMENT previous = placement;
    placement.rcNormalPosition = *normalRect;
    if (SetWindowPlacement(hwnd, &placement) != FALSE && IsIconic(hwnd) != FALSE &&
        (IsWindowVisible(hwnd) != FALSE) == visible && normalClientGeometry(hwnd) == geometry)
        return true;
    // Keep failure atomic as far as USER32 permits; the caller can then roll
    // its content/geometry transaction back without adopting icon dimensions.
    static_cast<void>(SetWindowPlacement(hwnd, &previous));
    return false;
}

LRESULT CALLBACK synchronizedResizeSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR subclassId,
                                                DWORD_PTR referenceData) {
    Q_UNUSED(subclassId);
    bool synchronizeFrame = false;
    if (message == WM_WINDOWPOSCHANGED) {
        const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam);
        const auto* interactiveResizeActive = reinterpret_cast<const bool*>(referenceData);
        synchronizeFrame = interactiveResizeActive != nullptr && *interactiveResizeActive &&
                           position != nullptr && (position->flags & SWP_NOSIZE) == 0 &&
                           (position->flags & SWP_NOCOPYBITS) != 0;
    }

    const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    if (synchronizeFrame) {
        // Qt queues the QWidget geometry notification behind its native
        // procedure. Drain only non-input window-system events so the resized
        // canvas and camera are ready before publishing the layered surface.
        static_cast<void>(
            QWindowSystemInterface::flushWindowSystemEvents(QEventLoop::ExcludeUserInputEvents));
        static_cast<void>(
            RedrawWindow(hwnd, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE));
    }
    return result;
}
} // namespace
#endif

struct screenshot_pinned_window_native::SystemMoveKeyboard::Impl final : QObject {
    explicit Impl(QWidget* widget) : window(widget) {}

    QPointer<QWidget> window;
    QList<QKeyCombination> combinations;
#if defined(Q_OS_WIN) || defined(_WIN32)
    static thread_local Impl* active;
    HHOOK hook = nullptr;
    bool translating = false;
    bool handled = false;
    QSet<int> pressedKeys;

    bool eventFilter(QObject* watched, QEvent* event) override {
        Q_UNUSED(watched);
        if (!translating || window == nullptr) {
            return false;
        }
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
            return false;
        }
        auto* key = static_cast<QKeyEvent*>(event);
        const bool press = event->type() == QEvent::KeyPress;
        if ((press && combinations.contains(key->keyCombination())) ||
            (!press && pressedKeys.contains(key->key()))) {
            handled = true;
            if (press) {
                pressedKeys.insert(key->key());
            } else if (!key->isAutoRepeat()) {
                pressedKeys.remove(key->key());
            }
            QCoreApplication::sendEvent(window, event);
        }
        // Translation is only a probe for movement shortcuts. Unmatched keys
        // remain owned by USER32, including Escape/Enter to end the drag.
        return true;
    }

    static LRESULT CALLBACK filter(int code, WPARAM wParam, LPARAM lParam) {
        Impl* self = active;
        if (code == HC_ACTION && wParam == PM_REMOVE && self != nullptr && !self->translating &&
            self->window != nullptr) {
            auto* message = reinterpret_cast<MSG*>(lParam);
            GUITHREADINFO info{};
            info.cbSize = sizeof(info);
            if (message != nullptr && GetGUIThreadInfo(GetCurrentThreadId(), &info) != FALSE &&
                (info.flags & GUI_INMOVESIZE) != 0 &&
                info.hwndMoveSize == toNativeHwnd(self->window->winId()) &&
                (message->message == WM_KEYDOWN || message->message == WM_KEYUP ||
                 message->message == WM_SYSKEYDOWN || message->message == WM_SYSKEYUP)) {
                QScopedValueRollback<bool> guard(self->translating, true);
                self->handled = false;
                // USER32's modal move loop bypasses Qt's event dispatcher.
                // Use Qt's window procedure for layout/modifier translation,
                // then deliver the translated event before the loop resumes.
                SendMessageW(toNativeHwnd(self->window->winId()), message->message, message->wParam,
                             message->lParam);
                QWindowSystemInterface::flushWindowSystemEvents();
                if (self->handled) {
                    message->message = WM_NULL;
                    message->wParam = 0;
                    message->lParam = 0;
                }
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
#endif
};

#if defined(Q_OS_WIN) || defined(_WIN32)
thread_local screenshot_pinned_window_native::SystemMoveKeyboard::Impl*
    screenshot_pinned_window_native::SystemMoveKeyboard::Impl::active = nullptr;
#endif

screenshot_pinned_window_native::SystemMoveKeyboard::SystemMoveKeyboard(QWidget* window)
    : m_impl(std::make_unique<Impl>(window)) {}

screenshot_pinned_window_native::SystemMoveKeyboard::~SystemMoveKeyboard() {
    stop();
}

void screenshot_pinned_window_native::SystemMoveKeyboard::setKeyCombinations(
    const QList<QKeyCombination>& combinations) {
    m_impl->combinations = combinations;
}

bool screenshot_pinned_window_native::SystemMoveKeyboard::start() {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (m_impl->hook != nullptr) {
        return true;
    }
    if (Impl::active != nullptr || m_impl->window == nullptr ||
        m_impl->window->windowHandle() == nullptr) {
        return false;
    }
    m_impl->hook = SetWindowsHookExW(WH_GETMESSAGE, Impl::filter, nullptr, GetCurrentThreadId());
    if (m_impl->hook == nullptr) {
        return false;
    }
    Impl::active = m_impl.get();
    m_impl->window->windowHandle()->installEventFilter(m_impl.get());
#endif
    return true;
}

void screenshot_pinned_window_native::SystemMoveKeyboard::stop() {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (m_impl->hook != nullptr) {
        UnhookWindowsHookEx(m_impl->hook);
        m_impl->hook = nullptr;
        Impl::active = nullptr;
        if (m_impl->window != nullptr && m_impl->window->windowHandle() != nullptr) {
            m_impl->window->windowHandle()->removeEventFilter(m_impl.get());
        }
        m_impl->pressedKeys.clear();
    }
#endif
}

bool screenshot_pinned_window_native::applyClientGeometry(WId windowId, const QRect& geometry,
                                                          GeometryUpdate update) {
    if (!geometry.isValid() || geometry.isEmpty()) {
        return false;
    }

#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return false;
    }
    if (IsIconic(hwnd) != FALSE)
        return applyMinimizedClientGeometry(hwnd, geometry);

    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if (update == GeometryUpdate::DiscardClientPixels) {
        flags |= SWP_NOCOPYBITS;
    }
    if (SetWindowPos(hwnd, nullptr, geometry.left(), geometry.top(), geometry.width(),
                     geometry.height(), flags) == FALSE) {
        return false;
    }
    if (currentClientGeometry(windowId) == geometry)
        return true;
    // Qt can round a fractional-DPI QWidget size back to a logical pixel in
    // WM_WINDOWPOSCHANGING. The controller already owns this physical target.
    if (SetWindowPos(hwnd, nullptr, geometry.left(), geometry.top(), geometry.width(),
                     geometry.height(), flags | SWP_NOSENDCHANGING) == FALSE)
        return false;
    return currentClientGeometry(windowId) == geometry;
#else
    Q_UNUSED(windowId);
    Q_UNUSED(update);
    return true;
#endif
}

bool screenshot_pinned_window_native::isMinimized(WId windowId) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    return hwnd != nullptr && IsIconic(hwnd) != FALSE;
#else
    Q_UNUSED(windowId);
    return false;
#endif
}

QRect screenshot_pinned_window_native::currentClientGeometry(WId windowId) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return {};
    }
    if (IsIconic(hwnd) != FALSE)
        return normalClientGeometry(hwnd);

    RECT clientRect{};
    POINT clientTopLeft{};
    if (GetClientRect(hwnd, &clientRect) == FALSE ||
        ClientToScreen(hwnd, &clientTopLeft) == FALSE) {
        return {};
    }
    return QRect(clientTopLeft.x, clientTopLeft.y,
                 static_cast<int>(clientRect.right - clientRect.left),
                 static_cast<int>(clientRect.bottom - clientRect.top));
#else
    Q_UNUSED(windowId);
    return {};
#endif
}

QRect screenshot_pinned_window_native::currentWindowGeometry(WId windowId) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return {};
    }

    RECT windowRect{};
    if (GetWindowRect(hwnd, &windowRect) != FALSE) {
        return QRect(QPoint(windowRect.left, windowRect.top),
                     QPoint(windowRect.right - 1, windowRect.bottom - 1));
    }
    return currentClientGeometry(windowId);
#else
    Q_UNUSED(windowId);
    return {};
#endif
}

bool screenshot_pinned_window_native::trackNonClientLeave(WId windowId) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr || !IsWindow(hwnd))
        return false;
    TRACKMOUSEEVENT tracking{};
    tracking.cbSize = sizeof(tracking);
    tracking.dwFlags = TME_LEAVE | TME_NONCLIENT;
    tracking.hwndTrack = hwnd;
    return TrackMouseEvent(&tracking) != FALSE;
#else
    Q_UNUSED(windowId);
    return false;
#endif
}

bool screenshot_pinned_window_native::applySystemResizeStyle(WId windowId) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR currentStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    if (currentStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }
    // Pool acquisition and presentation attach the same prepared HWND again.
    // Its frame only needs recalculating when the resize style actually changes.
    if ((currentStyle & WS_THICKFRAME) != 0) {
        return true;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousStyle = SetWindowLongPtr(hwnd, GWL_STYLE, currentStyle | WS_THICKFRAME);
    if (previousStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }

    return SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                            SWP_NOACTIVATE) != FALSE;
#else
    Q_UNUSED(windowId);
    return false;
#endif
}

bool screenshot_pinned_window_native::setInputTransparent(WId windowId, bool transparent) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (QGuiApplication::platformName() != QStringLiteral("windows")) {
        return true;
    }
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR currentStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (currentStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }
    constexpr LONG_PTR passThroughStyles = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    const LONG_PTR requestedStyle =
        transparent ? currentStyle | passThroughStyles : currentStyle & ~passThroughStyles;
    if (requestedStyle != currentStyle) {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previousStyle = SetWindowLongPtr(hwnd, GWL_EXSTYLE, requestedStyle);
        if (previousStyle == 0 && GetLastError() != ERROR_SUCCESS) {
            return false;
        }
    }
    if (SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) ==
        FALSE) {
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR appliedStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (appliedStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }
    return transparent ? (appliedStyle & passThroughStyles) == passThroughStyles
                       : (appliedStyle & passThroughStyles) == 0;
#else
    Q_UNUSED(windowId);
    Q_UNUSED(transparent);
    return true;
#endif
}

bool screenshot_pinned_window_native::setStaysOnTop(WId windowId, bool staysOnTop) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (QGuiApplication::platformName() != QStringLiteral("windows")) {
        return false;
    }
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return false;
    }
    const HWND insertAfter = staysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
    if (SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) ==
        FALSE) {
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR extendedStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (extendedStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }
    return staysOnTop ? (extendedStyle & WS_EX_TOPMOST) != 0 : (extendedStyle & WS_EX_TOPMOST) == 0;
#else
    Q_UNUSED(windowId);
    Q_UNUSED(staysOnTop);
    return false;
#endif
}

bool screenshot_pinned_window_native::activateWindow(WId windowId) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return false;
    }

    if (GetForegroundWindow() == hwnd ||
        (SetForegroundWindow(hwnd) != FALSE && GetForegroundWindow() == hwnd)) {
        static_cast<void>(BringWindowToTop(hwnd));
        static_cast<void>(SetFocus(hwnd));
        return true;
    }

    const HWND foregroundHwnd = GetForegroundWindow();
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD foregroundThread =
        foregroundHwnd != nullptr ? GetWindowThreadProcessId(foregroundHwnd, nullptr) : 0;
    const bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
                          AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;

    static_cast<void>(SetForegroundWindow(hwnd));
    static_cast<void>(BringWindowToTop(hwnd));
    static_cast<void>(SetActiveWindow(hwnd));
    static_cast<void>(SetFocus(hwnd));
    const bool activated = GetForegroundWindow() == hwnd;

    if (attached) {
        static_cast<void>(AttachThreadInput(currentThread, foregroundThread, FALSE));
    }
    return activated;
#else
    Q_UNUSED(windowId);
    return false;
#endif
}

bool screenshot_pinned_window_native::installSynchronizedResize(
    WId windowId, const bool* interactiveResizeActive) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    return hwnd != nullptr && interactiveResizeActive != nullptr &&
           SetWindowSubclass(hwnd, synchronizedResizeSubclassProc, kSynchronizedResizeSubclassId,
                             reinterpret_cast<DWORD_PTR>(interactiveResizeActive)) != FALSE;
#else
    Q_UNUSED(windowId);
    Q_UNUSED(interactiveResizeActive);
    return true;
#endif
}

void screenshot_pinned_window_native::removeSynchronizedResize(WId windowId) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd != nullptr) {
        static_cast<void>(RemoveWindowSubclass(hwnd, synchronizedResizeSubclassProc,
                                               kSynchronizedResizeSubclassId));
    }
#else
    Q_UNUSED(windowId);
#endif
}

bool screenshot_pinned_window_native::applyCursor(Qt::CursorShape shape) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    QPlatformNativeInterface* nativeInterface = QGuiApplication::platformNativeInterface();
    if (nativeInterface == nullptr) {
        return false;
    }

    HCURSOR cursorHandle = static_cast<HCURSOR>(
        nativeInterface->nativeResourceForCursor(QByteArrayLiteral("hcursor"), QCursor(shape)));
    if (cursorHandle == nullptr) {
        return false;
    }

    SetCursor(cursorHandle);
    return GetCursor() == cursorHandle;
#else
    Q_UNUSED(shape);
    return false;
#endif
}

bool screenshot_pinned_window_native::synchronizeClientPaint(WId windowId,
                                                             PaintSynchronization synchronization) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    const HWND hwnd = toNativeHwnd(windowId);
    if (hwnd == nullptr) {
        return false;
    }
    UINT flags = RDW_UPDATENOW | RDW_ALLCHILDREN;
    if (synchronization == PaintSynchronization::InvalidateAndUpdate) {
        flags |= RDW_INVALIDATE;
    }
    return RedrawWindow(hwnd, nullptr, nullptr, flags) != FALSE;
#else
    Q_UNUSED(windowId);
    Q_UNUSED(synchronization);
    return true;
#endif
}
