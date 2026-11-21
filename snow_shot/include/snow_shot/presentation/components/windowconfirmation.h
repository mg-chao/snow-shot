#pragma once

#include "snow_shot/presentation/components/confirmationskipbutton.h"
#include "snow_shot/presentation/components/modalwindow.h"

#include <utility>

namespace snow_shot::presentation::modal_window {

// The caller can install validation, translations and result handlers before opening.
[[nodiscard]] inline adqt::widgets::AdModal*
createConfirmation(QObject* lifetime, QWidget* owner,
                   adqt::widgets::AdModalService::Request options = {},
                   std::function<bool()> disableConfirmation = {}) {
    using adqt::widgets::AdModal;
    options.preset = AdModal::Preset::Confirm;
    auto* modal = create(lifetime, owner, std::move(options));
    QObject::connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
    if (disableConfirmation) {
        // The standard footer exists once the window has been presented.
        QObject::connect(
            modal, &AdModal::openChanged, modal,
            [modal, disable = std::move(disableConfirmation)](bool open) {
                if (open)
                    new ConfirmationSkipButton(*modal, disable);
            },
            Qt::SingleShotConnection);
    }
    return modal;
}

} // namespace snow_shot::presentation::modal_window
