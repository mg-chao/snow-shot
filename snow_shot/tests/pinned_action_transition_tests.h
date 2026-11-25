// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace pinned_action_transition_tests {
using namespace pinned_mouse_operation_tests;

QAction& menuAction(ScreenshotPinnedWindow& pin, const char* name) {
    materializePinnedMenuTree(pin);
    auto* action = pin.findChild<QAction*>(QString::fromLatin1(name));
    require(action, "the pinned menu exposes the action");
    return *action;
}

QVariantAnimation& animation(ScreenshotPinnedWindow& pin) {
    auto* animation =
        pin.findChild<QVariantAnimation*>(QStringLiteral("screenshotPinnedGeometryAnimation"));
    require(animation && animation->state() == QAbstractAnimation::Running,
            "the interactive thumbnail transition is running");
    return *animation;
}

void finishTransition(ScreenshotPinnedWindow& pin) {
    auto& transition = animation(pin);
    transition.setCurrentTime(transition.duration());
}

bool editing(const ScreenshotPinnedWindow& pin) {
    return pin.automationState().value(QStringLiteral("editing")).toBool();
}

void setEditing(ScreenshotPinnedWindow& pin, bool enabled) {
    QString error;
    const auto reply = pin.automationEdit(QStringLiteral("editing"),
                                          {{QStringLiteral("enabled"), enabled}}, &error);
    require(error.isEmpty() && reply.contains(QStringLiteral("editing")) &&
                reply.value(QStringLiteral("editing")).toBool() == enabled &&
                editing(pin) == enabled,
            "a successful editing reply reports the committed window state");
}

void setThumbnail(ScreenshotPinnedWindow& pin, bool enabled) {
    QString error;
    require(pin.automationUpdate({{QStringLiteral("thumbnail"), enabled}}, &error) &&
                pin.persistenceSnapshot().thumbnailMode == enabled,
            "automation settles the requested thumbnail state");
}

void initialAnnotation() {
    enum class Request { Disabled, Cancel, Enable };
    for (const auto request : {Request::Disabled, Request::Cancel, Request::Enable}) {
        ScreenshotImageLoadCallback deliver;
        auto config = cachedOcrPinConfig(nullptr);
        config.initialAnnotationMode = true;
        config.enableEditing = request != Request::Disabled;
        config.imageLoader = [&](QObject*, ScreenshotImageLoadCallback callback) {
            deliver = std::move(callback);
        };
        ScreenshotPinnedWindow pin;
        pin.setAttribute(Qt::WA_DeleteOnClose, false);
        require(pin.present(config) && deliver, "present a shell with content still loading");
        auto& drawing = menuAction(pin, "screenshotPinnedDrawingAction");
        require(drawing.isChecked() == config.enableEditing &&
                    drawing.isEnabled() == config.enableEditing &&
                    !pin.findChild<ScreenshotPinnedEditController*>(),
                "the shell exposes an eligible initial request without creating its editor");
        QString error;
        const auto reply = pin.automationEdit(QStringLiteral("editing"),
                                              {{QStringLiteral("enabled"), true}}, &error);
        require(reply.isEmpty() && error == QStringLiteral("not_ready"),
                "MCP keeps the first-frame gate");
        if (request == Request::Cancel)
            drawing.trigger();
        deliver(config.imageSource.materializedImage);
        translation_tests::waitUntil(
            [&] {
                return pin.automationState().value(QStringLiteral("ready")).toBool() &&
                       !Access::initialAnnotationPending(pin);
            },
            "the deferred annotation option settles");
        require(editing(pin) == (request == Request::Enable) && drawing.isChecked() == editing(pin),
                "only a still-requested, supported initial edit opens the palette");
    }
}

void annotationIntents() {
    PinnedSelectionFixture fixture;
    auto& disabled =
        fixture.add({360, 100}, {220, 160}, [](auto& config) { config.enableEditing = false; });
    auto& drawing = menuAction(disabled, "screenshotPinnedDrawingAction");
    require(!drawing.isEnabled(), "a disabled annotation capability is reflected in the menu");
    QString error;
    const auto rejected = disabled.automationEdit(
        QStringLiteral("tool"), {{QStringLiteral("tool"), QStringLiteral("rectangle")}}, &error);
    require(rejected.isEmpty() && error == QStringLiteral("action_unavailable") &&
                !disabled.findChild<ScreenshotPinnedEditController*>(),
            "a tool request cannot enable editing on a disabled pin");
    auto& pin = fixture.add({60, 80}, {240, 180}, [](auto& config) {
        config.recognitionResults = cachedOcrPinConfig(nullptr).recognitionResults;
    });
    setEditing(pin, false);
    require(!pin.findChild<ScreenshotPinnedEditController*>(),
            "disabling a fresh editor is allocation-free");
    auto& action = menuAction(pin, "screenshotPinnedDrawingAction");
    const QRect expanded = pin.currentNativeGeometry();
    auto* editButton = buttonNamed(pin, QStringLiteral("Enable annotation mode"));
    require(editButton && action.isEnabled(), "a fresh static pin exposes both UI entry points");
    setThumbnail(pin, true);
    action.trigger();
    require(action.isChecked() && !editing(pin),
            "the menu shows the requested mode while expansion is pending");
    editButton->click();
    require(!editing(pin), "repeated enable waits for the same expansion");
    finishTransition(pin);
    require(editing(pin) && pin.currentNativeGeometry() == expanded,
            "the toolbar opens after full geometry is committed");
    auto* controller = pin.findChild<ScreenshotPinnedEditController*>();
    setEditing(pin, true);
    require(controller == pin.findChild<ScreenshotPinnedEditController*>(),
            "repeated enable keeps the current controller");
    setEditing(pin, false);

    setThumbnail(pin, true);
    editButton->click();
    action.trigger();
    finishTransition(pin);
    require(!editing(pin) && !action.isChecked(), "unchecking the menu cancels the deferred edit");

    setThumbnail(pin, true);
    editButton->click();
    setEditing(pin, false);
    finishTransition(pin);
    require(!editing(pin), "an explicit disable wins over deferred entry");

    setThumbnail(pin, true);
    editButton->click();
    setEditing(pin, true);
    require(pin.currentNativeGeometry() == expanded && !pin.persistenceSnapshot().thumbnailMode,
            "an immediate enable settles an interactive expansion before returning");
    auto* stopped =
        pin.findChild<QVariantAnimation*>(QStringLiteral("screenshotPinnedGeometryAnimation"));
    require(!stopped || stopped->state() == QAbstractAnimation::Stopped,
            "no previous frame may overwrite the committed geometry");

    setThumbnail(pin, true);
    Access::recognitionForHideTest(pin);
    require(!editing(pin) && !Access::recognition(pin)->active(),
            "recognition waits for its requested editor");
    setEditing(pin, true);
    require(Access::recognition(pin)->active(),
            "settling the same enable immediately preserves the pending recognition request");
    auto* confirm =
        buttonNamed(*controller->toolbarWindow()->palette(), QStringLiteral("Confirm edit"));
    require(confirm, "the editing palette exposes Confirm");
    confirm->click();
    require(!Access::recognition(pin)->active() && editing(pin),
            "Confirm immediately leaves recognition and defers palette retirement");
    setEditing(pin, true);
    QCoreApplication::processEvents();
    require(editing(pin), "a newer enable supersedes the queued Confirm exit");
    confirm->click();
    QCoreApplication::processEvents();
    require(!editing(pin), "Confirm exits annotation after the button returns");

    setThumbnail(pin, true);
    editButton->click();
    require(pin.automationUpdate({{QStringLiteral("scale_percent"), 125}}, &error),
            "another geometry command settles the expansion");
    QCoreApplication::processEvents();
    require(!editing(pin) && qRound(Access::scale(pin)) == 125,
            "scaling cancels the interrupted edit");

    auto* platform = Access::installFailingPlatform(pin);
    setThumbnail(pin, true);
    editButton->click();
    platform->failNextGeometry = true;
    const auto refused =
        pin.automationEdit(QStringLiteral("editing"), {{QStringLiteral("enabled"), true}}, &error);
    require(refused.isEmpty() && error == QStringLiteral("action_unavailable") && !editing(pin) &&
                pin.persistenceSnapshot().thumbnailMode,
            "a rejected native expansion preserves a usable thumbnail");
    editButton->click();
    platform->failNextGeometry = true;
    finishTransition(pin);
    require(!editing(pin) && pin.persistenceSnapshot().thumbnailMode && !action.isChecked(),
            "a failed final animation frame cancels the requested edit");
    error.clear();
    setEditing(pin, true);
    require(pin.automationUpdate({{QStringLiteral("click_through"), true}}, &error),
            "enter click-through for the tool command");
    platform->failNextTransparency = true;
    const auto toolFailure = pin.automationEdit(
        QStringLiteral("tool"), {{QStringLiteral("tool"), QStringLiteral("arrow")}}, &error);
    require(toolFailure.isEmpty() && error == QStringLiteral("action_unavailable") &&
                !editing(pin) && Access::clickThroughActive(pin),
            "a failed click-through exit cannot report a successful tool change");
    error.clear();
    const auto selectedTool = pin.automationEdit(
        QStringLiteral("tool"), {{QStringLiteral("tool"), QStringLiteral("arrow")}}, &error);
    require(error.isEmpty() &&
                selectedTool.value(QStringLiteral("active_tool")) == QStringLiteral("arrow") &&
                editing(pin) && !Access::clickThroughActive(pin),
            "tool retries perform the entire transition");

    require(pin.automationUpdate({{QStringLiteral("hide_to_top"), true}}, &error) &&
                pin.automationState().value(QStringLiteral("hide_to_top")).toBool() &&
                !editing(pin),
            "hide at top exits editing when it reserves its handle");
    setEditing(pin, true);
    require(!pin.automationState().value(QStringLiteral("hide_to_top")).toBool(),
            "opening annotation restores the pin from hide at top");

    setThumbnail(pin, true);
    editButton->click();
    pin.hide();
    finishTransition(pin);
    pin.show();
    require(!editing(pin), "a hidden pin does not reopen its palette on the previous completion");

    setThumbnail(pin, true);
    editButton->click();
    ScreenshotClipboardContent replacement;
    replacement.kind = ScreenshotClipboardContentKind::Image;
    replacement.image = QImage(155, 95, QImage::Format_ARGB32_Premultiplied);
    replacement.image.fill(Qt::red);
    require(Access::replace(pin, std::move(replacement)),
            "replace the content while an edit is pending");
    QCoreApplication::processEvents();
    require(!editing(pin) && !pin.persistenceSnapshot().thumbnailMode,
            "replacement retires the previous request");
    setEditing(pin, true);
    require(editing(pin), "the same window accepts an independent request for its replacement");
    confirm = buttonNamed(*controller->toolbarWindow()->palette(), QStringLiteral("Confirm edit"));
    confirm->click();
    ScreenshotClipboardContent secondReplacement;
    secondReplacement.image = QImage(220, 140, QImage::Format_ARGB32_Premultiplied);
    secondReplacement.image.fill(Qt::blue);
    require(Access::replace(pin, std::move(secondReplacement)),
            "replace the pin before Confirm retires its palette");
    QCoreApplication::processEvents();
    require(editing(pin),
            "a queued Confirm from replaced content cannot close the retained editor");
    setThumbnail(pin, true);
    editButton->click();
    require(pin.automationAction(QStringLiteral("hide")),
            "retire a pin with an outstanding edit request");
    QCoreApplication::processEvents();
    require(!pin.isVisible() && !editing(pin),
            "retirement cancels the previous content's transition");
}

void thumbnailSelection() {
    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    const auto key = definition(Action::Thumbnail).key();
    const auto original = store.snapshot().value(key);
    const auto restore = qScopeGuard([&] { store.setValue(key, original); });
    PinnedSelectionFixture fixture;
    auto& a = fixture.add({60, 80}, {220, 160});
    auto& b = fixture.add({360, 90}, {160, 100});
    const auto peer = b.currentNativeGeometry();
    const QList<QPointer<ScreenshotPinnedWindow>> order{&b, &a};
    for (const bool rebound : {false, true}) {
        const auto button = rebound ? Qt::ForwardButton : Qt::LeftButton;
        const auto keys = rebound ? Qt::AltModifier : Qt::NoModifier;
        require(store.setValue(key, encode({Kind::DoubleClick, button, keys})),
                "bind the thumbnail gesture");
        fixture.selection.clearSelection();
        for (const auto& selected : order)
            fixture.selection.toggleSelection(selected);
        auto* surface = a.findChild<SnowCanvasWidget*>();
        const auto expected = rebound ? order : QList<QPointer<ScreenshotPinnedWindow>>{};
        const auto point = surface->mapToGlobal(surface->rect().center());
        click(*surface, button, keys);
        require(fixture.selection.selectedWindows() == expected &&
                    !a.persistenceSnapshot().thumbnailMode,
                "the first release alone determines the ordered selection");
        pointer(*surface, QEvent::MouseButtonDblClick, point, button, button, keys);
        pointer(*surface, QEvent::MouseButtonRelease, point, button, {}, keys);
        finishTransition(a);
        require(fixture.selection.selectedWindows() == expected &&
                    a.persistenceSnapshot().thumbnailMode &&
                    !b.persistenceSnapshot().thumbnailMode && b.currentNativeGeometry() == peer,
                "the thumbnail transition changes just the target display and preserves the first "
                "release's selection");
        setThumbnail(a, false);
    }
    auto& thumbnail = menuAction(a, "screenshotPinnedThumbnailAction");
    thumbnail.trigger();
    finishTransition(a);
    require(fixture.selection.selectedWindows() == order && !b.persistenceSnapshot().thumbnailMode,
            "a checkable menu command has no ordinary left release on the pin");
}

void mouseCancelsPendingAnnotation() {
    auto& store = snow_shot::storage::ApplicationStorage::instance().configuration();
    const auto key = definition(Action::Annotation).key();
    const auto original = store.snapshot().value(key);
    const auto restore = qScopeGuard([&] { store.setValue(key, original); });
    require(store.setValue(key, encode({Kind::Click, Qt::ForwardButton})),
            "bind annotation to a side button");
    PinnedSelectionFixture fixture;
    auto& pin = fixture.add({60, 80}, {240, 180});
    auto* canvas = pin.findChild<SnowCanvasWidget*>();
    auto& action = menuAction(pin, "screenshotPinnedDrawingAction");
    setThumbnail(pin, true);
    click(*canvas, Qt::ForwardButton);
    require(action.isChecked() && !editing(pin), "the first click requests expansion");
    click(*canvas, Qt::ForwardButton);
    require(!action.isChecked(), "the second click cancels annotation before expansion finishes");
    finishTransition(pin);
    require(!editing(pin), "a canceled gesture leaves the palette closed");
}

void editDuringSharedDrag() {
    PinnedSelectionFixture fixture;
    auto& leader = fixture.add({60, 80}, {240, 180});
    auto& peer = fixture.add({360, 80}, {180, 120});
    fixture.selection.toggleSelection(&leader);
    fixture.selection.toggleSelection(&peer);
    auto* canvas = leader.findChild<SnowCanvasWidget*>();
    const QPoint start = canvas->mapToGlobal(canvas->rect().center());
    const QPoint delta(QApplication::startDragDistance() + 8, 15);
    const QRect beforeLeader = leader.currentNativeGeometry();
    const QRect beforePeer = peer.currentNativeGeometry();
    pointer(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, {});
    pointer(*canvas, QEvent::MouseMove, start + delta, Qt::NoButton, Qt::LeftButton, {});
    require(fixture.selection.geometryActive(), "the press and drag move both selected pins");
    const auto movedLeader = leader.currentNativeGeometry();
    const auto movedPeer = peer.currentNativeGeometry();
    require(movedLeader != beforeLeader && movedPeer != beforePeer, "both pins have moved");
    pointer(*canvas, QEvent::MouseMove, start + delta + QPoint(3, 4), Qt::NoButton, Qt::LeftButton,
            {});
    setEditing(peer, true);
    require(!fixture.selection.geometryActive(), "editing a participant finishes the shared drag");
    const auto committedLeader = leader.currentNativeGeometry();
    const auto committedPeer = peer.currentNativeGeometry();
    require(committedLeader != movedLeader && committedPeer != movedPeer &&
                committedLeader.topLeft() - movedLeader.topLeft() ==
                    committedPeer.topLeft() - movedPeer.topLeft(),
            "the final queued movement is committed for both participants");
    pointer(*canvas, QEvent::MouseMove, start + 2 * delta, Qt::NoButton, Qt::LeftButton, {});
    pointer(*canvas, QEvent::MouseButtonRelease, start + 2 * delta, Qt::LeftButton, {}, {});
    require(leader.currentNativeGeometry() == committedLeader &&
                peer.currentNativeGeometry() == committedPeer &&
                fixture.selection.selectedCount() == 2,
            "later pointer events preserve the committed group move");
}

void combinedPresentationUpdates() {
    PinnedSelectionFixture fixture;
    auto& pin = fixture.add({60, 80}, {240, 180}, [](auto& config) {
        config.recognitionResults = cachedOcrPinConfig(nullptr).recognitionResults;
    });
    auto& peer = fixture.add({360, 80}, {180, 120});
    fixture.selection.toggleSelection(&pin);
    fixture.selection.toggleSelection(&peer);
    const auto order = fixture.selection.selectedWindows();
    auto* platform = Access::installFailingPlatform(pin);
    Access::recognitionForHideTest(pin);
    require(Access::recognition(pin)->active() && editing(pin), "start editing with text visible");
    const QRect expanded = pin.currentNativeGeometry();
    const QJsonObject clickAndScale{{QStringLiteral("click_through"), true},
                                    {QStringLiteral("scale_percent"), 125}};
    QString error;
    platform->failNextGeometry = true;
    require(
        !pin.automationUpdate(clickAndScale, &error) &&
            error == QStringLiteral("geometry_failed") && !Access::clickThroughActive(pin) &&
            !platform->transparent && editing(pin) && Access::recognition(pin)->active() &&
            pin.currentNativeGeometry() == expanded && fixture.selection.selectedWindows() == order,
        "a failed scale restores native input, selection, and the existing recognition session");
    require(pin.automationUpdate(clickAndScale, &error) && Access::clickThroughActive(pin) &&
                Access::scale(pin) == 125 && !Access::recognition(pin)->active() && !editing(pin),
            "enabling click-through allows scaling from recognition in one request");
    require(pin.automationUpdate({{QStringLiteral("click_through"), false}}, &error),
            "restore input before testing thumbnail updates");
    setThumbnail(pin, true);
    const QRect thumbnail = pin.currentNativeGeometry();
    const int opacity = Access::opacity(pin);
    platform->failNextGeometry = true;
    require(!pin.automationUpdate(
                {{QStringLiteral("opacity_percent"), 45}, {QStringLiteral("thumbnail"), false}},
                &error) &&
                error == QStringLiteral("geometry_failed") && Access::opacity(pin) == opacity &&
                pin.persistenceSnapshot().thumbnailMode && pin.currentNativeGeometry() == thumbnail,
            "a refused expansion leaves both the thumbnail and its opacity unchanged");
    platform->failNextTransparency = true;
    require(!pin.automationUpdate({{QStringLiteral("geometry"), QJsonArray{10, 20, 100, 90}},
                                   {QStringLiteral("click_through"), true}},
                                  &error) &&
                pin.currentNativeGeometry() == thumbnail &&
                pin.persistenceSnapshot().thumbnailMode && !Access::clickThroughActive(pin) &&
                Access::scale(pin) == 125,
            "a later input failure restores the previous native geometry and thumbnail mode");
    setThumbnail(pin, false);
    require(pin.automationUpdate({{QStringLiteral("hide_to_top"), true}}, &error),
            "enter hide at top before a combined update");
    auto& hide = Access::hideToTop(pin);
    hide.animation().setCurrentTime(hide.animation().duration());
    for (bool revealed : {false, true}) {
        if (revealed)
            hide.updatePointer(hide.handleGeometry().center());
        const auto handle = hide.handleGeometry();
        const auto state = hide.state();
        const auto position = pin.currentNativeGeometry();
        platform->failNextTransparency = true;
        require(!pin.automationUpdate({{QStringLiteral("click_through"), true}}, &error) &&
                    error == QStringLiteral("geometry_failed") &&
                    state == (revealed ? ScreenshotPinnedHideToTopController::State::Revealed
                                       : ScreenshotPinnedHideToTopController::State::Hidden) &&
                    hide.state() == state && hide.handleGeometry() == handle &&
                    pin.currentNativeGeometry() == position && pin.isVisible() == revealed,
                "a refused click-through restores the handle, geometry and prior visibility");
    }
}

void pressAndRelease(QWidget& receiver, Qt::Key key) {
    sendShortcut(receiver, key);
    PhysicalKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&receiver, &release);
}

void shortcutsAndScaling() {
    const snow_shot::storage::PinToScreenShortcutSettings settings;
    const auto drawing = QStringLiteral("drawing_mode");
    const auto thumbnail = QStringLiteral("thumbnail_mode");
    const auto oldDrawing = settings.shortcuts(drawing);
    const auto oldThumbnail = settings.shortcuts(thumbnail);
    const auto restore = qScopeGuard([&] {
        settings.setShortcuts(drawing, oldDrawing);
        settings.setShortcuts(thumbnail, oldThumbnail);
    });
    require(settings.setShortcuts(drawing, {QStringLiteral("F6")}) &&
                settings.setShortcuts(thumbnail, {QStringLiteral("F7")}),
            "use distinct annotation and thumbnail shortcuts");
    PinnedSelectionFixture fixture;
    auto& peer = fixture.add({360, 90}, {180, 120});
    auto& pin = fixture.add({60, 90}, {240, 180});
    fixture.selection.toggleSelection(&peer);
    fixture.selection.toggleSelection(&pin);
    const auto order = fixture.selection.selectedWindows();
    auto* canvas = pin.findChild<SnowCanvasWidget*>();
    pressAndRelease(*canvas, Qt::Key_F6);
    require(editing(pin) && !editing(peer), "the shortcut enters annotation on first use");
    pressAndRelease(*canvas, Qt::Key_F6);
    require(!editing(pin), "the drawing shortcut toggles off");
    pressAndRelease(*canvas, Qt::Key_F7);
    finishTransition(pin);
    require(pin.persistenceSnapshot().thumbnailMode && !peer.persistenceSnapshot().thumbnailMode &&
                fixture.selection.selectedWindows() == order,
            "a thumbnail shortcut affects one selected pin");
    pressAndRelease(*canvas, Qt::Key_F6);
    pressAndRelease(*canvas, Qt::Key_F6);
    finishTransition(pin);
    require(!editing(pin), "a second shortcut cancels pending entry");

    auto* platform = Access::installFailingPlatform(pin);
    const QPointF local = canvas->rect().center();
    QNativeGestureEvent pinch(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2,
                              local, local, canvas->mapToGlobal(local), .03625, {});
    QApplication::sendEvent(canvas, &pinch);
    require(qAbs(Access::scale(pin) - 103.625) < .001 && qAbs(Access::scale(peer) - 103.625) < .001,
            "one native gesture on a canvas applies its continuous factor exactly once to the "
            "ordered selection");
    menuAction(pin, "screenshotPinnedIncreaseScaleAction").trigger();
    require(Access::scale(pin) == 114 && qAbs(Access::scale(peer) - 103.625) < .001,
            "the menu adds ten points to the rounded value on one pin");
    QString error;
    const auto topLeft = pin.currentNativeGeometry().topLeft();
    require(pin.automationUpdate({{QStringLiteral("scale_percent"), 137}}, &error) &&
                Access::scale(pin) == 137 && qAbs(Access::scale(peer) - 103.625) < .001 &&
                pin.currentNativeGeometry().topLeft() == topLeft,
            "an absolute scale retains the target's top-left and leaves peers alone");
    const auto opacity = Access::opacity(pin);
    require(!pin.automationUpdate(
                {{QStringLiteral("scale_percent"), 501}, {QStringLiteral("opacity_percent"), 50}},
                &error) &&
                error == QStringLiteral("invalid_parameters") && Access::opacity(pin) == opacity,
            "invalid scale rejects a combined update before any mutation");
    const auto previous = pin.currentNativeGeometry();
    platform->failNextGeometry = true;
    require(!pin.automationUpdate({{QStringLiteral("scale_percent"), 190}}, &error) &&
                error == QStringLiteral("geometry_failed") &&
                pin.currentNativeGeometry() == previous && Access::scale(pin) == 137,
            "rejected native scale preserves both the committed percentage and rectangle");
}

} // namespace pinned_action_transition_tests
