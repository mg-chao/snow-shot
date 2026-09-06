#pragma once

#include "snow/image/document.h"
#include "snow/image/result.h"

#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace snow::image::internal {

[[nodiscard]] std::optional<Orientation>
parse_exif_orientation(std::span<const std::byte> exif) noexcept;
[[nodiscard]] bool rewrite_exif_orientation(std::vector<std::byte>* exif,
                                            Orientation orientation) noexcept;
[[nodiscard]] Result<void> apply_orientation(Document* document, Orientation orientation,
                                             std::stop_token stop = {});

} // namespace snow::image::internal
