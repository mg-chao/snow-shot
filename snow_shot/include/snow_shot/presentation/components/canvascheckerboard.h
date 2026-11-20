// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme/theme_manager.h"
#include "widgets/checkerboard.h"

namespace snow_shot::presentation::components {

inline QBrush canvasCheckerboard(const adqt::theme::ThemeMapToken& theme) {
    return adqt::widgets::checkerboardBrush(10, theme.colorBgContainer, theme.colorFillSecondary);
}

} // namespace snow_shot::presentation::components
