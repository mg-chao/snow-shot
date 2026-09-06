#pragma once

#include "theme_types.h"

#include <QPalette>

namespace adqt::theme {

QPalette buildPalette(const AdTheme& theme, const QPalette& basePalette = QPalette());
QPalette buildPalette(const ResolvedTheme& resolved, const QPalette& basePalette = QPalette());

}  // namespace adqt::theme
