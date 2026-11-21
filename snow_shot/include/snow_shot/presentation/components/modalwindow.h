#pragma once

#include "widgets/modal.h"

namespace snow_shot::presentation::modal_window {

// An ownerless job must not acquire whichever unrelated window happens to be active.
inline void detach(adqt::widgets::AdModal& modal) {
    modal.setWindowModeDetached(true);
    modal.setOwnerWindow(nullptr);
    modal.setWindowModality(Qt::ApplicationModal);
}

[[nodiscard]] inline adqt::widgets::AdModal*
create(QObject* lifetime, QWidget* owner, adqt::widgets::AdModalService::Request options = {}) {
    options.mode = adqt::widgets::AdModal::Mode::Window;
    const auto scope = options.windowModality.value_or(Qt::WindowModal);
    options.windowModality = !owner && scope == Qt::WindowModal ? Qt::ApplicationModal : scope;
    options.centered = true;
    options.maskVisible = false;
    options.closeOnMaskClick = false;
    auto* modal = adqt::widgets::AdModalService::create(options, lifetime, owner);
    if (!owner)
        modal->setWindowModeDetached(true);
    return modal;
}

} // namespace snow_shot::presentation::modal_window
