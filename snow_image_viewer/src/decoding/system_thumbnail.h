#pragma once

#include "core/image_types.h"
#include "decoding/image_decoder.h"

#include <QString>

namespace snow::image_viewer {

inline constexpr int kSystemThumbnailMaximumExtent = 1024;

ImageThumbnail loadSystemThumbnail(const QString& filePath,
                                   int maximumExtent = kSystemThumbnailMaximumExtent,
                                   const DecodeCancellation* cancellation = nullptr);

} // namespace snow::image_viewer
