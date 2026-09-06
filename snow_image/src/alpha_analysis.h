#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace snow::image::detail {

enum class AlphaAnalysisValue : std::uint8_t {
    unknown,
    analyzing,
    opaque,
    non_opaque,
};

struct AlphaAnalysisState final {
    std::atomic<AlphaAnalysisValue> value{AlphaAnalysisValue::unknown};
    std::mutex mutex;
    std::condition_variable_any changed;
};

} // namespace snow::image::detail
