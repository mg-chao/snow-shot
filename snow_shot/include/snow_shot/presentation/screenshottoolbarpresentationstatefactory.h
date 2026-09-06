#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTTOOLBARPRESENTATIONSTATEFACTORY_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTTOOLBARPRESENTATIONSTATEFACTORY_H

#include "snow_shot/presentation/screenshottoolbarpresenter.h"

class ScreenshotInteractionState;
class ScreenshotSelectionModel;

[[nodiscard]] ScreenshotToolbarPresentationState
makeScreenshotToolbarPresentationState(const ScreenshotInteractionState& interaction,
                                       const ScreenshotSelectionModel& selection);

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTTOOLBARPRESENTATIONSTATEFACTORY_H
