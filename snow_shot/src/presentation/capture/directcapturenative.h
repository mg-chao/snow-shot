#ifndef SNOW_SHOT_PRESENTATION_DIRECTCAPTURENATIVE_H
#define SNOW_SHOT_PRESENTATION_DIRECTCAPTURENATIVE_H
#include "snow_shot/presentation/directcaptureworkflow.h"
namespace snow_shot::presentation {
[[nodiscard]] DirectCaptureFrame captureDirectTarget(const DirectCaptureRequest& request);
}
#endif
