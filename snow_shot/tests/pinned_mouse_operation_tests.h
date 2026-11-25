// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace pinned_mouse_operation_tests {
using namespace snow_shot::mouse;
using Access = ScreenshotPinnedWindowTestAccess;

void pointer(QWidget& surface, QEvent::Type type, const QPointF& global, Qt::MouseButton button,
             Qt::MouseButtons held, Qt::KeyboardModifiers keys) {
    QMouseEvent event(type, surface.mapFromGlobal(global), global, button, held, keys);
    QApplication::sendEvent(&surface, &event);
}

void click(QWidget& surface, Qt::MouseButton button, Qt::KeyboardModifiers keys = {}) {
    const QPointF global = surface.mapToGlobal(surface.rect().center());
    pointer(surface, QEvent::MouseButtonPress, global, button, button, keys);
    pointer(surface, QEvent::MouseButtonRelease, global, button, {}, keys);
}

void doubleClick(QWidget& surface, Qt::MouseButton button) {
    const QPointF global = surface.mapToGlobal(surface.rect().center());
    click(surface, button);
    pointer(surface, QEvent::MouseButtonDblClick, global, button, button, {});
    pointer(surface, QEvent::MouseButtonRelease, global, button, {}, {});
}

void wheel(QWidget& surface, QPoint angleDelta, Qt::KeyboardModifiers keys,
           Qt::MouseButtons held = {}) {
    const QPointF local = surface.rect().center();
    QWheelEvent event(local, surface.mapToGlobal(local), {}, angleDelta, held, keys,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&surface, &event);
}

void nativeMoveKeepsPressAnchor() {
    ScreenshotPinnedWindow window;
    auto* platform = Access::installObservedPlatform(window);
    ScreenshotPinnedWindow::Config config;
    config.nativeGeometry = QRect(-350, 40, 160, 100);
    config.initialWindowSize = config.nativeGeometry.size();
    config.canvasSourceRect = QRectF(QPointF(), QSizeF(config.initialWindowSize));
    QImage image(config.initialWindowSize, QImage::Format_ARGB32);
    image.fill(Qt::white);
    config.imageSource = ScreenshotImageSource::fromImage(image, config.canvasSourceRect);
    Access::restoreOffscreen(window, config);
    const QRect original = platform->observed;
    const QPoint press = original.center();
    const QPoint delta(QApplication::startDragDistance() + 25, -9);
    Access::setPhysicalCursor(window, {true, [=] { return std::optional(press + delta); }, {}});
    require(!Access::startNativeMove(window, press) &&
                platform->systemMoveStart == original.translated(delta) &&
                platform->observed == original,
            "a rejected system move restores the press-time geometry for the drag fallback");
    platform->acceptSystemMove = true;
    require(Access::startNativeMove(window, press) &&
                platform->systemMoveStart == original.translated(delta),
            "the system takes over with the grabbed pixel still under the cursor");
    require(Access::finishNativeInteraction(window) &&
                Access::authority(window) == original.translated(delta),
            "releasing without another move keeps the initial displacement");
}

void annotationOnFreshPin() {
    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    const auto annotation = definition(Action::Annotation).key();
    const auto thumbnail = definition(Action::Thumbnail).key();
    const auto restore = qScopeGuard([&, original = store.snapshot()] {
        store.setValues(
            {{annotation, original.value(annotation)}, {thumbnail, original.value(thumbnail)}});
    });
    require(store.setValues({{thumbnail, QJsonObject{}},
                             {annotation, encode({Kind::DoubleClick, Qt::LeftButton})}}),
            "bind annotation mode to left-button double-click");

    PinnedSelectionFixture fixture;
    for (const bool editingEnabled : {true, false}) {
        auto& pin = fixture.add({60, 80}, {240, 180}, [editingEnabled](auto& config) {
            config.enableEditing = editingEnabled;
        });
        auto* canvas = pin.findChild<SnowCanvasWidget*>();
        require(canvas && !pin.findChild<ScreenshotPinnedEditController*>(),
                "a fresh pin starts with a canvas and a deferred annotation controller");
        doubleClick(*canvas, Qt::LeftButton);
        auto* edit = pin.findChild<ScreenshotPinnedEditController*>();
        if (editingEnabled) {
            require(edit && edit->editMode(), "the first double-click enters annotation mode");
            doubleClick(*canvas, Qt::LeftButton);
            require(!edit->editMode(), "the next double-click exits annotation mode");
        } else {
            require(!edit, "a pin with editing disabled ignores the annotation gesture");
        }
    }
}

void doubleClickModifiers() {
    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    const auto menu = definition(Action::Menu).key();
    const auto multiSelect = definition(Action::MultiSelect).key();
    const auto thumbnail = definition(Action::Thumbnail).key();
    const auto original = store.snapshot();
    require(store.setValues(
                {{menu, QJsonObject{}},
                 {multiSelect, encode({Kind::Click, Qt::RightButton})},
                 {thumbnail, encode({Kind::DoubleClick, Qt::RightButton, Qt::AltModifier})}}),
            "assign one button to separate single and double-click actions");

    PinnedSelectionFixture fixture;
    auto& window = fixture.add({60, 80});
    const QPointF global = window.mapToGlobal(window.rect().center());
    click(window, Qt::RightButton);
    require(fixture.selection.isSelected(&window), "first press is an unmodified single click");
    pointer(window, QEvent::MouseButtonDblClick, global, Qt::RightButton, Qt::RightButton,
            Qt::AltModifier);
    pointer(window, QEvent::MouseButtonRelease, global, Qt::RightButton, {}, Qt::AltModifier);
    require(!window.persistenceSnapshot().thumbnailMode,
            "different modifiers never combine into a double-click action");

    click(window, Qt::RightButton, Qt::AltModifier);
    pointer(window, QEvent::MouseButtonDblClick, global, Qt::RightButton, Qt::RightButton,
            Qt::AltModifier);
    pointer(window, QEvent::MouseButtonRelease, global, Qt::RightButton, {}, Qt::AltModifier);
    require(window.persistenceSnapshot().thumbnailMode,
            "two clicks with matching modifiers trigger the double-click action");
    require(store.setValues({{menu, original.value(menu)},
                             {multiSelect, original.value(multiSelect)},
                             {thumbnail, original.value(thumbnail)}}),
            "restore the mouse bindings after double-click tests");
}

void textAndHandleBindings() {
    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    require(store.setValues(
                {{definition(Action::ResetZoom).key(), QJsonObject{}},
                 {definition(Action::Menu).key(), encode({Kind::Click, Qt::ForwardButton})}}),
            "assign the menu to side button 2");
    PinnedSelectionFixture fixture;
    const auto text = [](const QString& value) {
        return [value](ScreenshotPinnedWindow::Config& config) {
            config.formattedTextDocument = std::make_shared<QTextDocument>();
            config.formattedTextDocument->setPlainText(value);
            config.formattedPlainText = value;
            config.originalClipboardContent.text = value;
        };
    };
    auto& a = fixture.add({40, 60}, {200, 140}, text(QStringLiteral("First")));
    auto& b = fixture.add({260, 70}, {200, 140}, text(QStringLiteral("Second")));
    auto& c = fixture.add({40, 250}, {210, 160}, text(QStringLiteral("Third")));
    auto& empty = fixture.add({280, 250});
    fixture.selection.toggleSelection(&b);
    fixture.selection.toggleSelection(&a);
    click(a, Qt::RightButton, Qt::ShiftModifier);
    require(QApplication::clipboard()->text() == QStringLiteral("Second\nFirst"),
            "copy text uses selection order");
    click(c, Qt::RightButton, Qt::ShiftModifier);
    require(QApplication::clipboard()->text() == QStringLiteral("Third"),
            "unselected text copy targets only the clicked window");
    click(empty, Qt::RightButton, Qt::ShiftModifier);
    require(QApplication::clipboard()->text() == QStringLiteral("Third"),
            "unavailable text preserves clipboard contents");
    const auto copyText = definition(Action::CopyText).key();
    const auto menu = definition(Action::Menu).key();
    const auto thumbnail = definition(Action::Thumbnail).key();
    const auto original = store.snapshot();
    const auto leftDouble = encode({Kind::DoubleClick, Qt::LeftButton});
    require(store.setValues({{thumbnail, QJsonObject{}}, {copyText, leftDouble}}),
            "bind Copy text to an unmodified left double-click");
    doubleClick(a, Qt::LeftButton);
    require(QApplication::clipboard()->text() == QStringLiteral("Second\nFirst") &&
                fixture.selection.selectedCount() == 2,
            "both releases of a double-click preserve the selected copy targets");
    require(store.setValues({{copyText, original.value(copyText)}, {menu, leftDouble}}),
            "bind Menu to an unmodified left double-click");
    doubleClick(a, Qt::LeftButton);
    auto* groupMenu = a.findChild<adqt::widgets::AdContextMenu*>(
        QStringLiteral("screenshotPinnedMultiSelectionMenu"));
    require(groupMenu && groupMenu->isPopupVisible(),
            "a double-click opens the multi-selection menu");
    groupMenu->hide();
    click(a, Qt::LeftButton);
    translation_tests::waitUntil([&] { return fixture.selection.selectedCount() == 0; },
                                 "a lone left click still clears the selection");
    require(store.setValues({{menu, original.value(menu)}, {thumbnail, original.value(thumbnail)}}),
            "restore the single-click menu and thumbnail bindings");
    click(empty, Qt::RightButton);
    require(!Access::contextMenu(empty)->isPopupVisible(), "the old menu binding is released");
    click(empty, Qt::ForwardButton);
    require(Access::contextMenu(empty)->isPopupVisible(),
            "a side-button click opens the window menu");
    Access::contextMenu(empty)->hide();
    auto& hide = Access::hideToTop(empty);
    require(hide.enter(screenshot_pinned_hide_to_top::screenGeometry(empty.screen())),
            "enter hide-at-top");
    hide.animation().setCurrentTime(250);
    require(hide.handleWidget()->isVisible(), "hidden window provides a visible handle");
    click(*hide.handleWidget(), Qt::ForwardButton);
    require(Access::contextMenu(empty)->isPopupVisible(),
            "the handle resolves the same menu binding while its window is hidden");
    Access::contextMenu(empty)->hide();
}

void recognitionBindings() {
    auto config = cachedOcrPinConfig(nullptr);
    snow_shot::presentation::PinnedWindowSelectionController selection;
    ScreenshotPinnedWindow window;
    const auto close = qScopeGuard([&] { window.close(); });
    auto* session = Access::hiddenSelectionOffscreen(window, config);
    Access::attachSelection(window, selection);
    session->activate(ScreenshotRecognitionSessionController::Mode::Text);
    waitForUi(20);
    auto* content = window.findChild<ScreenshotRecognitionWindow*>();
    require(content && content->isVisible(), "pin displays cached OCR results");
    auto* layer = content->findChild<QGraphicsView*>(QStringLiteral("snowShotOcrTextLayer"));
    require(layer && !ocrGlyphItems(layer).isEmpty(), "recognition text is rendered");
    const QRectF glyph = ocrGlyphItems(layer).front()->sceneBoundingRect();
    const QPoint text = layer->viewport()->mapTo(
        content,
        layer->mapFromScene(QPointF(glyph.left() + glyph.width() / 4, glyph.center().y())));
    require(!content->isOcrBackgroundAt(text), "press targets selectable OCR text");
    const QPointF origin = content->mapToGlobal(text);
    auto& configStore = snow_shot::storage::ApplicationStorage::instance().configuration();
    const auto copyKey = definition(Action::Copy).key();
    const auto restoreCopy =
        qScopeGuard([&, old = configStore.value(copyKey)] { configStore.setValue(copyKey, old); });
    require(
        configStore.setValue(copyKey, encode({Kind::Click, Qt::MiddleButton, Qt::ShiftModifier})),
        "bind regular Copy to Shift+middle");
    QApplication::clipboard()->setText(QStringLiteral("previous clipboard"));
    pointer(*content, QEvent::MouseButtonPress, origin, Qt::MiddleButton, Qt::MiddleButton,
            Qt::ShiftModifier);
    pointer(*content, QEvent::MouseButtonRelease, origin, Qt::MiddleButton, {}, Qt::ShiftModifier);
    require(QApplication::clipboard()->text() == QStringLiteral("Saved OCR"),
            "the configured click copies recognition content over a selectable glyph");

    pointer(*content, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton,
            Qt::ControlModifier);
    pointer(*content, QEvent::MouseButtonRelease, origin, Qt::LeftButton, {}, Qt::ControlModifier);
    require(selection.isSelected(&window), "the default Multi-select click works over OCR glyphs");

    pointer(*content, QEvent::MouseButtonPress, origin, Qt::LeftButton, Qt::LeftButton,
            Qt::ControlModifier);
    const QPointF end = origin + QPointF(QApplication::startDragDistance() + 24, 0);
    pointer(*content, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    pointer(*content, QEvent::MouseButtonRelease, end, Qt::LeftButton, {}, Qt::ControlModifier);
    require(selection.isSelected(&window) && content->textSelectionClipboardMimeData(),
            "dragging hands the original press to OCR selection without toggling the pin");

    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    const auto menu = definition(Action::Menu).key();
    const auto multiSelect = definition(Action::MultiSelect).key();
    const auto restore = qScopeGuard([&, old = store.snapshot()] {
        store.setValues({{menu, old.value(menu)}, {multiSelect, old.value(multiSelect)}});
    });
    require(store.setValues({{menu, encode({Kind::Click, Qt::ForwardButton})},
                             {multiSelect, encode({Kind::Click, Qt::RightButton})}}),
            "move the menu and bind right-click to Multi-select");
    for (const bool contextOnPress : {true, false}) {
        const bool wasSelected = selection.isSelected(&window);
        const auto sendContext = [&] {
            QContextMenuEvent context(QContextMenuEvent::Mouse, text, origin.toPoint());
            context.ignore();
            require(QApplication::sendEvent(content, &context) && context.isAccepted(),
                    "the pin suppresses the native mouse context event");
        };
        pointer(*content, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, {});
        if (contextOnPress)
            sendContext();
        pointer(*content, QEvent::MouseButtonRelease, origin, Qt::RightButton, {}, {});
        if (!contextOnPress)
            sendContext();
        require(selection.isSelected(&window) != wasSelected && session->active() &&
                    !QApplication::activePopupWidget(),
                "the rebound right-click executes through OCR without opening its native menu");
    }
}

void failedCopyKeepsSource() {
    const snow_shot::storage::ScreenshotSettings settings;
    require(settings.setCopyImageFileToClipboard(true) &&
                settings.setAutoSaveFilenameFormat(emptyExportFilenameFormat()),
            "configure a file copy with no usable filename");
    PinnedSelectionFixture fixture;
    auto& window = fixture.add({60, 80});
    Access::editSelectionOffscreen(window, true);
    auto* edit = window.findChild<ScreenshotPinnedEditController*>();
    auto* canvas = window.findChild<SnowCanvasWidget*>();
    require(edit && edit->editMode() && canvas, "enter annotation mode before failed copy");
    QApplication::clipboard()->setText(QStringLiteral("previous clipboard"));
    click(*canvas, Qt::MiddleButton);
    translation_tests::waitUntil([&] { return !Access::exportArtifact(window); },
                                 "failed clipboard file export settles");
    QCoreApplication::processEvents();
    require(window.isVisible() && edit->editMode() &&
                QApplication::clipboard()->text() == QStringLiteral("previous clipboard"),
            "failed copy preserves the annotation mode, window and clipboard");
}

void wheelCancelsCropGestures() {
    PinnedSelectionFixture fixture;
    auto& window = fixture.add({60, 80}, {240, 180});
    auto* canvas = window.findChild<SnowCanvasWidget*>();
    require(canvas, "pin canvas exists for crop gesture testing");
    const QPointF origin = canvas->mapToGlobal(canvas->rect().center());
    const QPointF end = origin + QPointF(QApplication::startDragDistance() + 15, 25);
    const double initialScale = Access::scale(window);
    pointer(*canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, {});
    pointer(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::RightButton, {});
    require(Access::cropSelecting(window), "right drag starts a crop");
    wheel(*canvas, {0, 120}, {}, Qt::RightButton);
    require(!Access::cropSelecting(window) && Access::scale(window) > initialScale,
            "wheel zoom cancels the crop before applying the new scale");
    pointer(*canvas, QEvent::MouseButtonRelease, end, Qt::RightButton, {}, {});
    require(!window.persistenceSnapshot().cropRect,
            "releasing a canceled crop does not commit its rectangle");

    const QPointF nextOrigin = canvas->mapToGlobal(canvas->rect().center());
    const QPointF nextEnd = nextOrigin + (end - origin);
    pointer(*canvas, QEvent::MouseButtonPress, nextOrigin, Qt::RightButton, Qt::RightButton, {});
    wheel(*canvas, {0, 120}, {}, Qt::RightButton);
    pointer(*canvas, QEvent::MouseMove, nextEnd, Qt::NoButton, Qt::RightButton, {});
    require(!Access::cropSelecting(window), "wheel also discards the pending crop origin");
    pointer(*canvas, QEvent::MouseButtonRelease, nextEnd, Qt::RightButton, {}, {});

    require(Access::crop(window, {55, 40, 110, 90}), "set an interior crop before panning");
    const auto crop = window.persistenceSnapshot().cropRect;
    const double croppedScale = Access::scale(window);
    const QPointF center = canvas->mapToGlobal(canvas->rect().center());
    const QPointF panned = center + QPointF(QApplication::startDragDistance() + 8, 4);
    pointer(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton,
            Qt::ShiftModifier);
    pointer(*canvas, QEvent::MouseMove, panned, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    require(window.persistenceSnapshot().cropRect != crop, "the drag pans the cropped image");
    wheel(*canvas, {0, 120}, {}, Qt::LeftButton);
    pointer(*canvas, QEvent::MouseButtonRelease, panned, Qt::LeftButton, {}, {});
    require(window.persistenceSnapshot().cropRect == crop && Access::scale(window) > croppedScale,
            "wheel zoom cancels the pan and keeps the original crop");
}

void windowManagementBindings() {
    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    const snow_shot::storage::PinToScreenShortcutSettings shortcuts;
    const struct {
        Action action;
        Qt::MouseButton button;
        Qt::KeyboardModifiers modifiers;
        Qt::Key key;
    } commands[] = {
        {Action::ShowAllWindows, Qt::BackButton, Qt::ShiftModifier, Qt::Key_F6},
        {Action::HideOtherWindows, Qt::ForwardButton, Qt::ShiftModifier, Qt::Key_F7},
        {Action::CloseOtherWindows, Qt::BackButton, Qt::AltModifier, Qt::Key_F8},
        {Action::CloseAllWindows, Qt::ForwardButton, Qt::AltModifier, Qt::Key_F9},
    };
    for (const auto& cmd : commands) {
        const auto& action = definition(cmd.action);
        require(
            shortcuts.setShortcuts(QString::fromLatin1(action.id),
                                   {QKeySequence(cmd.key).toString(QKeySequence::PortableText)}) &&
                store.setValue(action.key(), encode({Kind::Click, cmd.button, cmd.modifiers})),
            "configure both inputs for a window management command");
    }
    require(
        store.setValues(
            {{definition(Action::ClickThrough).key(), encode({Kind::DoubleClick, Qt::BackButton})},
             {definition(Action::Lock).key(), encode({Kind::Click, Qt::ForwardButton})}}),
        "configure click-through and lock gestures");

    for (bool keyboard : {false, true}) {
        PinnedSelectionFixture fixture;
        auto& pin = fixture.add({60, 80});
        auto& other = fixture.add({260, 80});
        const auto groupId = fixture.groups.createGroup(QStringLiteral("Parked"));
        require(groupId.has_value(), "create an inactive group");
        auto& parked = fixture.add({450, 80});
        parked.setGroupId(*groupId);
        const auto invoke = [keyboard, &other](ScreenshotPinnedWindow& target, const auto& cmd) {
            if (keyboard) {
                sendShortcut(target, cmd.key);
                if (cmd.action == Action::CloseOtherWindows)
                    require(other.isVisible(),
                            "close others keeps its target visible until release");
                if (cmd.action == Action::CloseAllWindows)
                    require(target.isVisible(),
                            "close all keeps the invoking pin visible until release");
                PhysicalKeyEvent release(QEvent::KeyRelease, cmd.key, Qt::NoModifier);
                QCoreApplication::sendEvent(&target, &release);
            } else {
                click(target, cmd.button, cmd.modifiers);
            }
        };
        {
            ScreenshotPinnedWindow live;
            int visibilityRequests = 0;
            auto config =
                live_pinned_tests::config(std::make_shared<live_pinned_tests::FakeSource>());
            config.showAllWindowsRequested = [&] { ++visibilityRequests; };
            config.hideOtherWindowsRequested = [&](const QString&) { ++visibilityRequests; };
            live_pinned_tests::present(live, config);
            live.activateWindow();
            live.setFocus();
            waitForUi(10);
            for (const auto& cmd : commands)
                invoke(live, cmd);
            require(visibilityRequests == 0 && live.isVisible() && pin.isVisible() &&
                        other.isVisible(),
                    "live pin input cannot invoke the omitted management commands");
            live.closeLiveWindow();
        }
        pin.activateWindow();
        pin.setFocus();
        waitForUi(10);
        int shows = 0;
        QString exceptId;
        QObject::connect(
            &fixture.groups,
            &snow_shot::presentation::PinnedWindowGroupManager::showAllWindowsRequested, &pin,
            [&] { ++shows; });
        QObject::connect(
            &fixture.groups,
            &snow_shot::presentation::PinnedWindowGroupManager::hideOtherWindowsRequested, &pin,
            [&](const QString& id) { exceptId = id; });
        invoke(pin, commands[0]);
        invoke(pin, commands[1]);
        require(shows == 1 && exceptId == pin.persistenceId(),
                "show all and hide others reach the group manager with the invoking pin");
        if (!keyboard) {
            click(pin, Qt::ForwardButton);
            require(pin.persistenceSnapshot().lockedMode && !other.persistenceSnapshot().lockedMode,
                    "the lock gesture targets only its pin");
            doubleClick(pin, Qt::BackButton);
            auto* exit = Access::clickThroughExitButton(pin);
            require(Access::clickThroughActive(pin) && exit && exit->isVisible(),
                    "the click-through gesture exposes the exit button");
            click(*exit, Qt::LeftButton);
            click(pin, Qt::ForwardButton);
            require(!Access::clickThroughActive(pin) && !pin.persistenceSnapshot().lockedMode,
                    "the exit button restores pointer input for unlocking");
        }
        invoke(pin, commands[2]);
        require(pin.isVisible() && !other.isVisible(), "close others keeps the invoking pin open");
        invoke(pin, commands[3]);
        require(!pin.isVisible() && parked.isVisible(),
                "close all includes the invoking pin and preserves other groups");
    }
    for (const auto& cmd : commands) {
        const auto& action = definition(cmd.action);
        require(shortcuts.setShortcuts(QString::fromLatin1(action.id), {}) &&
                    store.setValue(action.key(), QJsonObject{}),
                "clear management bindings");
    }
    require(store.setValues({{definition(Action::ClickThrough).key(), QJsonObject{}},
                             {definition(Action::Lock).key(), QJsonObject{}}}),
            "clear mode gestures");
}

void run() {
    windowManagementBindings();
    annotationOnFreshPin();
    wheelCancelsCropGestures();
    auto& config = snow_shot::storage::ApplicationStorage::instance().configuration();
    require(config.setValues(
                {{definition(Action::MultiSelect).key(), encode({Kind::Click, Qt::BackButton})},
                 {definition(Action::ResetZoom).key(), encode({Kind::Click, Qt::ForwardButton})},
                 {definition(Action::PanCrop).key(), encode({Kind::Drag, Qt::ForwardButton})},
                 {definition(Action::FineOpacity).key(),
                  encode({Kind::Wheel, Qt::NoButton, Qt::ShiftModifier})},
                 {definition(Action::CopyClose).key(), encode({Kind::Click, Qt::MiddleButton})}}),
            "install independent pin bindings");

    PinnedSelectionFixture fixture;
    ScreenshotPinnedWindow::Config sourceConfig;
    auto& first = fixture.add({40, 60}, {240, 180}, [&](auto& config) {
        auto source = config.imageSource.materializedImage;
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixelColor(x, y, QColor(x, y, 50));
        config.imageSource = ScreenshotImageSource::fromImage(source, config.canvasSourceRect);
        sourceConfig = config;
    });
    auto& second = fixture.add({390, 100}, {150, 100});
    auto* canvas = first.findChild<SnowCanvasWidget*>();
    require(canvas, "pin canvas exists");
    click(*canvas, Qt::BackButton, Qt::ControlModifier);
    require(!fixture.selection.isSelected(&first), "extra modifiers do not select a pin");
    click(*canvas, Qt::BackButton);
    click(second, Qt::BackButton);
    require(fixture.selection.selectedCount() == 2, "rebound side clicks toggle pins");
#ifdef Q_OS_WIN
    // Qt's Windows plugin puts Alt+vertical wheel notches on the X axis.
    const QPoint modifiedNotch(120, 0);
#else
    const QPoint modifiedNotch(0, 120);
#endif
    auto* peerCanvas = second.findChild<SnowCanvasWidget*>();
    require(peerCanvas, "the selected peer has a canvas");
    const auto firstZoom = canvas->canvasToViewTransform().m11();
    const auto secondZoom = peerCanvas->canvasToViewTransform().m11();
    wheel(*canvas, modifiedNotch, Qt::AltModifier);
    require(Access::scale(first) == 101 && Access::scale(second) == 101 &&
                canvas->canvasToViewTransform().m11() > firstZoom &&
                peerCanvas->canvasToViewTransform().m11() > secondZoom,
            "a fine wheel step commits both selected viewports before returning");
    Access::setGeneralOpacity(first, 85);
    wheel(*canvas, {0, -120}, Qt::ShiftModifier);
    require(Access::opacity(first) == 84 && Access::opacity(second) == 100,
            "fine opacity changes only the target");
    wheel(*canvas, {0, 120}, Qt::ControlModifier | Qt::ShiftModifier);
    require(Access::opacity(first) == 84 && Access::scale(first) == 101,
            "unassigned modifier combination is inert");
    fixture.selection.clearSelection();

    Access::scaleBorderFixture(first, 150);
    wheel(*canvas, modifiedNotch, Qt::AltModifier);
    require(Access::scale(first) == 151, "fine zoom advances one percentage point from 150");
    require(config.setValues(
                {{definition(Action::FineOpacity).key(),
                  encode({Kind::Wheel, Qt::NoButton, Qt::ControlModifier | Qt::AltModifier})},
                 {definition(Action::FineZoom).key(),
                  encode({Kind::Wheel, Qt::NoButton, Qt::ShiftModifier})}}),
            "rebind both fine wheel actions on a visible pin");
    wheel(*canvas, -modifiedNotch / 2, Qt::ControlModifier | Qt::AltModifier);
    require(Access::opacity(first) == 84, "fine opacity waits for a complete notch");
    wheel(*canvas, -modifiedNotch / 2, Qt::ControlModifier | Qt::AltModifier);
    require(Access::opacity(first) == 83 && Access::scale(first) == 151,
            "two half notches with Ctrl+Alt change only opacity by one point");
    wheel(*canvas, {0, -240}, Qt::ShiftModifier);
    require(Access::scale(first) == 149, "rebound fine zoom applies both downward notches");
    wheel(*canvas, modifiedNotch * 2, Qt::ShiftModifier);
    require(Access::scale(first) == 151, "modified scrolling restores both fine zoom steps");
    wheel(*canvas, {-120, 0}, Qt::NoModifier);
    require(Access::scale(first) == 151, "an ordinary horizontal scroll leaves zoom unchanged");
    const auto fullViewport = Access::visibleCrop(first);
    require(Access::crop(first, {55, 40, 110, 90}), "create an interior crop");
    Access::contextMenu(first);
    auto* cancelCrop = Access::cancelCropAction(first);
    require(cancelCrop && cancelCrop->isEnabled(), "Cancel crop is available before panning");
    const auto before = first.persistenceSnapshot();
    const auto originalPixels =
        pinned_crop_tests::exportImage(first, Access::viewportExport(first));
    const QRect frame = first.currentNativeGeometry();
    const QPointF origin = canvas->mapToGlobal(canvas->rect().center());
    const QPointF delta(24, 3);
    pointer(*canvas, QEvent::MouseButtonPress, origin, Qt::ForwardButton, Qt::ForwardButton, {});
    cancelCrop->trigger();
    require(first.persistenceSnapshot().cropRect == before.cropRect,
            "a pending pan keeps the crop when the menu action is triggered");
    pointer(*canvas, QEvent::MouseMove, origin + delta, Qt::NoButton, Qt::ForwardButton,
            Qt::AltModifier);
    pointer(*canvas, QEvent::MouseButtonRelease, origin + delta, Qt::ForwardButton, {},
            Qt::AltModifier);
    auto after = first.persistenceSnapshot();
    require(after.cropRect && before.cropRect && after.cropRect->x() < before.cropRect->x() &&
                after.cropRect->size() == before.cropRect->size() &&
                first.currentNativeGeometry() == frame && Access::scale(first) == 151,
            "panning changes persisted source coordinates and consumes the click");
    const auto pixels = pinned_crop_tests::exportImage(first, Access::viewportExport(first));
    require(pixels != originalPixels &&
                pixels == pinnedExportSnapshotPixels(Access::fileSave(first), first),
            "copy and save render the changed source pixels");
    Access::persistNow(first);
    require(fixture.repository.flush().success, "flush the finished pan to disk");
    const auto saved = fixture.repository.loadRecord(first.persistenceId());
    require(saved && saved->cropRect == after.cropRect, "persist the source region on release");
    sourceConfig.restorePersistentState = true;
    sourceConfig.persistedCropRect = saved->cropRect;
    sourceConfig.persistedOpacityPercent = saved->opacityPercent;
    sourceConfig.persistedExactInitialWindowSize = saved->exactInitialWindowSize;
    sourceConfig.initialWindowSize = saved->initialWindowSize;
    sourceConfig.imageSource =
        ScreenshotImageSource::fromImage(saved->image, saved->canvasSourceRect);
    sourceConfig.persistedScalePercent = saved->scalePercent;
    sourceConfig.nativeGeometry = saved->nativeGeometry;
    ScreenshotPinnedWindow reopened;
    reopened.setAttribute(Qt::WA_DeleteOnClose, false);
    require(reopened.present(sourceConfig), "reopen the panned viewport");
    translation_tests::waitUntil([&] { return Access::shakeContentReady(reopened); },
                                 "reopened crop ready");
    const auto reopenedPixels =
        pinned_crop_tests::exportImage(reopened, Access::viewportExport(reopened));
    require(pixels.size() == reopenedPixels.size() &&
                Access::visibleCrop(first) == Access::visibleCrop(reopened),
            "reopening preserves the extent and source coordinates of the pan");
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            require(pixels.pixel(x, y) == reopenedPixels.pixel(x, y),
                    "reopened crop pixels match the copied viewport");
    reopened.close();
    pointer(*canvas, QEvent::MouseButtonPress, origin, Qt::ForwardButton, Qt::ForwardButton, {});
    pointer(*canvas, QEvent::MouseButtonRelease, origin + QPointF(900, 900), Qt::ForwardButton, {},
            {});
    const auto edge = first.persistenceSnapshot().cropRect;
    require(edge && edge->topLeft() == QPointF() && edge->size() == before.cropRect->size() &&
                first.currentNativeGeometry() == frame,
            "crop panning clamps both axes without moving the frame");
    click(*canvas, Qt::ForwardButton);
    require(Access::scale(first) == 100, "release below threshold executes only reset zoom");
    pinned_crop_tests::cancelThroughMenu(first);
    require(!first.persistenceSnapshot().cropRect && Access::visibleCrop(first) == fullViewport &&
                Access::scale(first) == 100,
            "canceling a panned crop restores the full viewport at the current zoom");

    const snow_shot::storage::PinToScreenSettings settings;
    require(settings.setExitAnnotationModeBeforeClosing(true) &&
                settings.setConfirmBeforeClosingWindow(false),
            "enable normal annotation close behavior");
    Access::editSelectionOffscreen(second, true);
    auto* edit = second.findChild<ScreenshotPinnedEditController*>();
    require(edit && edit->editMode(), "enter annotation mode");
    auto* secondCanvas = second.findChild<SnowCanvasWidget*>();
    click(*secondCanvas, Qt::MiddleButton);
    translation_tests::waitUntil([&] { return !edit->editMode(); },
                                 "successful copy exits annotation mode");
    require(second.isVisible() && !exportedClipboardImage().isNull(),
            "one normal close leaves the annotated pin visible");
    click(first, Qt::MiddleButton);
    Access::copyCurrentViewport(second);
    translation_tests::waitUntil(
        [&] { return !Access::exportArtifact(first) && !Access::exportArtifact(second); },
        "superseded copy settles");
    require(first.isVisible(), "a superseded copy does not close the source");
    nativeMoveKeepsPressAnchor();
    failedCopyKeepsSource();
    textAndHandleBindings();
    doubleClickModifiers();
    require(config.setValues(
                {{definition(Action::MultiSelect).key(),
                  encode(definition(Action::MultiSelect).factory)}}),
            "restore the default Multi-select binding before testing OCR input");
    recognitionBindings();
}
} // namespace pinned_mouse_operation_tests
