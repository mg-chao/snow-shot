// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace pinned_action_benchmark {
using namespace pinned_mouse_operation_tests;

void report(const char* scenario, std::vector<qint64> samples) {
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](std::size_t p) {
        return static_cast<double>(samples[(samples.size() * p + 99) / 100 - 1]) / 1000.;
    };
    std::cout << "pin_action " << scenario << " samples=" << samples.size()
              << " p50_us=" << percentile(50) << " p95_us=" << percentile(95) << '\n';
}

void presentationAndFirstEdit() {
    auto& configStore = snow_shot::storage::ApplicationStorage::instance().configuration();
    require(configStore.setValues({{definition(Action::Thumbnail).key(), QJsonObject{}},
                                   {definition(Action::Annotation).key(),
                                    encode({Kind::DoubleClick, Qt::LeftButton})}}),
            "configure the measured annotation gesture");
    auto config = cachedOcrPinConfig(nullptr);
    config.recognitionResults = {};
    std::vector<qint64> fresh, prepared, edit;
    for (int sample = -4; sample < 60; ++sample) {
        const bool prewarm = sample % 2 == 0;
        std::unique_ptr<ScreenshotPinnedWindow> pin;
        if (prewarm) {
            pin = std::make_unique<ScreenshotPinnedWindow>();
            require(pin->prewarm(config.screen), "prewarm the measured shell");
        }
        QElapsedTimer clock;
        clock.start();
        if (!pin)
            pin = std::make_unique<ScreenshotPinnedWindow>();
        pin->setAttribute(Qt::WA_DeleteOnClose, false);
        qint64 firstFrame = 0;
        require(pin->present(config,
                             [&](bool ok, QImage) {
                                 require(ok, "publish the measured content frame");
                                 firstFrame = clock.nsecsElapsed();
                             }),
                "present the measured shell");
        while (!firstFrame && clock.elapsed() < 5000)
            QApplication::processEvents();
        require(firstFrame && pin->automationState().value(QStringLiteral("ready")).toBool(),
                "presentation sample includes the first content frame");
        QApplication::processEvents();
        auto* canvas = pin->findChild<SnowCanvasWidget*>();
        require(canvas && !pin->findChild<ScreenshotPinnedEditController*>(),
                "ordinary presentation keeps the editor lazy");
        clock.restart();
        doubleClick(*canvas, Qt::LeftButton);
        const auto activated = clock.nsecsElapsed();
        const auto* editor = pin->findChild<ScreenshotPinnedEditController*>();
        require(editor && editor->editMode() && editor->toolbarWindow() &&
                    editor->toolbarWindow()->isVisible() && editor->toolbarWindow()->palette(),
                "the measured gesture opens a usable palette");
        if (sample >= 0) {
            (prewarm ? prepared : fresh).push_back(firstFrame);
            edit.push_back(activated);
        }
        pin->close();
        pin.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
    }
    report("fresh_first_frame", fresh);
    report("prewarmed_first_frame", prepared);
    report("first_double_click", edit);
}

void pointerEvents(bool selected) {
    PinnedSelectionFixture fixture;
    auto& pin = fixture.add({60, 80}, {240, 180});
    auto& peer = fixture.add({390, 120}, {180, 120});
    // Exercise the native macOS input route with offscreen geometry.
    Access::installFailingPlatform(pin);
    Access::installFailingPlatform(peer);
    if (selected) {
        fixture.selection.toggleSelection(&peer);
        fixture.selection.toggleSelection(&pin);
    }
    auto* canvas = pin.findChild<SnowCanvasWidget*>();
    require(canvas, "benchmark delivers input to the pin canvas");
    std::vector<qint64> hover, wheelTimes, pinch;
    const auto measure = [](auto operation) {
        QElapsedTimer clock;
        clock.start();
        operation();
        return clock.nsecsElapsed();
    };
    const auto zoom = [canvas](double factor) {
        const QPointF local = canvas->rect().center();
        QNativeGestureEvent event(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(),
                                  2, local, local, canvas->mapToGlobal(local), factor - 1, {});
        QApplication::sendEvent(canvas, &event);
    };
    for (int frame = -8; frame < 240; ++frame) {
        const QPointF point = canvas->mapToGlobal(canvas->rect().center() + QPoint(frame % 3, 0));
        const auto moveTime =
            measure([&] { pointer(*canvas, QEvent::MouseMove, point, Qt::NoButton, {}, {}); });
        const auto wheelTime = measure([&] { wheel(*canvas, {0, 120}, {}); });
        require(Access::scale(pin) > 100 && (Access::scale(peer) > 100) == selected,
                "wheel dispatch scales precisely its intended targets");
        wheel(*canvas, {0, -120}, {});
        const auto pinchTime = measure([&] { zoom(1.035); });
        const auto pinched = Access::scale(pin);
        require(pinched > 100. && qAbs(pinched - qRound(pinched)) > .001 &&
                    qAbs(Access::scale(peer) - (selected ? pinched : 100.)) < .001,
                "native gesture dispatch applies fractional scaling to its targets");
        zoom(1. / 1.035);
        if (frame >= 0) {
            hover.push_back(moveTime);
            wheelTimes.push_back(wheelTime);
            pinch.push_back(pinchTime);
        }
        QApplication::processEvents();
    }
    report(selected ? "selected_hover" : "single_hover", hover);
    report(selected ? "selected_wheel" : "single_wheel", wheelTimes);
    report(selected ? "selected_pinch" : "single_pinch", pinch);
}

void run() {
    presentationAndFirstEdit();
    pointerEvents(false);
    pointerEvents(true);
}
} // namespace pinned_action_benchmark
