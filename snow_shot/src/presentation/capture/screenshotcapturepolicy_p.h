#ifndef SNOW_SHOT_PRESENTATION_CAPTURE_SCREENSHOTCAPTUREPOLICY_P_H
#define SNOW_SHOT_PRESENTATION_CAPTURE_SCREENSHOTCAPTUREPOLICY_P_H

#include "snow_shot/presentation/capture/screenshotcapturepolicy.h"

#include <windows.h>
#include <atomic>
#include <mutex>
#include <optional>

namespace snow_shot::presentation::capture::detail {

class AutoScreenshotModeCache final {
  public:
    void invalidate() noexcept {
        m_dirty = true;
    }

    template <typename Query> ScreenshotApiMode resolve(Query query) {
        const std::lock_guard lock(m_mutex);
        // Consume before querying so a notification during the query survives publication.
        if (m_dirty.exchange(false)) {
            if (const auto mode = query())
                m_mode = *mode;
            else
                m_dirty = true;
        }
        return m_mode;
    }

  private:
    std::atomic<bool> m_dirty{true};
    std::mutex m_mutex;
    ScreenshotApiMode m_mode = ScreenshotApiMode::Gdi;
};

using DisplayColorInfoQuery = LONG(WINAPI*)(DISPLAYCONFIG_DEVICE_INFO_HEADER*);

[[nodiscard]] ScreenshotApiMode autoScreenshotApiMode(bool anyActiveHdr) noexcept;

[[nodiscard]] std::optional<bool>
displayHdrEnabled(const DISPLAYCONFIG_PATH_TARGET_INFO& target,
                  DisplayColorInfoQuery query = DisplayConfigGetDeviceInfo) noexcept;

} // namespace snow_shot::presentation::capture::detail

#endif
