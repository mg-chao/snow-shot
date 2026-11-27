#include "snow_shot/shortcuts/shortcutbinding.h"
#include "snow_shot/presentation/pinnedgeometry.h"
#include "snow_shot/presentation/screenshotwheelinput.h"
#include "snow_shot/presentation/screenshotpinnedwindow.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/presentation/pinnedwindowselectioncontroller.h"
#include "pinnedwindowplatform.h"
#include "screenshotpinnednativegeometrycontroller.h"
#include "screenshotpinnedresizegeometry.h"
#include "screenshotpinnedcropgeometry.h"
#include "screenshotpinneddragexport.h"
#include "screenshotpinnedhidetotopcontroller.h"
#include "screenshotpinnedopacity.h"
#include "screenshotpinnedshakegesture.h"
#include "snow_shot/presentation/pinnedwindowgroupmanager.h"
#include "snow_shot/presentation/screenshotpinnededitcontroller.h"
#include "snow_shot/presentation/screenshotrecognitionsessioncontroller.h"
#include "snow_draw_engine_qt/snow_canvas_widget.h"
#include "snow_shot/presentation/screenshotrecognitionwindow.h"
#include "widgets/button.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QScopedValueRollback>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <utility>

namespace platform = snow_shot::presentation;
namespace resize_geometry = screenshot_pinned_resize_geometry;
namespace {
bool isGestureEscape(const QKeyEvent& event) {
    const auto key = snow_shot::shortcuts::commandKey(event);
    return key == Qt::Key_Escape || (key == Qt::Key_unknown && event.key() == Qt::Key_Escape);
}

Qt::CursorShape resizeCursor(int handle) {
    using H = resize_geometry::DragHandle;
    switch (H(handle)) {
    case H::TopLeft:
    case H::BottomRight:
        return Qt::SizeFDiagCursor;
    case H::TopRight:
    case H::BottomLeft:
        return Qt::SizeBDiagCursor;
    case H::Top:
    case H::Bottom:
        return Qt::SizeVerCursor;
    case H::Left:
    case H::Right:
        return Qt::SizeHorCursor;
    }
    return Qt::ArrowCursor;
}
} // namespace

void ScreenshotPinnedWindow::beginWindowShake(const QPointF& pointer, bool physical) {
    endWindowShake(true);
    if (isLiveWindow() || !snow_shot::storage::PinToScreenSettings().shakeWindowToHideOthers() ||
        !m_presented || m_closing || !m_windowDragActive || m_systemSizingActive ||
        m_selectionGeometryActive || !screen())
        return;
    if (!m_shakeGesture)
        m_shakeGesture = std::make_unique<ScreenshotPinnedShakeGesture>();
    auto& gesture = *m_shakeGesture;
    gesture.physical = physical;
    gesture.display = screen();
    gesture.scale = physical ? screen()->devicePixelRatio() : 1;
    gesture.pointer = pointer;
    gesture.logicalPosition = {};
    gesture.detector.begin({});
    gesture.clock.start();
    gesture.active = true;
}

void ScreenshotPinnedWindow::sampleWindowShake(const QPointF& pointer) {
    if (m_shakeGesture && m_shakeGesture->active && !m_shakeGesture->detector.triggered())
        updateWindowShake(pointer, m_shakeGesture->clock.elapsed());
}

void ScreenshotPinnedWindow::updateWindowShake(const QPointF& pointer, qint64 timeMs) {
    if (!m_shakeGesture || !m_shakeGesture->active || m_shakeGesture->detector.triggered())
        return;
    auto& gesture = *m_shakeGesture;
    if (!m_windowDragActive || m_systemSizingActive || m_selectionGeometryActive || m_closing ||
        !isVisible() || !screen()) {
        endWindowShake(true);
        return;
    }
    const qreal scale = gesture.physical ? screen()->devicePixelRatio() : 1;
    if (gesture.display != screen() || gesture.scale != scale) {
        gesture.display = screen();
        gesture.scale = scale;
        gesture.pointer = pointer;
        gesture.logicalPosition = {};
        gesture.detector.begin({}, timeMs);
        return;
    }
    gesture.logicalPosition += (pointer - gesture.pointer) / gesture.scale;
    gesture.pointer = pointer;
    if (!gesture.detector.sample(gesture.logicalPosition, timeMs))
        return;
    gesture.pending = true;
    const quint64 generation = gesture.generation;
    const quint64 presentation = m_presentationGeneration;
    const QString group = m_groupId;
    QTimer::singleShot(0, this, [this, generation, presentation, group] {
        if (!m_shakeGesture || !m_shakeGesture->pending || m_shakeGesture->generation != generation)
            return;
        m_shakeGesture->pending = false;
        if (m_presentationGeneration != presentation || !sourcePinAvailable() || !isVisible() ||
            m_groupId != group || (m_groupManager && m_groupManager->activeGroupId() != group) ||
            !snow_shot::storage::PinToScreenSettings().shakeWindowToHideOthers())
            return;
        const QPointer<ScreenshotPinnedWindow> lifetime(this);
        const auto result = toggleOtherWindowsForShake();
        if (lifetime && m_presentationGeneration == presentation && !m_closing && result.success &&
            result.changed) {
            m_shakeMessageAction = result.action;
            showShakeMessage();
        }
    });
}

void ScreenshotPinnedWindow::endWindowShake(bool cancel) {
    if (!m_shakeGesture)
        return;
    m_shakeGesture->active = false;
    m_shakeGesture->detector.reset();
    if (cancel) {
        ++m_shakeGesture->generation;
        m_shakeGesture->pending = false;
    }
}

bool ScreenshotPinnedWindow::exportDragEnabledAt(const QPoint& position,
                                                 Qt::KeyboardModifiers modifiers) const {
    if (isLiveWindow())
        return false;
    return m_presented && !m_closing && !m_clickThroughActive && !m_geometryAnimating &&
           !m_interactionPlacement && !m_windowDragActive && !m_ocrMode &&
           !(m_hiddenTextSelection && modifiers.testFlag(Qt::AltModifier)) && m_canvas &&
           contentViewRect().contains(position) && !isControlsPanelPosition(position) &&
           (!interactiveResizingEnabled() || !resizeHandleAt(position));
}

void ScreenshotPinnedWindow::cancelExportDrag() {
    ++m_exportDragGeneration;
    m_exportDragPreparing = false;
    if (m_exportDragOrigin) {
        m_exportDragOrigin.reset();
        qApp->removeEventFilter(this);
    }
    if (m_dragExport)
        m_dragExport->cancel();
}

bool ScreenshotPinnedWindow::handleExportDrag(QObject* watched, QEvent* event) {
    if (!event)
        return false;
    // Native dragging owns Escape/release and can deactivate its source window.
    if (m_dragExport && m_dragExport->dragging())
        return false;
    if (m_exportDragOrigin) {
        if ((event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate ||
             event->type() == QEvent::Close) &&
            watched == this) {
            cancelExportDrag();
            return false;
        }
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ||
            event->type() == QEvent::KeyRelease) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                if (event->type() == QEvent::KeyRelease)
                    cancelExportDrag();
                else if (event->type() == QEvent::KeyPress) {
                    m_exportDragAborted = true;
                    if (m_dragExport)
                        m_dragExport->cancel();
                }
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent*>(event)->button() == m_exportButton) {
            cancelExportDrag();
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (!mouse->buttons().testFlag(m_exportButton)) {
                cancelExportDrag();
                return true;
            }
            if (!m_exportDragAborted && !m_exportDragPreparing &&
                (!m_dragExport || !m_dragExport->busy()) &&
                (mouse->globalPosition().toPoint() - *m_exportDragOrigin).manhattanLength() >=
                    QApplication::startDragDistance())
                beginExportDrag();
            return true;
        }
        return false;
    }
    return false;
}

void ScreenshotPinnedWindow::reconcilePlatformEnvironment(bool layoutChanged) {
    if (m_selectionGeometryActive) {
        if (layoutChanged && m_selectionController)
            m_selectionController->cancelGeometry();
        else
            return;
    }
    if (!m_platformApplying)
        stopAttentionShake();
    if (!m_platform || !m_platform->usesControlledInteraction() || !m_presented || m_closing ||
        m_platformApplying)
        return;
    m_platformRecoveryPending = m_platformRecoveryPending || layoutChanged;
    if (m_platformReconciliationPending)
        return;
    m_platformReconciliationPending = true;
    QTimer::singleShot(0, this, [this] {
        m_platformReconciliationPending = false;
        if (m_selectionGeometryActive) {
            if (m_platformRecoveryPending && m_selectionController)
                m_selectionController->cancelGeometry();
            else
                return;
        }
        if (m_closing || !m_nativeGeometryController || !m_platformPlacement || m_platformApplying)
            return;
        if (m_interactionPlacement) {
            const auto displays = QGuiApplication::screens();
            const bool originExists =
                std::any_of(displays.begin(), displays.end(), [this](QScreen* display) {
                    return !m_interactionPlacement->displaySerial.isEmpty()
                               ? display->serialNumber() == m_interactionPlacement->displaySerial
                               : display->name() == m_interactionPlacement->displayName;
                });
            if (originExists)
                return;
            endControlledInteraction(false);
        }
        const bool recover = std::exchange(m_platformRecoveryPending, false);
        auto placement = *m_platformPlacement;
        QScreen* target = platform::pinnedDisplay(placement, screen());
        if (!target)
            return;
        if (recover)
            placement = platform::recoverPinnedPlacement(placement, *target);
        m_platformApplying = true;
        const bool applied = m_platform->applyStablePlacement(placement, target);
        m_platformApplying = false;
        if (!applied)
            return;
        m_platformPlacement = m_platform->placement();
        if (!m_platformPlacement)
            return;
        target = platform::pinnedDisplay(*m_platformPlacement, target);
        if (m_preThumbnailPlacement.isValid()) {
            if (recover)
                m_preThumbnailPlacement =
                    platform::recoverPinnedPlacement(m_preThumbnailPlacement, *target);
            m_preThumbnailNativeGeometry = platform::pinnedWindowRect(
                m_preThumbnailPlacement, *platform::pinnedDisplay(m_preThumbnailPlacement, target));
        }
        const QRect targetRect = platform::pinnedWindowRect(*m_platformPlacement, *target);
        if (m_nativeGeometryController->beginProgrammatic(
                targetRect, ScreenshotPinnedNativeGeometryController::Origin::DpiTransition)) {
            static_cast<void>(m_nativeGeometryController->commitTarget());
        }
        m_preserveScaleForSettledGeometry = true;
        updateCanvasViewport();
        updateControlsGeometry();
        if (hideToTopActive())
            m_hideToTop->reconcileScreen(screenshot_pinned_hide_to_top::screenGeometry(target));
        if (m_clickThroughActive && !updateClickThroughExitButtonGeometry())
            static_cast<void>(setClickThroughMode(false));
        if (m_editController)
            m_editController->updatePlacement();
        schedulePersistence();
    });
}

bool ScreenshotPinnedWindow::beginControlledInteraction(const QPointF& desktopPosition,
                                                        std::optional<int> handle) {
    if (m_lockedMode) {
        showLockedReadout();
        return false;
    }
    stopAttentionShake();
    if (m_interactionPlacement || m_cropJob.isValid() || m_contentReplacementJob.isValid() ||
        m_cropSelectionOrigin || m_closing || m_geometryAnimating || !screen())
        return false;
    const auto placement = m_platform->placement();
    if (!placement)
        return false;
    if (handle ? !interactiveResizingEnabled() : (!windowDragEnabled() && !m_clickThroughActive))
        return false;
    exitHideToTop();
    const QRect pixels = platform::pinnedWindowRect(*placement, *screen());
    const bool begun =
        handle ? m_nativeGeometryController->beginResize(resize_geometry::DragHandle(*handle))
               : m_nativeGeometryController->beginMove(pixels.topLeft());
    if (!begun)
        return false;
    if (!handle && m_recognitionContent && (m_ocrMode || m_hiddenTextSelection)) {
        m_recognitionContent->clearTextSelection();
    }
    resetPinnedGestures();
    m_interactionCropRect = handle ? m_cropRect : std::nullopt;
    m_interactionCropCanvas = visibleCanvasRect();
    m_interactionPlacement = placement;
    m_interactionResizeHandle = handle;
    m_interactionEffectiveResizeHandle = handle.value_or(0);
    m_interactionNativePointer = handle && !m_platform->usesControlledInteraction()
                                     ? physicalCursorPosition()
                                     : std::nullopt;
    m_interactionPointer = desktopPosition;
    m_interactionAnchor =
        (desktopPosition - platform::pinnedDesktopRect(*placement, *screen()).topLeft()) *
        platform::pinnedGeometryScale(screen()->devicePixelRatio());
    m_systemSizingActive = handle.has_value();
    m_windowDragActive = !handle.has_value();
    if (!handle)
        beginWindowShake(desktopPosition, false);
    if (m_editController) {
        if (handle && m_editController->editMode())
            static_cast<void>(m_editController->beginTemporaryResizeWindowTool());
    }
    beginAuxiliaryWindowInteraction();
    if (!m_clickThroughActive)
        static_cast<void>(m_platform->activate());
    m_interactionGrabber = QWidget::mouseGrabber();
    if (!m_interactionGrabber) {
        m_interactionGrabber = this;
        grabMouse();
    }
    if (!m_mouseStroke) {
        // Direct platform moves and the click-through handle have already begun a drag.
        MouseStroke stroke;
        stroke.press.button = stroke.press.reportedButton = Qt::LeftButton;
        stroke.press.drag = snow_shot::mouse::Action::Move;
        stroke.press.origin = desktopPosition;
        stroke.press.crossed = stroke.started = true;
        stroke.receiver = m_interactionGrabber;
        m_mouseStroke = stroke;
    }
    qApp->installEventFilter(this);
    setWindowDragCursor(handle ? resizeCursor(*handle) : Qt::ClosedHandCursor);
    return true;
}

void ScreenshotPinnedWindow::updateControlledInteraction(const QPointF& desktopPosition) {
    if (!m_interactionPlacement || !screen())
        return;
    auto placement = *m_interactionPlacement;
    QScreen* target = screen();
    if (!m_interactionResizeHandle) {
        if (QScreen* underPointer = platform::pinnedDisplayAt(desktopPosition))
            target = underPointer;
        placement =
            platform::pinnedPlacementAtPointer(placement, platform::pinnedDisplayGeometry(*target),
                                               desktopPosition, m_interactionAnchor);
    } else {
        QScreen* originScreen = platform::pinnedDisplay(placement, screen());
        const QRect origin = platform::pinnedWindowRect(placement, *originScreen);
        QPoint delta = ((desktopPosition - m_interactionPointer) *
                        platform::pinnedGeometryScale(originScreen->devicePixelRatio()))
                           .toPoint();
        if (m_interactionNativePointer) {
            const auto pointer = physicalCursorPosition();
            if (!pointer)
                return;
            delta = *pointer - *m_interactionNativePointer;
        }
        auto effective = resize_geometry::DragHandle(m_interactionEffectiveResizeHandle);
        QRect resized;
        if (m_interactionCropRect) {
            const QRect nativeContent = platform::pinnedContentRect(origin);
            const qreal sx =
                m_scalePercent / 100.0 * scaleReferenceSize().width() / visibleCanvasRect().width();
            const qreal sy = sx;
            const auto adjusted = screenshot_pinned_crop_geometry::adjust(
                m_interactionCropCanvas, m_backgroundCanvasRect,
                QPointF(delta.x() / sx, delta.y() / sy),
                resize_geometry::DragHandle(*m_interactionResizeHandle), effective,
                QSizeF(10 / sx, 10 / sy));
            effective = adjusted.handle;
            const auto mapping = screenshot_pinned_crop_geometry::sourceToCanvas(
                m_originalPixelSize, m_imageTransform, m_backgroundCanvasRect);
            m_cropRect = mapping.inverted().mapRect(adjusted.rect);
            resized = platform::pinnedOuterRect(QRect(
                qRound(nativeContent.x() + (adjusted.rect.x() - m_interactionCropCanvas.x()) * sx),
                qRound(nativeContent.y() + (adjusted.rect.y() - m_interactionCropCanvas.y()) * sy),
                std::max(1, qRound(adjusted.rect.width() * sx)),
                std::max(1, qRound(adjusted.rect.height() * sy))));
            m_preserveScaleForSettledGeometry = true;
        } else if (!resize_geometry::dragResizeRect(
                       origin, delta, orientedInitialWindowSize(),
                       resize_geometry::DragHandle(*m_interactionResizeHandle), .1, 5., &effective,
                       &resized, platform::pinnedShadowMargin()))
            return;
        m_interactionEffectiveResizeHandle = int(effective);
        setWindowDragCursor(resizeCursor(int(effective)));
        target = originScreen;
        placement = platform::pinnedPlacement(resized, *target);
    }
    placement.displayName = target->name();
    placement.displaySerial = target->serialNumber();
    bool settled = false;
    const int attempts = placement.units == platform::PinnedGeometryUnits::LogicalPixels ? 1 : 3;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (!m_nativeGeometryController->acceptInteractiveGeometry(
                platform::pinnedWindowRect(placement, *target))) {
            endControlledInteraction(true);
            return;
        }
        m_platformApplying = true;
        const bool applied = m_platform->applyPlacement(placement, target);
        m_platformApplying = false;
        if (!applied)
            break;
        m_platformPlacement = m_platform->placement();
        if (!m_platformPlacement)
            break;
        target = platform::pinnedDisplay(*m_platformPlacement, target);
        if (m_platformPlacement->windowSize == placement.windowSize) {
            settled = true;
            break;
        }
        if (!m_interactionResizeHandle) {
            placement = platform::pinnedPlacementAtPointer(*m_interactionPlacement,
                                                           platform::pinnedDisplayGeometry(*target),
                                                           desktopPosition, m_interactionAnchor);
        } else {
            placement.displayName = target->name();
            placement.displaySerial = target->serialNumber();
            placement.position = m_platformPlacement->position;
        }
    }
    if (!settled) {
        endControlledInteraction(true);
        return;
    }
    static_cast<void>(m_nativeGeometryController->acceptInteractiveGeometry(
        platform::pinnedWindowRect(*m_platformPlacement, *target)));
    if (m_systemSizingActive && !m_interactionCropRect)
        setEffectiveScale(
            100. * (m_platformPlacement->windowSize.width() - 2 * platform::pinnedShadowMargin()) /
                std::max(1, orientedInitialWindowSize().width()),
            true);
    if (m_interactionCropRect)
        updateCropPresentation();
    else
        updateCanvasViewport();
    if (m_clickThroughActive)
        static_cast<void>(updateClickThroughExitButtonGeometry());
    if (!m_interactionResizeHandle)
        sampleWindowShake(desktopPosition);
}

void ScreenshotPinnedWindow::endControlledInteraction(bool cancel) {
    if (!m_interactionPlacement)
        return;
    endWindowShake(cancel);
    const auto original = *m_interactionPlacement;
    m_mouseStroke.reset();
    m_interactionPlacement.reset();
    m_interactionResizeHandle.reset();
    m_interactionNativePointer.reset();
    qApp->removeEventFilter(this);
    auto grabber = m_interactionGrabber;
    m_interactionGrabber = nullptr;
    if (grabber && QWidget::mouseGrabber() == grabber)
        grabber->releaseMouse();
    if (cancel) {
        if (m_interactionCropRect)
            m_cropRect = m_interactionCropRect;
        m_nativeGeometryController->cancelPendingInteraction();
        if (QScreen* target = platform::pinnedDisplay(original, screen())) {
            m_platformApplying = true;
            const bool restored = m_platform->applyStablePlacement(original, target);
            m_platformApplying = false;
            if (restored)
                m_platformPlacement = m_platform->placement();
            else
                QTimer::singleShot(0, this, &QWidget::close);
        }
    } else {
        static_cast<void>(m_nativeGeometryController->commitTarget());
    }
    m_interactionCropRect.reset();
    m_systemSizingActive = false;
    m_windowDragActive = false;
    clearWindowDragCursor();
    if (m_editController) {
        m_editController->endTemporaryResizeWindowTool();
    }
    if (!m_closing) {
        updateCanvasViewport();
        adoptSettledNativeScale();
        refreshControlsPointerPresence();
        if (m_platformRecoveryPending)
            reconcilePlatformEnvironment();
        schedulePersistence();
    }
    endAuxiliaryWindowInteraction();
}

void ScreenshotPinnedWindow::executeMouseClick(snow_shot::mouse::Action action,
                                               const QPoint& global) {
    using enum snow_shot::mouse::Action;
    switch (action) {
    case Copy:
    case CopyClose:
        copyCurrentAndMaybeClose(action == CopyClose);
        break;
    case CopyText:
        copyPinnedWindowText(m_selectionController && m_selectionController->isSelected(this)
                                 ? m_selectionController->selectedWindows()
                                 : QList<QPointer<ScreenshotPinnedWindow>>{this});
        break;
    case Save:
        saveAsFile();
        break;
    case ResetZoom:
        applyScale(100);
        break;
    case Thumbnail:
        setThumbnailMode(!m_thumbnailMode);
        break;
    case HideAtTop:
        toggleHideToTop();
        break;
    case Recognition:
        if (!isLiveWindow())
            toggleTextRecognitionResults();
        break;
    case Close:
        requestUserClose();
        break;
    case Destroy:
        if (liveCommandAllowed(QStringLiteral("destroy_window")))
            confirmDestroy();
        break;
    case Menu:
        showContextMenu(global);
        break;
    case Annotation:
        toggleEditMode();
        break;
    case ClickThrough:
        toggleClickThrough();
        break;
    case Lock:
        setLockedMode(!m_lockedMode);
        break;
    case ShowAllWindows:
        executeMenuCommand(MenuCommand::ShowAllWindows);
        break;
    case HideOtherWindows:
        executeMenuCommand(MenuCommand::HideOtherWindows);
        break;
    case CloseOtherWindows:
        executeMenuCommand(MenuCommand::CloseOtherWindows);
        break;
    case CloseAllWindows:
        executeMenuCommand(MenuCommand::CloseAllWindows);
        break;
    case MultiSelect:
        if (m_selectionController)
            m_selectionController->toggleSelection(this);
        break;
    default:
        break;
    }
}

void ScreenshotPinnedWindow::updateWindowDragCursor(const QPointF& local,
                                                    Qt::KeyboardModifiers keys) {
    using namespace snow_shot::mouse;
    if (!m_presented || m_closing || m_selectionGeometryActive || m_interactionPlacement)
        return;
    if (m_windowDragActive) {
        setWindowDragCursor(Qt::ClosedHandCursor);
        return;
    }
    // Cursor feedback describes a primary-button drag at this point.
    const auto action = match(Category::Pinned, {Kind::Drag, Qt::LeftButton, modifiers(keys)});
    if (action == Action::Move || action == Action::ForceMove) {
        if (interactiveResizingEnabled()) {
            if (const auto handle = resizeHandleAt(local)) {
                setWindowDragCursor(resizeCursor(*handle));
                return;
            }
        }
        if (windowMoveEnabledForPointer(
                local.toPoint(), action == Action::ForceMove ? Qt::AltModifier : Qt::NoModifier)) {
            setWindowDragCursor(Qt::OpenHandCursor);
            return;
        }
    }
    clearWindowDragCursor();
}

bool ScreenshotPinnedWindow::startMouseDrag(const MouseStroke& stroke, bool released,
                                            bool spontaneous) {
    using enum snow_shot::mouse::Action;
    if (!stroke.press.drag)
        return false;
    switch (*stroke.press.drag) {
    case Crop:
        return beginCropSelection(stroke.nativeOrigin);
    case PanCrop:
        return stroke.panOrigin && cropSelectionAllowed();
    case Move:
    case ForceMove:
        if (m_lockedMode) {
            showLockedReadout();
            return false;
        }
        if (m_selectionController && m_selectionController->usesSharedGeometry(this) &&
            !(stroke.resizeHandle && m_cropRect))
            return m_selectionController->beginGeometry(this, stroke.press.origin,
                                                        stroke.resizeHandle, stroke.physicalOrigin);
        if (m_selectionController && !m_selectionController->isSelected(this))
            m_selectionController->clearSelection();
        // Keep the native cross-display move and DPI anchoring on Windows.
        if (spontaneous && !released && !m_platform->usesControlledInteraction() &&
            !stroke.resizeHandle && stroke.press.reportedButton == Qt::LeftButton &&
            startWindowMove(stroke.physicalOrigin))
            return true;
        if (!beginControlledInteraction(stroke.press.origin, stroke.resizeHandle))
            return false;
        if (stroke.resizeHandle)
            m_interactionNativePointer = stroke.physicalOrigin;
        return true;
    case Export:
        if (!released) {
            m_exportButton = stroke.press.reportedButton;
            m_exportDragOrigin = stroke.press.origin.toPoint();
            m_exportDragAborted = false;
            m_exportDragSpontaneous = spontaneous;
            qApp->installEventFilter(this);
            beginExportDrag();
        }
        return false;
    default:
        return false;
    }
}

void ScreenshotPinnedWindow::finishMouseDrag(bool cancel) {
    auto stroke = std::exchange(m_mouseStroke, std::nullopt);
    if (!stroke || !stroke->started)
        return;
    using enum snow_shot::mouse::Action;
    if (stroke->press.drag == PanCrop) {
        if (cancel)
            m_cropRect = stroke->panOrigin;
        updateCropPresentation();
        if (!cancel)
            schedulePersistence();
    } else if (stroke->press.drag == Crop) {
        endCropSelection(cancel);
    } else if (m_selectionGeometryActive && m_selectionController) {
        m_selectionController->endGeometry(cancel);
    } else if (m_interactionPlacement) {
        endControlledInteraction(cancel);
    }
}

bool ScreenshotPinnedWindow::cancelMouseOperation() {
    if (!m_mouseStroke || m_startingMouseDrag)
        return false;
    finishMouseDrag(true);
    return true;
}

bool ScreenshotPinnedWindow::handleMouseOperation(QObject* watched, QEvent* event,
                                                  bool toolFallback, bool topHandle) {
    using namespace snow_shot::mouse;
    if (!event || !m_presented || m_closing || m_destroying)
        return false;
    const auto type = event->type();
    auto& clickSequence = topHandle ? m_topHandleClickSequence : m_clickSequence;
    if ((watched == this || topHandle) &&
        (type == QEvent::WindowDeactivate || type == QEvent::Hide || type == QEvent::Close)) {
        clickSequence.reset();
    } else if (type == QEvent::UngrabMouse && (watched == this || watched == m_canvas ||
                                               watched == m_recognitionContent || topHandle)) {
        clickSequence.cancelPress();
    }
    if (m_startingMouseDrag)
        return false;
    if (type == QEvent::ContextMenu && m_recognitionContent &&
        static_cast<QContextMenuEvent*>(event)->reason() == QContextMenuEvent::Mouse) {
        const auto* widget = qobject_cast<QWidget*>(watched);
        if (widget &&
            (widget == m_recognitionContent || m_recognitionContent->isAncestorOf(widget))) {
            // The release matcher owns mouse menus. Keep keyboard context menus local to OCR.
            event->accept();
            return true;
        }
    }
    if (m_controlledEscapeRelease && type == QEvent::KeyRelease &&
        isGestureEscape(*static_cast<QKeyEvent*>(event))) {
        m_controlledEscapeRelease = false;
        event->accept();
        return true;
    }
    if (m_mouseStroke) {
        if (type == QEvent::Wheel && (m_mouseStroke->press.drag == Action::Crop ||
                                      m_mouseStroke->press.drag == Action::PanCrop)) {
            clickSequence.cancelPress();
            cancelMouseOperation();
            return false;
        }
        if (((watched == this || topHandle) &&
             (type == QEvent::WindowDeactivate || type == QEvent::Hide || type == QEvent::Close)) ||
            (type == QEvent::UngrabMouse &&
             (watched == this || watched == m_mouseStroke->receiver ||
              watched == m_interactionGrabber || topHandle)) ||
            type == QEvent::ScreenChangeInternal || type == QEvent::DevicePixelRatioChange) {
            // Controlled moves support crossing screens. Crop uses the press-time mapping.
            if ((type != QEvent::ScreenChangeInternal && type != QEvent::DevicePixelRatioChange) ||
                m_mouseStroke->press.drag == Action::Crop ||
                m_mouseStroke->press.drag == Action::PanCrop)
                cancelMouseOperation();
            return false;
        }
        if ((type == QEvent::KeyPress || type == QEvent::ShortcutOverride) &&
            isGestureEscape(*static_cast<QKeyEvent*>(event))) {
            if (type == QEvent::KeyPress) {
                m_controlledEscapeRelease = true;
                cancelMouseOperation();
            }
            event->accept();
            return true;
        }
    }
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick &&
        type != QEvent::MouseMove && type != QEvent::MouseButtonRelease)
        return false;
    auto* mouse = static_cast<QMouseEvent*>(event);
    const auto widget = qobject_cast<QWidget*>(watched);
    const bool surface =
        topHandle || watched == this || watched == m_canvas || watched == m_recognitionContent ||
        (widget && m_recognitionContent && m_recognitionContent->isAncestorOf(widget));
    if (!surface && !m_mouseStroke)
        return false;
    clickSequence.observe(*mouse, topHandle ? *widget : *this);
    if (m_selectionController &&
        (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick))
        m_selectionController->resolvePendingSelectionClick(
            this, !topHandle && clickSequence.kind() == Kind::DoubleClick);
    const QPointF local =
        topHandle ? QPointF(rect().center()) : mapFromGlobal(mouse->globalPosition());
    if (m_mouseStroke && (type == QEvent::MouseMove || type == QEvent::MouseButtonRelease))
        return continueMouseOperation(*mouse, local);
    if (type == QEvent::MouseMove) {
        if (surface && !topHandle && mouse->buttons() == Qt::NoButton) {
            updateWindowDragCursor(local, mouse->modifiers());
            // Recognition supplies its own text cursor on glyphs. On movable background the
            // host owns the hover, including events delivered to a formatted text viewport.
            if (m_windowDragCursorSet && widget && m_recognitionContent &&
                (widget == m_recognitionContent || m_recognitionContent->isAncestorOf(widget))) {
                mouse->accept();
                return true;
            }
        }
        return false;
    }
    if (type == QEvent::MouseButtonRelease || m_mouseStroke || m_clickThroughActive ||
        (m_geometryAnimating && m_pendingEdit != PendingEdit::Expansion) ||
        (!topHandle && isControlsPanelPosition(local.toPoint())))
        return false;
    return beginMouseOperation(watched, *mouse, local, toolFallback, topHandle);
}

bool ScreenshotPinnedWindow::continueMouseOperation(QMouseEvent& event, const QPointF& local) {
    using namespace snow_shot::mouse;
    const bool release = event.type() == QEvent::MouseButtonRelease;
    const auto button = m_mouseStroke->press.reportedButton;
    if (release && event.button() != button)
        return false;
    if (!release && !event.buttons().testFlag(button)) {
        cancelMouseOperation();
        return false;
    }
    const bool wasDrag = m_mouseStroke->press.crossed;
    if (!m_mouseStroke->press.updateThreshold(event.globalPosition())) {
        if (release) {
            const auto stroke = *std::exchange(m_mouseStroke, std::nullopt);
            if (stroke.press.click)
                executeMouseClick(*stroke.press.click, event.globalPosition().toPoint());
            else if (stroke.press.button == Qt::LeftButton && stroke.press.keys == Qt::NoModifier) {
                if (m_recognitionContent && (m_ocrMode || m_hiddenTextSelection))
                    m_recognitionContent->clearTextSelection();
                if (stroke.clearSelection && m_selectionController) {
                    const auto action =
                        match(Category::Pinned, {Kind::DoubleClick, Qt::LeftButton});
                    if (action == Action::CopyText || action == Action::Menu ||
                        action == Action::MultiSelect)
                        m_selectionController->deferSelectionClear(this);
                    else
                        m_selectionController->clearSelection();
                }
            }
        }
        event.accept();
        return true;
    }
    if (!wasDrag) {
        if (const auto timestamp = m_mouseStroke->recognitionPressTimestamp) {
            auto stroke = std::exchange(m_mouseStroke, std::nullopt);
            if (auto* receiver = qobject_cast<QWidget*>(stroke->receiver.data())) {
                // Give the text selection the original press before its first drag update.
                const QScopedValueRollback<bool> starting(m_startingMouseDrag, true);
                const auto& press = stroke->press;
                QMouseEvent replay(QEvent::MouseButtonPress, receiver->mapFromGlobal(press.origin),
                                   press.origin, press.reportedButton, press.reportedButton,
                                   press.keys, event.pointingDevice());
                replay.setTimestamp(*timestamp);
                QCoreApplication::sendEvent(receiver, &replay);
            }
            return false;
        }
        const auto stroke = *m_mouseStroke;
        const QScopedValueRollback<bool> starting(m_startingMouseDrag, true);
        const bool started = startMouseDrag(stroke, release, event.spontaneous());
        if (!m_mouseStroke)
            return true;
        m_mouseStroke->started = started;
        if (stroke.press.drag == Action::Export ||
            (m_windowDragActive && !m_interactionPlacement && !m_selectionGeometryActive)) {
            // QDrag and the native system move handle their own release.
            m_mouseStroke.reset();
            event.accept();
            return true;
        }
    }
    if (m_mouseStroke->started)
        updateMouseDrag(event.globalPosition(), local, release || !wasDrag);
    if (release)
        finishMouseDrag(false);
    event.accept();
    return true;
}

void ScreenshotPinnedWindow::updateMouseDrag(const QPointF& global, const QPointF& local,
                                             bool immediate) {
    using enum snow_shot::mouse::Action;
    const auto action = m_mouseStroke->press.drag;
    if (action == Crop || action == PanCrop) {
        const auto native = nativePositionForWindowPosition(local);
        if (action == Crop)
            updateCropSelection(native);
        else
            updateCropPan(*m_mouseStroke->panOrigin, native - m_mouseStroke->nativeOrigin);
    } else if (m_selectionGeometryActive && m_selectionController) {
        if (immediate)
            m_selectionController->updateGeometry(global);
        else
            m_selectionController->queueGeometryUpdate(global);
    } else {
        updateControlledInteraction(global);
    }
}

bool ScreenshotPinnedWindow::beginMouseOperation(QObject* receiver, QMouseEvent& event,
                                                 const QPointF& local, bool toolFallback,
                                                 bool topHandle) {
    using namespace snow_shot::mouse;
    MouseStroke stroke;
    const auto& clickSequence = topHandle ? m_topHandleClickSequence : m_clickSequence;
    stroke.press = resolvePress(Category::Pinned, event, clickSequence.kind(),
                                topHandle ? *static_cast<QWidget*>(receiver) : *this);
    if (m_geometryAnimating) {
        if (stroke.press.click != Action::Annotation)
            return false;
        // Retain the normal press/release threshold, without starting a drag during expansion.
        stroke.press.drag.reset();
    }
    const bool force = stroke.press.drag == Action::ForceMove;
    const auto semanticKeys = force ? Qt::AltModifier : Qt::NoModifier;
    const bool geometry = force || stroke.press.drag == Action::Move;
    if (geometry && !topHandle && (interactiveResizingEnabled() || m_lockedMode))
        stroke.resizeHandle = resizeHandleAt(local);
    if (topHandle ||
        (geometry && !stroke.resizeHandle &&
         !windowMoveEligibleForPointer(local.toPoint(), semanticKeys)) ||
        (stroke.press.drag == Action::Export &&
         !exportDragEnabledAt(local.toPoint(), Qt::NoModifier)) ||
        (stroke.press.drag == Action::Crop && !cropSelectionAllowed()) ||
        (stroke.press.drag == Action::PanCrop && (!m_cropRect || !cropSelectionAllowed())))
        stroke.press.drag.reset();
    if (!topHandle && !stroke.resizeHandle) {
        if (!toolFallback && m_canvas && m_canvas->interactionEnabled() && !m_ocrMode &&
            m_editController && m_editController->editMode())
            return false;
        if (!force && m_recognitionContent && m_recognitionContent->boxSelectionEnabled() &&
            event.type() == QEvent::MouseButtonPress && stroke.press.button == Qt::LeftButton &&
            stroke.press.keys == Qt::NoModifier)
            return false;
        if (!stroke.press.drag && (m_ocrMode || m_hiddenTextSelection) &&
            (stroke.press.button == Qt::LeftButton || stroke.press.button == Qt::MiddleButton) &&
            !windowDragEligibleAt(local.toPoint(), Qt::NoModifier)) {
            // Leave word selection and the unmodified background shortcut for dismissing OCR
            // intact. Other configured clicks can own the release over selectable content.
            if (!stroke.press.click || (stroke.press.keys == Qt::NoModifier &&
                                        (stroke.press.button == Qt::LeftButton ||
                                         stroke.press.click == Action::Recognition)))
                return false;
            if (event.button() == Qt::LeftButton)
                stroke.recognitionPressTimestamp = event.timestamp();
        }
    }
    if (stroke.press.drag == Action::PanCrop)
        stroke.panOrigin = m_cropRect;
    stroke.clearSelection = !topHandle && stroke.press.button == Qt::LeftButton &&
                            stroke.press.keys == Qt::NoModifier && m_selectionController &&
                            m_selectionController->selectedCount() > 0;
    if (!stroke.press.click && !stroke.press.drag && !stroke.clearSelection)
        return false;
    stroke.nativeOrigin = nativePositionForWindowPosition(local);
    if (geometry && event.spontaneous() && !m_platform->usesControlledInteraction())
        stroke.physicalOrigin = stroke.nativeOrigin.toPoint();
    stroke.receiver = receiver;
    m_mouseStroke = stroke;
    event.accept();
    return true;
}

void ScreenshotPinnedWindow::resetPinnedGestures() {
    m_scrollWheelSteps.reset();
    m_scrollOpacity = 0;
    m_pinchActive = false;
}

bool ScreenshotPinnedWindow::handlePinnedGesture(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide)
        resetPinnedGestures();
    if (m_clickThroughActive || m_interactionPlacement || m_geometryAnimating || m_closing) {
        resetPinnedGestures();
        return false;
    }
    if (event->type() != QEvent::NativeGesture && event->type() != QEvent::Wheel)
        return false;
    if (watched != this && watched != m_canvas && watched != m_recognitionContent)
        return false;
    if ((!m_ocrMode && m_editController && m_editController->editMode()) || !canScaleContent())
        return false;
    if (event->type() == QEvent::NativeGesture) {
        auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::BeginNativeGesture) {
            resetPinnedGestures();
            m_pinchActive = true;
            gesture->accept();
            return true;
        }
        if (gesture->gestureType() == Qt::EndNativeGesture) {
            m_pinchActive = false;
            gesture->accept();
            return true;
        }
        if (gesture->gestureType() != Qt::ZoomNativeGesture)
            return false;
        const double factor = 1. + gesture->value();
        if (std::isfinite(factor) && factor > 0) {
            const QPointF local = windowPositionForEvent(watched, gesture->position());
            applyPointerScale(std::clamp(m_scalePercent * factor, 10., 500.),
                              nativePositionForWindowPosition(local));
        }
        // Qt propagates an unaccepted native gesture from the canvas to its parent pin.
        gesture->accept();
        return true;
    }
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (wheel->phase() == Qt::ScrollMomentum || m_pinchActive)
        return true;
    if (wheel->phase() == Qt::ScrollBegin)
        resetPinnedGestures();
    if (wheel->phase() == Qt::ScrollEnd) {
        resetPinnedGestures();
        wheel->accept();
        return true;
    }
    const bool precise = platform::usesPreciseWheelDelta(*wheel);
    const int angleDelta = platform::pinnedWheelAngleDelta(*wheel);
    const int delta = precise ? wheel->pixelDelta().y() : angleDelta;
    const auto action = wheelAction(*wheel);
    if (!action)
        return false;
    if (*action == snow_shot::mouse::Action::Opacity ||
        *action == snow_shot::mouse::Action::FineOpacity) {
        m_scrollWheelSteps.reset();
        if (!precise) {
            m_scrollOpacity = 0;
            return false;
        }
        if (m_scrollOpacity == 0)
            m_scrollOpacity = m_opacityPercent;
        const int step = *action == snow_shot::mouse::Action::FineOpacity
                             ? 1
                             : snow_shot::storage::PinToScreenSettings().mouseWheelOpacityStep();
        m_scrollOpacity = std::clamp(m_scrollOpacity + delta * (step / 120.),
                                     double(screenshot_pinned_opacity::kMinimumOpacityPercent),
                                     double(screenshot_pinned_opacity::kMaximumOpacityPercent));
        setOpacityPercent(qRound(m_scrollOpacity));
    } else if (delta != 0) {
        m_scrollOpacity = 0;
        const int steps = m_scrollWheelSteps.consume(*wheel, angleDelta);
        if (steps != 0) {
            applyWheelScaleSteps(
                steps,
                nativePositionForWindowPosition(windowPositionForEvent(watched, wheel->position())),
                *action == snow_shot::mouse::Action::FineZoom ? 1 : 0);
        }
    }
    wheel->accept();
    return true;
}
