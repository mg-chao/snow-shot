#pragma once

#include "widgets/detail/window_modality.h"

#include <QApplication>
#include <QPointer>
#include <QWidget>
#include <QWindow>

namespace snow_shot::presentation {

[[nodiscard]] inline bool prepareCaptureInput() {
    // Qt redirects mouse events to the active popup even on unrelated windows.
    // Closing one menu can reveal another popup or open a dialog in its close handler.
    while (QWidget* popup = QApplication::activePopupWidget()) {
        const QPointer<QWidget> guard(popup);
        popup->close();
        if (guard && guard->isVisible())
            return false;
    }

    if (!QGuiApplication::modalWindow())
        return true;
    // Capture overlays are independent top-level windows and may not exist yet.
    // Query the same ownership hierarchy without showing or creating a native surface.
    QWindow overlay;
    overlay.setFlags(Qt::Tool);
    return !adqt::widgets::detail::blockingModalWindow(&overlay);
}

} // namespace snow_shot::presentation
