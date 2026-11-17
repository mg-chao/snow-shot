#ifndef SNOW_SHOT_PLATFORM_WINDOWS_PROCESSPOWERQOS_H
#define SNOW_SHOT_PLATFORM_WINDOWS_PROCESSPOWERQOS_H

#include <QStringView>

namespace snow_shot::platform::windows {

[[nodiscard]] bool applyProcessPowerQoS(QStringView value);

} // namespace snow_shot::platform::windows

#endif // SNOW_SHOT_PLATFORM_WINDOWS_PROCESSPOWERQOS_H
