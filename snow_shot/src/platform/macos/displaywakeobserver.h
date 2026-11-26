// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SNOW_SHOT_PLATFORM_MACOS_DISPLAYWAKEOBSERVER_H
#define SNOW_SHOT_PLATFORM_MACOS_DISPLAYWAKEOBSERVER_H

#include <functional>
#include <memory>

class QObject;

namespace snow_shot::platform::macos {

// Register and destroy on the main thread. The callback runs on that thread.
[[nodiscard]] std::unique_ptr<QObject> observeDisplayWake(std::function<void()> onWake);

} // namespace snow_shot::platform::macos

#endif
