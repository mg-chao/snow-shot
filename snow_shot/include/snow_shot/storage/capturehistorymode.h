// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace snow_shot::storage {
enum class CaptureHistoryMode { Full, ResultsOnly, Disabled };

inline QString captureHistoryModeId(CaptureHistoryMode mode) {
    switch (mode) {
    case CaptureHistoryMode::ResultsOnly:
        return QStringLiteral("results_only");
    case CaptureHistoryMode::Disabled:
        return QStringLiteral("disabled");
    case CaptureHistoryMode::Full:
        return QStringLiteral("full");
    }
    return {};
}

inline CaptureHistoryMode captureHistoryModeFromId(QStringView id) {
    if (id == u"results_only")
        return CaptureHistoryMode::ResultsOnly;
    if (id == u"disabled")
        return CaptureHistoryMode::Disabled;
    return CaptureHistoryMode::Full;
}
} // namespace snow_shot::storage
