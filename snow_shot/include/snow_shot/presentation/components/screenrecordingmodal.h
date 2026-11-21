#ifndef SNOW_SHOT_PRESENTATION_COMPONENTS_SCREENRECORDINGMODAL_H
#define SNOW_SHOT_PRESENTATION_COMPONENTS_SCREENRECORDINGMODAL_H

#include "snow_shot/presentation/components/modalwindow.h"

namespace snow_shot::presentation {

// The recording area owns placement and native stacking while it exists.
// A render job can outlive that area and then use a detached application modal.
[[nodiscard]] inline adqt::widgets::AdModal* createScreenRecordingModal(QObject* lifetime,
                                                                        QWidget* owner) {
    auto* modal = modal_window::create(lifetime, owner);
    modal->setWindowAlwaysOnTop(owner == nullptr);
    return modal;
}

} // namespace snow_shot::presentation

#endif
