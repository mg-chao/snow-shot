#include "snow_shot/presentation/pinnedwindowselectioncontroller.h"
#include "snow_shot/presentation/pinnedgeometry.h"

#include "pinnedwindowplatform.h"
#include "pinnedwindowselectiongeometry.h"
#include "screenshotpinnednativegeometrycontroller.h"
#include "screenshotpinnedresizegeometry.h"
#include "snow_shot/presentation/screenshotpinnededitcontroller.h"
#include "snow_shot/presentation/screenshotpinnedwindow.h"
#include "snow_shot/presentation/screenshotrecognitionsessioncontroller.h"
#include "snow_shot/presentation/screenshotrecognitionwindow.h"
#include "snow_shot/shortcuts/shortcutbinding.h"
#include "snow_draw_engine_qt/snow_canvas_widget.h"
#include "widgets/button.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QTimer>
#include <QWindow>
#include <QWindowStateChangeEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace snow_shot::presentation {
namespace {
using Handle = screenshot_pinned_resize_geometry::DragHandle;

Qt::CursorShape selectionResizeCursor(int handle) {
    switch (Handle(handle)) {
    case Handle::TopLeft:
    case Handle::BottomRight:
        return Qt::SizeFDiagCursor;
    case Handle::TopRight:
    case Handle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case Handle::Top:
    case Handle::Bottom:
        return Qt::SizeVerCursor;
    case Handle::Left:
    case Handle::Right:
        return Qt::SizeHorCursor;
    }
    return Qt::ArrowCursor;
}

double dragScaleFactor(const QSize& origin, const QPointF& delta, int handle) {
    const auto pressed = Handle(handle);
    const bool left =
        pressed == Handle::Left || pressed == Handle::TopLeft || pressed == Handle::BottomLeft;
    const bool right =
        pressed == Handle::Right || pressed == Handle::TopRight || pressed == Handle::BottomRight;
    const bool top =
        pressed == Handle::Top || pressed == Handle::TopLeft || pressed == Handle::TopRight;
    const bool bottom = pressed == Handle::Bottom || pressed == Handle::BottomLeft ||
                        pressed == Handle::BottomRight;
    const double width = (origin.width() + (left ? -delta.x() : delta.x())) / origin.width();
    const double height = (origin.height() + (top ? -delta.y() : delta.y())) / origin.height();
    if ((left || right) && (top || bottom))
        return std::max(width, height);
    return left || right ? width : height;
}
} // namespace

struct PinnedWindowSelectionController::GeometryState {
    struct Member {
        QPointer<ScreenshotPinnedWindow> window;
        PinnedPlacement placement;
        QPointF desktopContentOrigin;
        QPointer<QScreen> display;
        QSizeF baseline;
        double scale = 100;
        bool preserveScale = false;
        bool persistenceDirty = false;
        bool persistenceTimerActive = false;
        int persistenceTimerRemaining = 0;
    };

    QList<Member> members;
    std::vector<double> originalScales;
    QList<QPointF> originsBuffer;
    QPointer<ScreenshotPinnedWindow> owner;
    QPointer<QWidget> grabber;
    QPointF desktopPress;
    std::optional<QPoint> nativePress;
    std::optional<int> handle;
    QPointF queuedPosition;
    bool updateQueued = false;
    bool active = false;
    bool ending = false;

    static std::shared_ptr<GeometryState>
    begin(PinnedWindowSelectionController& controller, ScreenshotPinnedWindow* owner,
          const QPointF& desktopPress, std::optional<int> handle, std::optional<QPoint> nativePress,
          bool pointerDriven) {
        auto targets = controller.selectedWindows();
        if (!owner || targets.size() < 2 || !controller.isSelected(owner) ||
            (handle && owner->m_thumbnailMode))
            return {};
        if (handle)
            targets.removeIf([](const auto& window) { return window && window->m_thumbnailMode; });
        for (const auto& window : targets) {
            if (!window || !controller.isSelectable(window) ||
                !window->m_nativeGeometryController || !window->screen() ||
                window->m_interactionPlacement || window->m_geometryAnimating ||
                window->m_nativeGeometryController->phase() !=
                    ScreenshotPinnedNativeGeometryController::Phase::Stable)
                return {};
            if (window->m_lockedMode) {
                owner->showLockedReadout();
                return {};
            }
        }
        auto state = std::make_shared<GeometryState>();
        state->owner = owner;
        state->desktopPress = desktopPress;
        state->nativePress = nativePress;
        state->handle = handle;
        state->members.reserve(targets.size());
        state->originalScales.reserve(static_cast<std::size_t>(targets.size()));
        state->originsBuffer.reserve(targets.size());
        for (const auto& window : targets) {
            window->stopAttentionShake();
            const auto placement = window->m_platform->placement();
            if (!placement)
                return {};
            QScreen* display = pinnedDisplay(*placement, window->screen());
            if (!display)
                return {};
            Member member;
            member.window = window;
            member.placement = *placement;
            member.desktopContentOrigin =
                pinnedDesktopContentRect(*placement, pinnedDisplayGeometry(*display),
                                         pinnedShadowMargin(placement->units))
                    .topLeft();
            member.display = display;
            member.baseline = window->scaleReferenceSize();
            member.scale = window->m_scalePercent;
            member.preserveScale = window->m_preserveScaleForSettledGeometry;
            member.persistenceDirty = window->m_selectionPersistenceDirty;
            if (window->m_persistenceTimer) {
                member.persistenceTimerActive = window->m_persistenceTimer->isActive();
                member.persistenceTimerRemaining = window->m_persistenceTimer->remainingTime();
            }
            state->members.append(member);
            state->originalScales.push_back(member.scale);
        }
        controller.m_geometryState = state;
        state->active = true;
        for (const auto& member : state->members) {
            auto* window = member.window.data();
            window->m_selectionGeometryActive = true;
            window->m_selectionPersistenceDirty = false;
            if (window->m_persistenceTimer)
                window->m_persistenceTimer->stop();
            if (window->m_nativeScaleSettleTimer)
                window->m_nativeScaleSettleTimer->stop();
            const bool begun =
                handle ? window->m_nativeGeometryController->beginResize(Handle(*handle))
                       : window->m_nativeGeometryController->beginMove(
                             pinnedWindowRect(member.placement, *member.display).topLeft());
            if (!begun) {
                controller.endGeometry(true);
                return {};
            }
            if (pointerDriven)
                window->resetPinnedGestures();
            window->m_windowDragActive = pointerDriven && !handle;
            window->m_systemSizingActive = handle.has_value();
            if (handle)
                window->m_preserveScaleForSettledGeometry = true;
            window->beginAuxiliaryWindowInteraction();
        }
        if (pointerDriven) {
            if (!handle && owner->m_recognitionContent &&
                (owner->m_ocrMode || owner->m_hiddenTextSelection))
                owner->m_recognitionContent->clearOcrSelection();
            if (handle && owner->m_editController && owner->m_editController->editMode())
                static_cast<void>(owner->m_editController->beginTemporaryResizeWindowTool());
            state->grabber = QWidget::mouseGrabber();
            if (!state->grabber) {
                state->grabber = owner;
                owner->grabMouse();
            }
            owner->setWindowDragCursor(handle ? selectionResizeCursor(*handle)
                                              : Qt::ClosedHandCursor);
            static_cast<void>(owner->m_platform->activate());
        }
        return state;
    }

    bool apply(const QList<QPointF>& origins, double factor = 1, bool resizing = false) {
        if (!active || origins.size() != members.size())
            return false;
        for (int index = 0; index < members.size(); ++index) {
            const auto& member = members[index];
            auto* window = member.window.data();
            if (!window || window->m_closing || window->m_lockedMode || !window->screen())
                return false;
            auto placement = member.placement;
            QPointer<QScreen> display = pinnedDisplayAt(origins[index]);
            if (!display)
                display = member.display ? member.display.data() : window->screen();
            if (!display)
                return false;
            placement = pinnedPlacementAtContentOrigin(
                std::move(placement), pinnedDisplayGeometry(*display), origins[index],
                pinnedShadowMargin(member.placement.units));
            if (resizing && std::abs(factor - 1.) > 1.e-12)
                placement.windowSize = pinnedOuterSize(
                    QSize(
                        std::max(1, qRound(member.baseline.width() * member.scale * factor / 100.)),
                        std::max(1,
                                 qRound(member.baseline.height() * member.scale * factor / 100.))),
                    placement.units);
            const QRect requested = pinnedWindowRect(placement, *display);
            const bool geometryChanged =
                window->m_nativeGeometryController->targetGeometry() != requested ||
                window->m_platformPlacement != placement;
            if (geometryChanged) {
                if (!window->m_nativeGeometryController->acceptInteractiveGeometry(requested))
                    return false;
                bool applied = false;
                {
                    const QScopedValueRollback<bool> applying(window->m_platformApplying, true);
                    applied = window->m_platform->applyExactPlacement(
                        placement, display, PinnedWindowPlatform::GeometryUpdate::PreserveContents,
                        [window](const auto& proposal, QScreen* proposalDisplay) {
                            return window->m_nativeGeometryController->acceptInteractiveGeometry(
                                pinnedWindowRect(proposal, *proposalDisplay));
                        },
                        pinnedShadowMargin(placement.units));
                }
                if (!active || !applied)
                    return false;
                const auto actual = window->m_platform->placement();
                if (!active || !actual || !display || actual->windowSize != placement.windowSize)
                    return false;
                window->m_platformPlacement = actual;
                display = pinnedDisplay(*actual, display);
                if (!display || !window->m_nativeGeometryController->acceptInteractiveGeometry(
                                    pinnedWindowRect(*actual, *display)))
                    return false;
            }
            if (resizing) {
                const double scale = member.scale * factor;
                if (std::abs(scale - window->m_scalePercent) > .001) {
                    window->invalidatePendingCopy();
                    window->m_scalePercent = std::clamp(scale, 10., 500.);
                }
                // A held drag needs each intermediate viewport. A wheel step finishes the
                // transaction immediately and refreshes every member in endGeometry().
                if (grabber)
                    window->updateCanvasViewport();
            }
        }
        if (resizing && owner)
            owner->showScaleReadout();
        return true;
    }
};

bool PinnedWindowSelectionController::geometryActive() const {
    return m_geometryState && m_geometryState->active;
}

bool PinnedWindowSelectionController::usesSharedGeometry(
    const ScreenshotPinnedWindow* window) const {
    return window && isSelected(window) && selectedCount() >= 2 && isSelectable(window);
}

bool PinnedWindowSelectionController::beginGeometry(ScreenshotPinnedWindow* window,
                                                    const QPointF& desktopPress,
                                                    std::optional<int> handle,
                                                    std::optional<QPoint> nativePress) {
    if (geometryActive())
        return false;
    cancelGeometry();
    return bool(GeometryState::begin(*this, window, desktopPress, handle, nativePress, true));
}

void PinnedWindowSelectionController::updateGeometry(const QPointF& desktopPosition) {
    const auto state = m_geometryState;
    if (!state || !state->active || !state->owner || state->ending)
        return;
    state->updateQueued = false;
    state->originsBuffer.clear();
    state->originsBuffer.reserve(state->members.size());
    double factor = 1;
    if (state->handle) {
        const auto ownerMember =
            std::find_if(state->members.begin(), state->members.end(),
                         [&state](const auto& member) { return member.window == state->owner; });
        if (ownerMember == state->members.end() || !ownerMember->display) {
            endGeometry(true);
            return;
        }
        QPointF delta = desktopPosition - state->desktopPress;
        delta *= storage::pinnedGeometryScale(ownerMember->display->devicePixelRatio(),
                                              ownerMember->placement.units);
        if (state->nativePress && !state->owner->m_platform->usesControlledInteraction()) {
            const auto cursor = state->owner->physicalCursorPosition();
            if (!cursor)
                return;
            delta = QPointF(*cursor - *state->nativePress);
        }
        const double requested =
            dragScaleFactor(pinnedContentRect(QRect(QPoint(), ownerMember->placement.windowSize),
                                              ownerMember->placement.units)
                                .size(),
                            delta, *state->handle);
        const auto clamped = pinned_window_selection_geometry::sharedScaleFactor(
            state->originalScales, std::max(requested, std::numeric_limits<double>::min()));
        if (!clamped) {
            endGeometry(true);
            return;
        }
        factor = *clamped;
    }
    QPointF translation = desktopPosition - state->desktopPress;
    if (!state->handle &&
        std::all_of(state->members.begin(), state->members.end(), [](const auto& member) {
            return member.placement.units == storage::PinnedGeometryUnits::LogicalPixels;
        })) {
        // Round the gesture once so windows on opposite sides of the desktop
        // origin retain their relative positions at half-point boundaries.
        translation = translation.toPoint();
    }
    for (const auto& member : state->members)
        state->originsBuffer.append(state->handle ? member.desktopContentOrigin
                                                  : member.desktopContentOrigin + translation);
    if (!state->apply(state->originsBuffer, factor, state->handle.has_value()))
        endGeometry(true);
}

void PinnedWindowSelectionController::endGeometry(bool cancel) {
    const auto state = m_geometryState;
    if (!state || state->ending)
        return;
    if (!cancel && state->updateQueued) {
        updateGeometry(state->queuedPosition);
        if (m_geometryState != state)
            return;
    }
    m_geometryState.reset();
    state->ending = true;
    state->active = false;
    if (state->grabber && state->owner && state->owner->m_mouseStroke &&
        state->owner->m_mouseStroke->started)
        state->owner->m_mouseStroke.reset();
    if (state->grabber && QWidget::mouseGrabber() == state->grabber)
        state->grabber->releaseMouse();
    for (const auto& member : state->members) {
        auto* window = member.window.data();
        if (!window)
            continue;
        const bool deferredPersistence = window->m_selectionPersistenceDirty;
        if (cancel && !window->m_closing && window->m_nativeGeometryController) {
            window->m_nativeGeometryController->cancelPendingInteraction();
            QScreen* display = member.display ? member.display.data() : window->screen();
            if (!display)
                display = QGuiApplication::primaryScreen();
            auto original = member.placement;
            if (!display) {
                window->m_selectionGeometryActive = false;
                window->m_selectionPersistenceDirty =
                    member.persistenceDirty || deferredPersistence;
                window->m_systemSizingActive = false;
                window->m_windowDragActive = false;
                window->m_nativeGeometryController->cancelPendingInteraction();
                QTimer::singleShot(0, window, &QWidget::close);
                continue;
            }
            if (!member.display)
                original = recoverPinnedPlacement(original, *display);
            bool restored = false;
            {
                const QScopedValueRollback<bool> applying(window->m_platformApplying, true);
                restored = window->m_platform->applyExactPlacement(
                    original, display, PinnedWindowPlatform::GeometryUpdate::PreserveContents,
                    [window](const auto& proposal, QScreen* proposalDisplay) {
                        const QRect target = pinnedWindowRect(proposal, *proposalDisplay);
                        return window->m_nativeGeometryController->phase() ==
                                       ScreenshotPinnedNativeGeometryController::Phase::Stable
                                   ? window->m_nativeGeometryController->beginProgrammatic(
                                         target, ScreenshotPinnedNativeGeometryController::Origin::
                                                     Restoration)
                                   : window->m_nativeGeometryController->acceptAppliedGeometry(
                                         target, true);
                    });
            }
            if (restored) {
                window->m_platformPlacement = window->m_platform->placement();
                const QRect geometry = window->m_platform->windowGeometry();
                if (window->m_nativeGeometryController->acceptAppliedGeometry(geometry, true))
                    static_cast<void>(window->m_nativeGeometryController->commitTarget());
            } else {
                QTimer::singleShot(0, window, &QWidget::close);
            }
            window->m_scalePercent = member.scale;
            window->m_preserveScaleForSettledGeometry = member.preserveScale;
        } else if (!window->m_closing && window->m_nativeGeometryController) {
            if (window->m_platformPlacement == member.placement &&
                std::abs(window->m_scalePercent - member.scale) <= .001)
                window->m_preserveScaleForSettledGeometry = member.preserveScale;
            const QScopedValueRollback<bool> applying(window->m_platformApplying, true);
            window->commitNativeGeometry(false);
        }
        window->m_systemSizingActive = false;
        window->m_windowDragActive = false;
        window->m_selectionGeometryActive = false;
        window->m_selectionPersistenceDirty = member.persistenceDirty || deferredPersistence;
        window->clearWindowDragCursor();
        if (window == state->owner && window->m_editController)
            window->m_editController->endTemporaryResizeWindowTool();
        if (!window->m_closing) {
            if (window->m_auxiliaryWindowInteractionActive)
                window->endAuxiliaryWindowInteraction(); // Also updates the viewport.
            else
                window->updateCanvasViewport(); // Setup can fail before every member begins.
            window->updateControlsGeometry();
            window->refreshControlsPointerPresence();
            window->refreshContextMenuIfVisible();
            const bool changed =
                !cancel && (window->m_platformPlacement != member.placement ||
                            std::abs(window->m_scalePercent - member.scale) > .001);
            if (changed || window->m_selectionPersistenceDirty)
                window->schedulePersistence();
            else if (member.persistenceTimerActive && window->m_persistenceTimer)
                window->m_persistenceTimer->start(std::max(1, member.persistenceTimerRemaining));
        }
    }
}

void PinnedWindowSelectionController::cancelGeometry() {
    endGeometry(true);
}

bool PinnedWindowSelectionController::cancelPointerInteraction(ScreenshotPinnedWindow* window) {
    const auto state = m_geometryState;
    if (window && window->cancelMouseOperation())
        return true;
    if (!window || !state || !state->active || (state->owner != window && !isSelected(window)))
        return false;
    cancelGeometry();
    return true;
}

bool PinnedWindowSelectionController::scaleBy(ScreenshotPinnedWindow* window,
                                              double requestedLeaderScale) {
    if (!usesSharedGeometry(window) || window->m_thumbnailMode || geometryActive() ||
        !std::isfinite(requestedLeaderScale) || requestedLeaderScale <= 0 ||
        window->m_scalePercent <= 0)
        return false;
    for (const auto& selected : selectedWindows()) {
        if (selected && !selected->m_thumbnailMode && !selected->canScaleContent())
            return false;
    }
    const double requested = requestedLeaderScale / window->m_scalePercent;
    cancelGeometry();
    const auto state = GeometryState::begin(*this, window, {}, int(Handle::BottomRight), {}, false);
    if (!state)
        return false;
    for (const auto& member : state->members)
        state->originsBuffer.append(member.desktopContentOrigin);
    const auto factor =
        pinned_window_selection_geometry::sharedScaleFactor(state->originalScales, requested);
    const bool applied = factor && state->apply(state->originsBuffer, *factor, true);
    endGeometry(!applied);
    return applied;
}

bool PinnedWindowSelectionController::alignSelection(SnowCanvasSelectionAlignment alignment) {
    const auto targets = selectedWindows();
    if (targets.size() < 2 || geometryActive())
        return false;
    cancelGeometry();
    const auto state = GeometryState::begin(*this, targets.first(), {}, {}, {}, false);
    if (!state)
        return false;
    QList<QRectF> rectangles;
    for (const auto& member : state->members) {
        rectangles.append(pinnedDesktopContentRect(member.placement,
                                                   pinnedDisplayGeometry(*member.display),
                                                   pinnedShadowMargin(member.placement.units)));
    }
    const auto aligned = pinned_window_selection_geometry::alignmentTargets(rectangles, alignment);
    QList<QPointF> origins;
    if (aligned) {
        for (const auto& rectangle : *aligned)
            origins.append(rectangle.topLeft());
    }
    const bool applied = aligned && state->apply(origins);
    endGeometry(!applied);
    return applied;
}

void PinnedWindowSelectionController::queueGeometryUpdate(const QPointF& position) {
    const auto state = m_geometryState;
    if (!state || !state->active)
        return;
    state->queuedPosition = position;
    if (!state->updateQueued) {
        state->updateQueued = true;
        QTimer::singleShot(0, this, [this, state] {
            if (m_geometryState == state && state->active && state->updateQueued)
                updateGeometry(state->queuedPosition);
        });
    }
}

bool PinnedWindowSelectionController::eventFilter(QObject* watched, QEvent* event) {
    if (event && event->type() == QEvent::LanguageChange)
        retranslateUi();
    if (!watched || !watched->isWidgetType() || !event)
        return QObject::eventFilter(watched, event);
    if (m_geometryState && !m_geometryState->owner)
        cancelGeometry();
    auto* widget = static_cast<QWidget*>(watched);
    if (event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange) {
        if (auto* changed = qobject_cast<ScreenshotPinnedWindow*>(widget);
            changed && m_windowIndex.contains(changed)) {
            windowStateChanged(changed);
            if (event->type() == QEvent::WindowStateChange && geometryActive() &&
                isSelected(changed)) {
                const auto previous =
                    static_cast<QWindowStateChangeEvent*>(event)->oldState() & ~Qt::WindowActive;
                const auto current = changed->windowState() & ~Qt::WindowActive;
                if (previous != current)
                    cancelGeometry();
            }
        }
    }
    if (geometryActive() && m_geometryState->owner) {
        auto* owner = m_geometryState->owner.data();
        if (owner->handleMouseOperation(watched, event))
            return true;
        if (!owner->m_startingMouseDrag && event->type() == QEvent::UngrabMouse &&
            (watched == owner || (m_geometryState && watched == m_geometryState->grabber)))
            cancelGeometry();
    }
    return QObject::eventFilter(watched, event);
}
} // namespace snow_shot::presentation
