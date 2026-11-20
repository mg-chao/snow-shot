// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SNOW_SHOT_PRESENTATION_STYLES_OUTPUTDESCRIPTIONSTYLE_H
#define SNOW_SHOT_PRESENTATION_STYLES_OUTPUTDESCRIPTIONSTYLE_H

#include "theme/theme_types.h"

#include <QLabel>

namespace snow_shot::presentation::styles {

inline void applyOutputDescriptionStyle(QLabel& label, QFont font,
                                        const adqt::theme::ThemeMapToken& theme) {
    font.setPixelSize(qMax(10, qRound(theme.fontSizeSM)));
    font.setWeight(QFont::Normal);
    label.setFont(font);
    label.setFixedHeight(label.fontMetrics().height());
    auto palette = label.palette();
    palette.setColor(QPalette::WindowText, theme.colorTextSecondary);
    label.setPalette(palette);
}

} // namespace snow_shot::presentation::styles

#endif // SNOW_SHOT_PRESENTATION_STYLES_OUTPUTDESCRIPTIONSTYLE_H
