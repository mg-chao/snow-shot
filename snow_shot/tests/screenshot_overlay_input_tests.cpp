#include "snow_shot/presentation/screenshotoverlayeventsink.h"
#include "snow_shot/presentation/screenshotoverlaywindow.h"
#include "snow_draw_engine_qt/snow_canvas_widget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QWindow>
#include <private/qevent_p.h>
#include <private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class InputSink final : public ScreenshotOverlayEventSink {
  public:
    bool selecting = true;
    bool inputAllowed = true;
    bool requireGenuineInput = false;
    bool deferSelection = false;
    Qt::KeyboardModifiers pressModifiers;
    QVector<QPointF> presses;
    QVector<QPointF> moves;
    QVector<QPointF> releases;
    QVector<QPointF> rightClicks;
    QVector<QPointF> doubleClicks;
    int wheels = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    int modifierRecomputations = 0;
    int snapClears = 0;
    void updateOverlayModifiers(Qt::KeyboardModifiers value, bool recompute) override {
        modifiers = value;
        modifierRecomputations += recompute;
    }
    void clearOverlaySnapping() override {
        ++snapClears;
    }

    bool acceptOverlayInput(bool genuine) override {
        if (requireGenuineInput && genuine)
            inputAllowed = true;
        return inputAllowed;
    }
    bool shouldHandleOverlayMouseEvent(const ScreenshotOverlayWindow*, const QPointF&,
                                       bool) const override {
        return selecting;
    }
    bool allowMouseCompletionBeforeSelection(ScreenshotOverlayWindow*,
                                             const QPointF&) const override {
        return deferSelection;
    }
    void handleOverlayMousePress(ScreenshotOverlayWindow*, const QPointF& point) override {
        pressModifiers = modifiers;
        presses.append(point);
    }
    void handleOverlayMouseMove(ScreenshotOverlayWindow*, const QPointF& point) override {
        moves.append(point);
    }
    void handleOverlayMouseRelease(ScreenshotOverlayWindow*, const QPointF& point) override {
        releases.append(point);
    }
    ScreenshotOverlayRightClickResult handleOverlayRightClick(ScreenshotOverlayWindow*,
                                                              const QPointF& point) override {
        rightClicks.append(point);
        return ScreenshotOverlayRightClickResult::Handled;
    }
    void handleMouseCompletion(snow_shot::mouse::Action action, ScreenshotOverlayWindow*,
                               const QPointF& point) override {
        require(action == snow_shot::mouse::Action::ShotCopy, "the configured gesture binds to Copy");
        doubleClicks.append(point);
    }
    bool handleOverlayWheel(ScreenshotOverlayWindow*, const QWheelEvent& event) override {
        ++wheels;
        require(event.pixelDelta() == QPoint(0, 2) && event.angleDelta() == QPoint(0, 4) &&
                    event.phase() == Qt::ScrollMomentum && event.timestamp() == 1234 &&
                    event.source() == Qt::MouseEventSynthesizedBySystem,
                "edge routing must preserve wheel gesture metadata");
        return true;
    }
    bool shouldBlockUnhandledOverlayKeyInput() const override {
        return false;
    }
    void raiseToolbarForCanvasInteraction() override {}
};

class InputCanvas final : public SnowCanvasWidget {
  public:
    int presses = 0;
    QPointF lastPress;

  protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress) {
            ++presses;
            lastPress = static_cast<QMouseEvent*>(event)->position();
            require(static_cast<QMouseEvent*>(event)->timestamp() == 1234,
                    "edge routing must preserve mouse timestamps");
        }
        return SnowCanvasWidget::event(event);
    }
};
} // namespace

void runScreenshotOverlayInputTests() {
    InputSink sink;
    auto* canvas = new InputCanvas;
    ScreenshotOverlayWindow overlay(sink, canvas);
    canvas->setInteractionEnabled(false);
    const auto mouse = [&](QEvent::Type type, const QPointF& point, Qt::MouseButton button,
                           Qt::MouseButtons buttons, bool throughWindow = true,
                           Qt::KeyboardModifiers keys = Qt::ShiftModifier) {
        const QPointF windowPoint = canvas->mapTo(&overlay, point);
        const QPointF global = canvas->mapToGlobal(point);
        QObject* receiver = throughWindow ? static_cast<QObject*>(overlay.windowHandle())
                                          : static_cast<QObject*>(&overlay);
        if (throughWindow && type == QEvent::MouseButtonDblClick) {
            // Qt delivers a flagged second press to QWidgetWindow to select the release receiver.
            QMouseEvent press(QEvent::MouseButtonPress, windowPoint, windowPoint, global, button,
                              buttons, keys, Qt::MouseEventSynthesizedBySystem);
            press.setTimestamp(1234);
            QMutableSinglePointEvent::setDoubleClick(&press, true);
            QApplication::sendEvent(receiver, &press);
        }
        QMouseEvent event(type, windowPoint, windowPoint, global, button, buttons, keys,
                          Qt::MouseEventSynthesizedBySystem);
        event.setTimestamp(1234);
        QApplication::sendEvent(receiver, &event);
    };
    QPointingDevice pen(QStringLiteral("edge tablet"), 95, QInputDevice::DeviceType::Stylus,
                        QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1,
                        3);
    for (const QRect display : {QRect(80, 60, 320, 240), QRect(-320, -240, 320, 240)}) {
        for (int surface = 0; surface < 2; ++surface) {
            overlay.setCaptureGeometry(display);
            overlay.restoreNativeSurface();
            overlay.show();
            QApplication::processEvents();
            require(canvas->size() == display.size() &&
                        canvas->mapToGlobal(QPoint()) == display.topLeft(),
                    "input routing must preserve the capture viewport and origin");
            // The last physical pixel occupies a fractional logical coordinate at scaled DPRs.
            for (const qreal inset : {1.0, 0.8, 2.0 / 3.0, 4.0 / 7.0, 0.5, 0.4, 0.01}) {
                for (const QPointF point : {QPointF(320 - inset, 120), QPointF(160, 240 - inset),
                                            QPointF(320 - inset, 240 - inset)}) {
                    sink.presses.clear();
                    sink.moves.clear();
                    sink.releases.clear();
                    const QPointF end = point - QPointF(20, 20);
                    mouse(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
                    require(sink.modifiers == Qt::ShiftModifier,
                            "selection input must retain mouse modifiers");
                    mouse(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
                    mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
                    require(sink.presses == QVector<QPointF>{point} &&
                                sink.moves == QVector<QPointF>{end} &&
                                sink.releases == QVector<QPointF>{end},
                            "an edge press must own the complete selection drag exactly once");
                    sink.moves.clear();
                    mouse(QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
                    require(sink.moves == QVector<QPointF>{point},
                            "edge hover must reach pointer presentation exactly once");
                    sink.rightClicks.clear();
                    sink.doubleClicks.clear();
                    mouse(QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton, true,
                          Qt::NoModifier);
                    mouse(QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton, true,
                          Qt::NoModifier);
                    mouse(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, true,
                          Qt::NoModifier);
                    mouse(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, true,
                          Qt::NoModifier);
                    mouse(QEvent::MouseButtonDblClick, point, Qt::LeftButton, Qt::LeftButton, true,
                          Qt::NoModifier);
                    mouse(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, true,
                          Qt::NoModifier);
                    require(sink.rightClicks == QVector<QPointF>{point},
                            "right clicks must reach the display edges");
                    require(sink.doubleClicks == QVector<QPointF>{point},
                            "completion double-clicks must reach the display edges");
                }
            }
            const QPointF edge(319.75, 239.75);
            const QPointF windowEdge = canvas->mapTo(&overlay, edge);
            sink.presses.clear();
            sink.moves.clear();
            sink.releases.clear();
            const QPointF outside(340, 260);
            mouse(QEvent::MouseButtonPress, edge, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
            require(sink.presses == QVector<QPointF>{edge} &&
                        sink.moves == QVector<QPointF>{outside} &&
                        sink.releases == QVector<QPointF>{outside},
                    "an edge drag must retain its implicit grab outside the canvas");
            const int wheels = sink.wheels;
            QWheelEvent wheel(windowEdge, canvas->mapToGlobal(edge), QPoint(0, 2), QPoint(0, 4),
                              Qt::NoButton, Qt::NoModifier, Qt::ScrollMomentum, false,
                              Qt::MouseEventSynthesizedBySystem);
            wheel.setTimestamp(1234);
            QApplication::sendEvent(overlay.windowHandle(), &wheel);
            require(sink.wheels == wheels + 1, "the edge must receive one wheel gesture");

            sink.presses.clear();
            sink.releases.clear();
            for (const auto type : {QEvent::TabletPress, QEvent::TabletRelease}) {
                QTabletEvent tablet(type, &pen, windowEdge, canvas->mapToGlobal(edge), 0.6, 4, 5, 0,
                                    0, 0, Qt::NoModifier, Qt::LeftButton,
                                    type == QEvent::TabletPress ? Qt::LeftButton : Qt::NoButton);
                QApplication::sendEvent(overlay.windowHandle(), &tablet);
            }
            require(sink.presses == QVector<QPointF>{edge} &&
                        sink.releases == QVector<QPointF>{edge},
                    "tablet edge clicks must use the same canvas coordinates as mouse clicks");
            require(sink.modifiers == Qt::NoModifier,
                    "tablet modifiers must reach selection input");
            const int recomputations = sink.modifierRecomputations;
            QKeyEvent controlPress(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
            QApplication::sendEvent(canvas, &controlPress);
            require(sink.modifiers.testFlag(Qt::ControlModifier) &&
                        sink.modifierRecomputations == recomputations + 1,
                    "Ctrl press requests a drag recomputation");
            QKeyEvent controlRelease(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
            QApplication::sendEvent(canvas, &controlRelease);
            require(!sink.modifiers.testFlag(Qt::ControlModifier) &&
                        sink.modifierRecomputations == recomputations + 2,
                    "Ctrl release clears the modifier even if Qt includes it in the release mask");
            const int clears = sink.snapClears;
            QFocusEvent focusLost(QEvent::FocusOut);
            QApplication::sendEvent(canvas, &focusLost);
            require(sink.snapClears > clears, "focus loss must clear transient snap feedback");

            sink.selecting = false;
            const int presses = canvas->presses;
            mouse(QEvent::MouseButtonPress, edge, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, edge, Qt::LeftButton, Qt::NoButton);
            require(canvas->presses == presses + 1 && canvas->lastPress == edge,
                    "an unhandled edge click must visit the disabled canvas only once");
            canvas->setInteractionEnabled(true);
            require(canvas->setCanvasTool(SnowCanvasTool::FreeDraw), "activate edge drawing");
            mouse(QEvent::MouseButtonPress, edge, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseMove, edge - QPointF(20, 20), Qt::NoButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, edge - QPointF(20, 20), Qt::LeftButton, Qt::NoButton);
            require(canvas->canvasHistoryState().canUndo && canvas->undo() &&
                        !canvas->canvasHistoryState().canUndo,
                    "drawing from an edge must commit one complete canvas gesture");
            canvas->setInteractionEnabled(false);
            sink.selecting = true;

            sink.inputAllowed = false;
            sink.requireGenuineInput = true;
            sink.presses.clear();
            // Qt's QWindow routing marks widget input spontaneous. Send directly
            // to the overlay to exercise genuinely synthetic fallback input.
            mouse(QEvent::MouseButtonPress, edge, Qt::LeftButton, Qt::LeftButton, false);
            mouse(QEvent::MouseButtonRelease, edge, Qt::LeftButton, Qt::NoButton, false);
            require(sink.presses.isEmpty(),
                    "synthetic edge input must respect startup suppression");
            QWindow* window = overlay.windowHandle();
            for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                QWindowSystemInterface::handleMouseEvent<
                    QWindowSystemInterface::SynchronousDelivery>(
                    window, QHighDpi::toNativeLocalPosition(windowEdge, window),
                    QHighDpi::toNativePixels(canvas->mapToGlobal(edge), window),
                    type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                    Qt::LeftButton, type);
            }
            require(sink.inputAllowed && sink.presses == QVector<QPointF>{edge},
                    "genuine edge input must retain its origin and resume startup input");
            sink.requireGenuineInput = false;
            overlay.releaseNativeSurface();
        }
    }

    namespace mouseOperations = snow_shot::mouse;
    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    const auto key = mouseOperations::definition(mouseOperations::Action::ShotCopy).key();
    const auto previous = store.value(key);
    const auto restore = qScopeGuard([&] { store.setValue(key, previous); });
    const auto keys = Qt::ControlModifier;
    require(store.setValue(
                key, mouseOperations::encode({mouseOperations::Kind::Click, Qt::LeftButton, keys})),
            "bind screenshot Copy to a modified left click");
    overlay.restoreNativeSurface();
    overlay.show();
    QApplication::processEvents();
    sink.deferSelection = true;
    sink.presses.clear();
    sink.moves.clear();
    sink.releases.clear();
    sink.doubleClicks.clear();
    const QPointF origin(80, 100);
    const QPointF end = origin + QPointF(QApplication::startDragDistance() + 2, 5);

    mouse(QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton, true, keys);
    require(sink.presses.isEmpty() && sink.doubleClicks.isEmpty(),
            "the binding defers selection and completion while the click is held");
    mouse(QEvent::MouseButtonRelease, origin, Qt::LeftButton, {}, true, Qt::NoModifier);
    require(sink.doubleClicks == QVector<QPointF>{origin} && sink.presses.isEmpty() &&
                sink.releases.isEmpty(),
            "only Copy runs when release remains below the threshold");

    for (const bool deliverMove : {true, false}) {
        sink.presses.clear();
        sink.moves.clear();
        sink.releases.clear();
        mouse(QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton, true, keys);
        if (deliverMove)
            mouse(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, true, Qt::NoModifier);
        mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, {}, true, Qt::NoModifier);
        require(sink.presses == QVector<QPointF>{origin} && sink.pressModifiers == keys &&
                    sink.moves == QVector<QPointF>{end} && sink.releases == QVector<QPointF>{end} &&
                    sink.modifiers == Qt::NoModifier && sink.doubleClicks.size() == 1,
                "a drag gets its original selection press and never invokes Copy");
    }
}
