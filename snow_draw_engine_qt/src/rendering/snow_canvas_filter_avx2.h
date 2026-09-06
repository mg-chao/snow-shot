#pragma once

#include "snow_canvas_filter_render.h"

namespace snow_canvas_filter_render::detail {

bool grayscaleAvx2(ImageView image, int beginRow, int endRow, int mix);
bool invertAvx2(ImageView image, int beginRow, int endRow, int mix);
bool grayscaleRectAvx2(ConstImageView source, ImageView destination, int left, int top, int right,
                       int bottom, int mix);
bool invertRectAvx2(ConstImageView source, ImageView destination, int left, int top, int right,
                    int bottom, int mix);
bool grayscaleMaskedAvx2(ConstImageView source, ImageView destination, AlphaView mask,
                         int maskOriginX, int maskOriginY, int left, int top, int right, int bottom,
                         int strengthMix);
bool invertMaskedAvx2(ConstImageView source, ImageView destination, AlphaView mask, int maskOriginX,
                      int maskOriginY, int left, int top, int right, int bottom, int strengthMix);
bool copyRowsAvx2(ConstImageView source, ImageView destination, int beginRow, int endRow);
bool downsampleFourTapAvx2(ConstImageView source, int sourceLeft, int sourceTop, int sourceRight,
                           int sourceBottom, ImageView destination, int factor, int beginRow,
                           int endRow);
int interpolateAndBlendConstantAvx2(const QRgb* first, const QRgb* second, QRgb* destination,
                                    int count, int weight, int mix);
int interpolateAndBlendMaskedAvx2(const QRgb* first, const QRgb* second, QRgb* destination,
                                  const std::uint8_t* mask, int count, int weight);

} // namespace snow_canvas_filter_render::detail
