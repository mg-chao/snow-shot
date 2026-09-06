#pragma once

#include <cstddef>
#include <cstdint>

namespace snow_canvas_pen_mask_avx2 {

bool rasterizeCapsuleSegment(std::uint8_t* alpha, std::ptrdiff_t stride, int tileLeft, int tileTop,
                             int beginX, int endX, int beginY, int endY, double ax, double ay,
                             double bx, double by, double transitionOuter);

} // namespace snow_canvas_pen_mask_avx2
