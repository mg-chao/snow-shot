#pragma once

#include "snow_draw_engine_qt/snow_canvas_types.h"

#include <QtGlobal>

#include <algorithm>

namespace demo_serial_number_controls {

inline bool isSerialNumberSource(SnowCanvasStyleToolbarSource source) {
    return source == SnowCanvasStyleToolbarSource::DefaultSerialNumber ||
           source == SnowCanvasStyleToolbarSource::SelectedSerialNumber;
}

inline bool hasMixedSelectedSerialNumber(const SnowCanvasStyleToolbarState& state) {
    return (state.serialNumberStyleMixed & SnowCanvasSerialNumberStyleMixedNumber) != 0;
}

inline bool selectedSerialNumberCanDecrease(const SnowCanvasStyleToolbarState& state) {
    if (state.source != SnowCanvasStyleToolbarSource::SelectedSerialNumber) {
        return false;
    }
    return hasMixedSelectedSerialNumber(state) || state.serialNumberStyle.number > 0;
}

inline bool serialNumberControlsCanDecrease(const SnowCanvasStyleToolbarState& state) {
    if (state.source == SnowCanvasStyleToolbarSource::SelectedSerialNumber) {
        return selectedSerialNumberCanDecrease(state);
    }
    if (state.source == SnowCanvasStyleToolbarSource::DefaultSerialNumber) {
        return state.serialNumberStyle.number > 0;
    }
    return false;
}

inline qint64 defaultSerialNumberAfterStep(qint64 number, qint64 delta) {
    if (delta < 0) {
        return std::max<qint64>(0, number + delta);
    }
    return number + delta;
}

} // namespace demo_serial_number_controls
