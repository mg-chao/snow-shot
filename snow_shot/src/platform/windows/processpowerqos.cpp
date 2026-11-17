#include "snow_shot/platform/windows/processpowerqos.h"

#include <QString>
#include <windows.h>

namespace snow_shot::platform::windows {

bool applyProcessPowerQoS(QStringView value) {
    PROCESS_POWER_THROTTLING_STATE policy{};
    policy.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (value == QStringLiteral("performance")) {
        policy.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    } else if (value == QStringLiteral("power_saving")) {
        policy.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        policy.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    } else if (value != QStringLiteral("normal")) {
        return false;
    }
    // Zero masks restore Windows management in Normal mode.
    return SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &policy,
                                 sizeof(policy)) != FALSE;
}

} // namespace snow_shot::platform::windows
