#pragma once

#include "snow_draw_engine.h"

namespace snow_canvas_element_id {

inline bool hasElementId(const SnowElementId& id) {
    return id.generation != 0;
}

inline bool sameElementId(const SnowElementId& lhs, const SnowElementId& rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

} // namespace snow_canvas_element_id
