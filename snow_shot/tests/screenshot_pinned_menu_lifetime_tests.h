// SPDX-License-Identifier: GPL-3.0-or-later
// Included after the pinned-window and crop fixtures.

void pinnedMenuActionLifetime() {
    using Access = ScreenshotPinnedWindowTestAccess;
    using ResizeHandle = screenshot_pinned_resize_geometry::DragHandle;
    ScreenshotPinnedWindow window;
    Access::prepareReplacement(window, pinned_crop_tests::config());
    window.show();

    const auto drainRetiredMenus = [] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    const auto open = [&] {
        QPointer<adqt::widgets::AdContextMenu> menu = openPinnedContextMenu(window);
        require(menu && Access::applyCropAction(window) == menu->findChild<QAction*>(QStringLiteral(
                                                               "screenshotPinnedApplyCropAction")),
                "the crop action belongs to the current popup");
        require(Access::cancelCropAction(window) &&
                    Access::cancelCropAction(window) == menu->findChild<QAction*>(QStringLiteral(
                                                            "screenshotPinnedCancelCropAction")) &&
                    menu->actions().indexOf(Access::cancelCropAction(window)) ==
                        menu->actions().indexOf(Access::applyCropAction(window)) + 1,
                "Cancel crop follows Apply crop in the current popup");
        return menu;
    };
    const auto retire = [&](QPointer<adqt::widgets::AdContextMenu>& menu) {
        QPointer<QAction> action = Access::applyCropAction(window);
        QPointer<QAction> cancel = Access::cancelCropAction(window);
        require(action && cancel, "the popup owns a crop action before retirement");
        menu->dismissPopup();
        drainRetiredMenus();
        require(!menu && !action && !cancel, "closing the popup deletes its menu and crop action");
        require(!Access::applyCropAction(window) && !Access::cancelCropAction(window),
                "the pin must not retain a destroyed popup's crop action");
    };

    auto menu = open();
    require(!Access::applyCropAction(window)->isVisible() &&
                !Access::cancelCropAction(window)->isVisible(),
            "an uncropped pin hides both crop actions");
    retire(menu);

    // Matches the crash path: a controlled resize settles its native scale after the
    // popup's deferred deletion, then refreshes the pin's menu state.
    const QRect beforeResize = window.currentNativeGeometry();
    const double beforeScale = Access::scale(window);
    const QPointF start = beforeResize.bottomRight();
    require(Access::beginControlled(window, start, int(ResizeHandle::BottomRight)),
            "begin resizing after menu deletion");
    Access::updateControlled(window, start + QPointF(40, 32));
    Access::endControlled(window, false);
    require(!Access::interactionActive(window) && Access::scale(window) > beforeScale &&
                window.currentNativeGeometry().size() != beforeResize.size(),
            "resize settlement updates geometry and scale after menu deletion");

    const QRectF crop(110, 210, 60, 50);
    require(Access::crop(window, crop) && Access::visibleCrop(window) == crop,
            "crop presentation remains usable after menu deletion");
    menu = open();
    require(Access::applyCropAction(window)->isVisible() &&
                Access::cancelCropAction(window)->isVisible() &&
                Access::applyCropAction(window)->isEnabled() &&
                Access::cancelCropAction(window)->isEnabled(),
            "a reopened popup reflects the current crop");

    // A new popup can be created while the old one's deletion is still queued.
    QPointer<QAction> oldAction = Access::applyCropAction(window);
    menu->dismissPopup();
    auto replacement = open();
    QPointer<QAction> replacementAction = Access::applyCropAction(window);
    require(replacement != menu && replacementAction != oldAction,
            "a new popup owns a distinct crop action");
    drainRetiredMenus();
    require(!menu && !oldAction && replacement && replacementAction &&
                Access::applyCropAction(window) == replacementAction,
            "deleting the old popup preserves the replacement's action");
    require(Access::crop(window, {120, 220, 40, 30}) && replacementAction->isEnabled(),
            "crop changes update the replacement popup");
    Access::cancelCropAction(window)->trigger();
    require(!replacementAction->isVisible() && !Access::cancelCropAction(window)->isVisible(),
            "cancel hides both actions in the current popup");
    retire(replacement);
    menu = open();
    require(!Access::applyCropAction(window)->isVisible() &&
                !Access::cancelCropAction(window)->isVisible(),
            "reopening after cancel hides both actions");
    require(Access::crop(window, {120, 220, 40, 30}) &&
                Access::applyCropAction(window)->isVisible() &&
                Access::cancelCropAction(window)->isVisible(),
            "setting a crop reveals both actions in the open menu");
    retire(menu);

    Access::applyCrop(window);
    require(!window.persistenceSnapshot().cropRect &&
                Access::originalImage(window).size() == QSize(40, 30),
            "applying a crop without a menu commits the cropped source");
    menu = open();
    require(!Access::applyCropAction(window)->isVisible() &&
                !Access::cancelCropAction(window)->isVisible(),
            "reopening after apply hides both crop actions");
    retire(menu);
    window.hide();
}
