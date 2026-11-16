#pragma once

#include "snow_shot/update/updateservice.h"

namespace snow_shot::update::detail {

// All calls and callbacks run on the application thread. Native callbacks are disconnected
// before destruction; a driver must never terminate the application except from install().
class MacOSUpdateDriver {
  public:
    virtual ~MacOSUpdateDriver() = default;
    std::function<void(const UpdateStatus&, bool, bool, bool)> changed;
    std::function<void(const QString&, const QString&)> finished;
    std::function<void(const QString&)> available;
    std::function<void()> committed;
    virtual void start(const QString& mode) = 0;
    virtual void setMode(const QString& mode) = 0;
    virtual void check(bool manual) = 0;
    virtual void download() = 0;
    virtual void cancel() = 0;
    virtual void install() = 0;
    virtual void retranslate() = 0;
};

std::unique_ptr<MacOSUpdateDriver> makeSparkleUpdateDriver();

} // namespace snow_shot::update::detail
