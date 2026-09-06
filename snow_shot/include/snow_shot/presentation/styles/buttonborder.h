#ifndef SNOW_SHOT_PRESENTATION_STYLES_BUTTONBORDER_H
#define SNOW_SHOT_PRESENTATION_STYLES_BUTTONBORDER_H

#include <QColor>
#include <QSize>

class QPainter;

namespace snow_shot::presentation::styles {
enum class BorderPattern {
    Solid,
    Dashed,
};

enum class BorderWidthRounding {
    Floor,
    Round,
};

struct ButtonBorderSpec {
    QColor color;
    int width = 1;
    int radius = 0;
    BorderPattern pattern = BorderPattern::Solid;
    BorderWidthRounding widthRounding = BorderWidthRounding::Floor;
};

void drawButtonBorder(QPainter* painter, const QSize& logicalSize, const ButtonBorderSpec& spec);
} // namespace snow_shot::presentation::styles

#endif // SNOW_SHOT_PRESENTATION_STYLES_BUTTONBORDER_H
