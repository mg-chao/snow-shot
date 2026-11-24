#include "../../presentation/pinned/pinnedwindowplatform.h"
#include "snow_shot/presentation/screenshotpinnedwindow.h"
#include "snow_shot/presentation/pinnedwindowselectioncontroller.h"
#include "snow_shot/presentation/pinnedgeometry.h"
#include "pinnedwindownative.h"
#include "../../presentation/pinned/screenshotpinnednativegeometrycontroller.h"
#include "../../presentation/pinned/screenshotpinnedgeometrymapping.h"
#include "../../presentation/pinned/screenshotpinnedhidetotopcontroller.h"
#include "../../presentation/pinned/screenshotpinnedshakegesture.h"
#include "snow_shot/presentation/screenshotpinnededitcontroller.h"
#include "snow_shot/presentation/screenshotrecognitionwindow.h"
#include <QWindow>
#include <QCursor>
#include <QTimer>
#include <qt_windows.h>
#include <windowsx.h>
#include <algorithm>

namespace native = screenshot_pinned_window_native;
namespace resize_geometry = screenshot_pinned_resize_geometry;
namespace {
constexpr int kMinimumScalePercent = 10;
constexpr int kMaximumScalePercent = 500;
template <typename T> T* pointerFromLParam(LPARAM value) {
    return reinterpret_cast<T*>(value);
}
QSize physicalSizeAtScale(const QSize& size, int percent) {
    return snow_shot::presentation::pinnedOuterSize(
        resize_geometry::scaledSize(size, percent / 100.));
}
#if defined(Q_OS_WIN) || defined(_WIN32)
Qt::Edges resizeEdgesForNativeHitTest(LRESULT hitTest) {
    switch (hitTest) {
    case HTLEFT:
        return Qt::LeftEdge;
    case HTRIGHT:
        return Qt::RightEdge;
    case HTTOP:
        return Qt::TopEdge;
    case HTBOTTOM:
        return Qt::BottomEdge;
    case HTTOPLEFT:
        return Qt::TopEdge | Qt::LeftEdge;
    case HTTOPRIGHT:
        return Qt::TopEdge | Qt::RightEdge;
    case HTBOTTOMLEFT:
        return Qt::BottomEdge | Qt::LeftEdge;
    case HTBOTTOMRIGHT:
        return Qt::BottomEdge | Qt::RightEdge;
    default:
        return {};
    }
}

bool dragHandleForSizingEdge(WPARAM sizingEdge, resize_geometry::DragHandle* handle) {
    if (handle == nullptr) {
        return false;
    }
    switch (sizingEdge) {
    case WMSZ_TOPLEFT:
        *handle = resize_geometry::DragHandle::TopLeft;
        return true;
    case WMSZ_TOPRIGHT:
        *handle = resize_geometry::DragHandle::TopRight;
        return true;
    case WMSZ_BOTTOMRIGHT:
        *handle = resize_geometry::DragHandle::BottomRight;
        return true;
    case WMSZ_BOTTOMLEFT:
        *handle = resize_geometry::DragHandle::BottomLeft;
        return true;
    case WMSZ_TOP:
        *handle = resize_geometry::DragHandle::Top;
        return true;
    case WMSZ_RIGHT:
        *handle = resize_geometry::DragHandle::Right;
        return true;
    case WMSZ_BOTTOM:
        *handle = resize_geometry::DragHandle::Bottom;
        return true;
    case WMSZ_LEFT:
        *handle = resize_geometry::DragHandle::Left;
        return true;
    default:
        return false;
    }
}

QRect qRectFromNativeRect(const RECT& rect) {
    return QRect(rect.left, rect.top, std::max(1, static_cast<int>(rect.right - rect.left)),
                 std::max(1, static_cast<int>(rect.bottom - rect.top)));
}

void writeNativeRect(const QRect& source, RECT* target) {
    if (target == nullptr) {
        return;
    }
    target->left = source.left();
    target->top = source.top();
    target->right = source.left() + source.width();
    target->bottom = source.top() + source.height();
}
#endif

} // namespace
bool PinnedWindowWindowsEvents::handle(ScreenshotPinnedWindow& window, const QByteArray& eventType,
                                       void* message, qintptr* result) {
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    const bool isWindowsMessage = eventType == QByteArrayLiteral("windows_generic_MSG") ||
                                  eventType == QByteArrayLiteral("windows_dispatcher_MSG");
    if (isWindowsMessage && message && static_cast<const MSG*>(message)->message == WM_NCHITTEST) {
        // Hit testing can precede any key event delivered to this nonactivating
        // window. Sample the live device state once for every routing policy.
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            modifiers |= Qt::ControlModifier;
        if (GetAsyncKeyState(VK_MENU) & 0x8000)
            modifiers |= Qt::AltModifier;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            modifiers |= Qt::ShiftModifier;
        if (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0)
            modifiers |= Qt::MetaModifier;
    }
    return handle(window, eventType, message, result, modifiers);
}

bool PinnedWindowWindowsEvents::handle(ScreenshotPinnedWindow& window, const QByteArray& eventType,
                                       void* message, qintptr* result,
                                       Qt::KeyboardModifiers modifiers) {

    const bool isWindowsMessage = eventType == QByteArrayLiteral("windows_generic_MSG") ||
                                  eventType == QByteArrayLiteral("windows_dispatcher_MSG");
    if (isWindowsMessage && message != nullptr) {
        auto* nativeMessage = static_cast<MSG*>(message);
        const HWND pinnedHwnd = nativeMessage->hwnd;
        if (pinnedHwnd == nullptr) {
            return false;
        }

        // The system move keyboard hook forwards this before USER32 consumes Escape.
        if ((nativeMessage->message == WM_KEYDOWN || nativeMessage->message == WM_SYSKEYDOWN) &&
            nativeMessage->wParam == VK_ESCAPE)
            window.endWindowShake(true);

        if (nativeMessage->message == WM_GETDPISCALEDSIZE &&
            window.m_nativeGeometryController != nullptr) {
            auto* pending = pointerFromLParam<SIZE>(nativeMessage->lParam);
            if (pending == nullptr)
                return false;
            if (window.m_windowsNativeDpi == 0)
                window.m_windowsNativeDpi = GetDpiForWindow(pinnedHwnd);
            using Phase = ScreenshotPinnedNativeGeometryController::Phase;
            const bool keepTarget =
                !window.m_presented || window.m_interactionResizeHandle ||
                window.m_selectionGeometryActive || window.m_attentionOrigin.isValid() ||
                window.m_platform->isMinimized() ||
                window.m_nativeGeometryController->phase() == Phase::Programmatic;
            const QSize desired =
                keepTarget
                    ? window.m_nativeGeometryController->targetGeometry().size()
                    : resize_geometry::dpiScaledOuterSize(
                          QSize(static_cast<int>(pending->cx), static_cast<int>(pending->cy)),
                          static_cast<int>(window.m_windowsNativeDpi),
                          static_cast<int>(nativeMessage->wParam),
                          snow_shot::presentation::pinnedShadowMargin());
            if (!desired.isValid() || desired.isEmpty())
                return false;
            // Negotiate content-only scaling before USER32 computes the DPI
            // origin. WM_DPICHANGED can then retain its system suggestion and
            // the existing cursor/transaction rebasing without another resize.
            pending->cx = desired.width();
            pending->cy = desired.height();
            if (result)
                *result = TRUE;
            return true;
        }

        if (window.m_clickThroughActive) {
            if (nativeMessage->message == WM_NCHITTEST) {
                if (result != nullptr) {
                    *result = HTTRANSPARENT;
                }
                return true;
            }
            if (nativeMessage->message == WM_MOUSEACTIVATE) {
                if (result != nullptr) {
                    *result = MA_NOACTIVATE;
                }
                return true;
            }
        }

        if (window.m_hideToTop != nullptr &&
            window.m_hideToTop->state() == ScreenshotPinnedHideToTopController::State::Hidden) {
            if (nativeMessage->message == WM_NCHITTEST) {
                if (result != nullptr) {
                    *result = HTTRANSPARENT;
                }
                return true;
            }
            if (nativeMessage->message == WM_MOUSEACTIVATE) {
                if (result != nullptr) {
                    *result = MA_NOACTIVATE;
                }
                return true;
            }
        }

        if (window.m_lockedMode) {
            if (nativeMessage->message == WM_NCHITTEST) {
                window.clearWindowDragCursor();
                if (result)
                    *result = HTCLIENT;
                return true;
            }
            const WPARAM command = nativeMessage->wParam & 0xfff0;
            const bool systemGeometryCommand = nativeMessage->message == WM_SYSCOMMAND &&
                                               (command == SC_MOVE || command == SC_SIZE);
            const bool geometryPress =
                nativeMessage->message == WM_NCLBUTTONDOWN &&
                (nativeMessage->wParam == HTCAPTION ||
                 resizeEdgesForNativeHitTest(nativeMessage->wParam) != Qt::Edges());
            if (systemGeometryCommand || geometryPress ||
                nativeMessage->message == WM_ENTERSIZEMOVE) {
                window.showLockedReadout();
                if (result)
                    *result = 0;
                return true;
            }
            if (nativeMessage->message == WM_MOVING || nativeMessage->message == WM_SIZING) {
                writeNativeRect(window.authoritativeNativeGeometry(),
                                pointerFromLParam<RECT>(nativeMessage->lParam));
                window.showLockedReadout();
                if (result)
                    *result = TRUE;
                return true;
            }
        }

        // A system move can still deliver non-client pointer messages. Re-arm its leave
        // tracking after Qt dispatch, which may replace the registration.
        const UINT pointerMessage = nativeMessage->message;
        if (pointerMessage == WM_NCMOUSEMOVE) {
            window.m_nonClientPointerInside = true;
            window.setControlsPointerInside(true);
            if (!window.m_nonClientTrackingPending) {
                window.m_nonClientTrackingPending = true;
                QMetaObject::invokeMethod(
                    &window,
                    [&window] {
                        window.m_nonClientTrackingPending = false;
                        if (window.m_nonClientPointerInside && !window.m_closing &&
                            window.internalWinId() != 0 &&
                            !native::trackNonClientLeave(window.internalWinId()))
                            qWarning("Failed to track pinned window non-client mouse leave");
                    },
                    Qt::QueuedConnection);
            }
        } else if (pointerMessage == WM_MOUSEMOVE) {
            window.m_nonClientPointerInside = false;
            window.setControlsPointerInside(true);
        } else if (pointerMessage == WM_NCMOUSELEAVE) {
            if (window.m_nonClientPointerInside) {
                window.m_nonClientPointerInside = false;
                window.setControlsPointerInside(false);
            }
        } else if (pointerMessage == WM_MOUSELEAVE && !window.m_nonClientPointerInside) {
            window.setControlsPointerInside(false);
        }
        if (pointerMessage == WM_MOUSEMOVE || pointerMessage == WM_NCMOUSEMOVE ||
            pointerMessage == WM_MOUSELEAVE || pointerMessage == WM_NCMOUSELEAVE ||
            pointerMessage == WM_CAPTURECHANGED || pointerMessage == WM_CANCELMODE ||
            pointerMessage == WM_LBUTTONUP || pointerMessage == WM_NCLBUTTONUP) {
            window.m_hideToTop->refreshPointer();
        }

        const bool automaticButtonRelease =
            nativeMessage->message == WM_CAPTURECHANGED && nativeMessage->lParam == 0 &&
            ((GetKeyState(VK_LBUTTON) | GetKeyState(VK_RBUTTON) | GetKeyState(VK_MBUTTON) |
              GetKeyState(VK_XBUTTON1) | GetKeyState(VK_XBUTTON2)) &
             0x8000) == 0;
        // Qt and USER32 release capture while handing off a held-button drag.
        // A plain click can instead end USER32's pre-move tracking without
        // WM_ENTERSIZEMOVE, WM_EXITSIZEMOVE or a button-up reaching this window.
        // Its capture release must finish the pending geometry transaction.
        const bool pendingSystemMoveHandoff =
            nativeMessage->message == WM_CAPTURECHANGED && nativeMessage->lParam == 0 &&
            !automaticButtonRelease && !window.m_auxiliaryWindowInteractionActive &&
            window.m_nativeGeometryController != nullptr &&
            (window.m_nativeGeometryController->phase() ==
                 ScreenshotPinnedNativeGeometryController::Phase::MovePending ||
             window.m_nativeGeometryController->phase() ==
                 ScreenshotPinnedNativeGeometryController::Phase::Moving);
        const bool moveCancelled = window.m_windowDragActive && !pendingSystemMoveHandoff &&
                                   (nativeMessage->message == WM_CANCELMODE ||
                                    (nativeMessage->message == WM_CAPTURECHANGED &&
                                     reinterpret_cast<HWND>(nativeMessage->lParam) != pinnedHwnd));
        const bool resizeCancelled =
            window.m_systemSizingActive && nativeMessage->message == WM_CANCELMODE;
        const bool captureLost = nativeMessage->message == WM_CANCELMODE ||
                                 (nativeMessage->message == WM_CAPTURECHANGED &&
                                  reinterpret_cast<HWND>(nativeMessage->lParam) != pinnedHwnd);
        if (captureLost && !automaticButtonRelease && !window.m_startingMouseDrag)
            window.cancelMouseOperation();
        // Qt releases its automatic capture before dispatching the button-up
        // event. Keep a pending click alive for that release; explicit shared
        // geometry owns capture until its transaction has ended instead.
        if (captureLost && !automaticButtonRelease && window.m_recognitionContent != nullptr &&
            window.m_recognitionContent->lineBoxSelectionActive()) {
            window.m_recognitionContent->cancelLineBoxSelection();
        }
        if (captureLost && !automaticButtonRelease && !window.m_startingMouseDrag &&
            window.m_selectionController &&
            window.m_selectionController->cancelPointerInteraction(&window)) {
            return false;
        }
        if (window.m_interactionPlacement && captureLost && !automaticButtonRelease &&
            !window.m_startingMouseDrag) {
            window.endControlledInteraction(true);
        } else if (!window.m_interactionPlacement && (moveCancelled || resizeCancelled)) {
            static_cast<void>(window.finishNativeGeometryInteraction());
            window.m_systemSizingActive = false;
            if (window.m_windowDragActive) {
                window.finishWindowMove(!automaticButtonRelease);
            }
            if (window.m_editController != nullptr) {
                window.m_editController->endTemporaryResizeWindowTool();
            }
            window.endAuxiliaryWindowInteraction();
        }
        if (nativeMessage->message == WM_WINDOWPOSCHANGING &&
            window.m_nativeGeometryController != nullptr && !window.m_platform->isMinimized()) {
            auto* position = pointerFromLParam<WINDOWPOS>(nativeMessage->lParam);
            if (position != nullptr) {
                const bool moveRequested = (position->flags & SWP_NOMOVE) == 0;
                const bool sizeRequested = (position->flags & SWP_NOSIZE) == 0;
                QRect proposal = window.m_nativeGeometryController->targetGeometry();
                if (!proposal.isValid() || proposal.isEmpty()) {
                    proposal = native::currentClientGeometry(reinterpret_cast<WId>(pinnedHwnd));
                }
                if (proposal.isValid() && !proposal.isEmpty()) {
                    if (moveRequested) {
                        proposal.moveTopLeft(QPoint(position->x, position->y));
                    }
                    if (sizeRequested) {
                        proposal.setSize(
                            QSize(std::max(1, position->cx), std::max(1, position->cy)));
                    }
                    const QRect constrained = window.m_nativeGeometryController->constrainWindowPos(
                        proposal, moveRequested, sizeRequested);
                    if (moveRequested) {
                        position->x = constrained.x();
                        position->y = constrained.y();
                    }
                    if (sizeRequested) {
                        position->cx = constrained.width();
                        position->cy = constrained.height();
                    }
                }
                // A translucent QWidget is published with UpdateLayeredWindow.
                // Letting USER preserve/copy old client bits while Qt replaces
                // that alpha surface makes live shrinking alternate between
                // the stale surface and a cleared backing-store frame.
                if (!window.m_presented || (sizeRequested && window.m_systemSizingActive)) {
                    position->flags |= SWP_NOCOPYBITS;
                }
                if (window.m_nativeGeometryController->targetGeometry().isValid()) {
                    // The controller owns physical geometry and the caller owns
                    // pixel preservation. Qt's default handler re-rounds this
                    // rectangle through DIPs and adds SWP_NOCOPYBITS on resize,
                    // turning passive layered-surface corrections into a paint loop.
                    if (result)
                        *result = 0;
                    return true;
                }
            }
        }

        if (nativeMessage->message == WM_DPICHANGED &&
            window.m_nativeGeometryController != nullptr) {
            const unsigned int dpi = LOWORD(nativeMessage->wParam);
            if (dpi > 0)
                window.m_windowsNativeDpi = dpi;
            // Discard partial legs immediately, before Qt updates the window's QScreen.
            // A recognized shake remains latched across the display transition.
            if (window.m_shakeGesture && window.m_shakeGesture->active &&
                !window.m_shakeGesture->detector.triggered()) {
                if (const auto cursor = window.physicalCursorPosition())
                    window.beginWindowShake(QPointF(*cursor), true);
                else
                    window.endWindowShake(true);
            }
            auto* suggestedRect = pointerFromLParam<RECT>(nativeMessage->lParam);
            if (window.m_platform->isMinimized()) {
                // An iconic suggestion must not become content geometry. Qt still
                // receives the notification to update its display/DPI bookkeeping.
                if (suggestedRect)
                    writeNativeRect(window.m_nativeGeometryController->targetGeometry(),
                                    suggestedRect);
                return false;
            }
            const bool shakingDuringApply =
                window.m_attentionOrigin.isValid() && window.m_platformApplying;
            if (!shakingDuringApply)
                window.stopAttentionShake();
            if (shakingDuringApply) {
                // A transient offset can cross a display boundary. Finish the
                // current native transaction before returning to its origin;
                // never adopt a DPI resize as part of an attention animation.
                if (suggestedRect)
                    writeNativeRect(window.m_nativeGeometryController->targetGeometry(),
                                    suggestedRect);
                QTimer::singleShot(0, &window, [&window] { window.stopAttentionShake(); });
            } else if (suggestedRect != nullptr && !window.m_presented) {
                writeNativeRect(window.m_nativeGeometryController->targetGeometry(), suggestedRect);
            } else if (suggestedRect != nullptr &&
                       (window.m_interactionResizeHandle || window.m_selectionGeometryActive)) {
                writeNativeRect(window.m_nativeGeometryController->targetGeometry(), suggestedRect);
            } else if (suggestedRect != nullptr &&
                       window.m_nativeGeometryController->adoptDpiTarget(
                           qRectFromNativeRect(*suggestedRect), window.physicalCursorPosition())) {
                // The adopted target equals the system suggestion verbatim;
                // writing it back is how the proposed geometry gets applied.
                writeNativeRect(window.m_nativeGeometryController->targetGeometry(), suggestedRect);
            }
        }

        if (nativeMessage->message == WM_GETMINMAXINFO &&
            window.nativeTrackSizeConstraintsEnabled()) {
            const QSize baseline = window.orientedInitialWindowSize();
            auto* limits = pointerFromLParam<MINMAXINFO>(nativeMessage->lParam);
            if (limits != nullptr && baseline.isValid() && !baseline.isEmpty()) {
                const QSize minimumSize =
                    window.m_cropRect ? snow_shot::presentation::pinnedOuterSize(QSize(1, 1))
                                      : physicalSizeAtScale(baseline, kMinimumScalePercent);
                const QSize maximumSize =
                    physicalSizeAtScale(window.m_cropRect
                                            ? window.m_initialWindowSize.expandedTo(
                                                  window.m_initialWindowSize.transposed())
                                            : baseline,
                                        kMaximumScalePercent)
                        .expandedTo(minimumSize);
                const auto trackLimits = resize_geometry::trackSizeLimits(
                    minimumSize, maximumSize,
                    window.m_nativeGeometryController->committedGeometry().size(),
                    window.m_nativeGeometryController->targetGeometry().size());
                limits->ptMinTrackSize.x = trackLimits.minimum.width();
                limits->ptMinTrackSize.y = trackLimits.minimum.height();
                limits->ptMaxTrackSize.x = trackLimits.maximum.width();
                limits->ptMaxTrackSize.y = trackLimits.maximum.height();
                if (result != nullptr) {
                    *result = 0;
                }
                return true;
            }
        }

        if ((window.m_interactionResizeHandle || window.m_cropSelectionOrigin) &&
            nativeMessage->message == WM_NCHITTEST) {
            if (result)
                *result = HTCLIENT;
            return true;
        }
        if (nativeMessage->message == WM_SETCURSOR && window.m_windowDragCursorSet &&
            (window.m_interactionResizeHandle || LOWORD(nativeMessage->lParam) == HTCAPTION)) {
            const Qt::CursorShape dragCursorShape =
                window.m_interactionResizeHandle ? window.cursor().shape()
                : window.m_windowDragActive      ? Qt::ClosedHandCursor
                                                 : Qt::OpenHandCursor;
            // HTCAPTION bypasses Qt's client cursor path. Reapply the native
            // handle because Windows may still be holding a resize cursor.
            if (!native::applyCursor(dragCursorShape)) {
                if (QWindow* handle = window.windowHandle()) {
                    handle->unsetCursor();
                    handle->setCursor(QCursor(dragCursorShape));
                }
            }
            if (result != nullptr) {
                *result = TRUE;
            }
            return true;
        }

        if (nativeMessage->message == WM_NCHITTEST) {
            // A synthetic caption starts USER32 movement before Qt can distinguish a
            // click from a drag. The window owns that decision for every button.
            const QPoint nativePosition(GET_X_LPARAM(nativeMessage->lParam),
                                        GET_Y_LPARAM(nativeMessage->lParam));
            const ScreenshotPinnedGeometryMapping mapping(
                window.currentNativeGeometry(), window.size(), window.devicePixelRatioF());
            if (!window.m_windowDragActive && !window.m_systemSizingActive)
                window.updateWindowDragCursor(mapping.localPosition(nativePosition), modifiers);
            if (result)
                *result = HTCLIENT;
            return true;
        }

        if (!window.m_interactionPlacement && !window.m_selectionGeometryActive &&
            (nativeMessage->message == WM_NCLBUTTONUP || nativeMessage->message == WM_LBUTTONUP)) {
            static_cast<void>(window.finishNativeGeometryInteraction());
            if (nativeMessage->wParam == HTCAPTION || window.m_windowDragActive) {
                window.finishWindowMove();
            }
            if (window.m_editController != nullptr) {
                window.m_editController->endTemporaryResizeWindowTool();
            }
            window.endAuxiliaryWindowInteraction();
        }

        if (nativeMessage->message == WM_ENTERSIZEMOVE) {
            window.stopAttentionShake();
            window.beginAuxiliaryWindowInteraction();
            if (window.m_nativeGeometryController != nullptr) {
                const auto phase = window.m_nativeGeometryController->phase();
                if (phase == ScreenshotPinnedNativeGeometryController::Phase::ResizePending ||
                    phase == ScreenshotPinnedNativeGeometryController::Phase::Resizing) {
                    window.m_preserveScaleForSettledGeometry = false;
                    window.m_systemSizingActive = true;
                }
            }
            // startSystemMove posts a request; USER32 can enter its move loop much later.
            // Measure gesture legs from actual drag ownership, retaining the press-time opt-in.
            if (window.m_shakeGesture && window.m_shakeGesture->active) {
                if (const auto cursor = window.physicalCursorPosition())
                    window.beginWindowShake(QPointF(*cursor), true);
                else
                    window.endWindowShake(true);
            }
        }

        if (nativeMessage->message == WM_MOVING && window.m_nativeGeometryController != nullptr) {
            auto* proposedNativeRect = pointerFromLParam<RECT>(nativeMessage->lParam);
            const auto cursor = window.physicalCursorPosition();
            if (proposedNativeRect != nullptr && cursor) {
                const QRect target = window.m_nativeGeometryController->updateMove(
                    qRectFromNativeRect(*proposedNativeRect), *cursor);
                if (target.isValid() && !target.isEmpty()) {
                    if (target != window.m_nativeGeometryController->committedGeometry()) {
                        window.exitHideToTop();
                    }
                    writeNativeRect(target, proposedNativeRect);
                    window.sampleWindowShake(QPointF(*cursor));
                    if (result != nullptr) {
                        *result = TRUE;
                    }
                    return true;
                }
            }
        }

        if (nativeMessage->message == WM_SIZING && window.interactiveResizingEnabled()) {
            window.m_preserveScaleForSettledGeometry = false;
            resize_geometry::DragHandle handle = resize_geometry::DragHandle::BottomRight;
            auto* proposedNativeRect = pointerFromLParam<RECT>(nativeMessage->lParam);
            const QSize baseline = window.orientedInitialWindowSize();
            if (proposedNativeRect != nullptr &&
                dragHandleForSizingEdge(nativeMessage->wParam, &handle) &&
                window.m_nativeGeometryController != nullptr) {
                const std::optional<QRect> modified =
                    window.m_nativeGeometryController->updateResize(
                        qRectFromNativeRect(*proposedNativeRect), handle, baseline,
                        kMinimumScalePercent / 100.0, kMaximumScalePercent / 100.0,
                        snow_shot::presentation::pinnedShadowMargin());
                if (!modified.has_value()) {
                    return false;
                }
                window.exitHideToTop();
                writeNativeRect(*modified, proposedNativeRect);
                window.m_systemSizingActive = true;
                window.setEffectiveScale(
                    100.0 *
                        (modified->width() - 2 * snow_shot::presentation::pinnedShadowMargin()) /
                        std::max(1, baseline.width()),
                    true);
                if (result != nullptr) {
                    *result = TRUE;
                }
                return true;
            }
        }

        if (nativeMessage->message == WM_EXITSIZEMOVE && !window.m_selectionGeometryActive) {
            static_cast<void>(window.finishNativeGeometryInteraction());
            window.m_systemSizingActive = false;
            if (window.m_windowDragActive) {
                window.finishWindowMove();
            }
            if (window.m_editController != nullptr) {
                window.m_editController->endTemporaryResizeWindowTool();
            }
            window.endAuxiliaryWindowInteraction();
        }

        if (nativeMessage->message == WM_WINDOWPOSCHANGED && window.m_platform &&
            window.m_platform->stackingChanged) {
            const auto* position = pointerFromLParam<WINDOWPOS>(nativeMessage->lParam);
            if (position && !(position->flags & SWP_NOZORDER))
                window.m_platform->stackingChanged();
        }
        if (nativeMessage->message == WM_WINDOWPOSCHANGED &&
            window.m_nativeGeometryController != nullptr && window.m_presented) {
            window.handleNativeGeometryObservation();
        }
    }

    return false;
}
