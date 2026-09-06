#include "snow_shot/presentation/styles/buttonborder.h"

#include <algorithm>
#include <cmath>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace snow_shot::presentation::styles {
namespace {
int computeBorderWidthInDevicePixels(qreal devicePixelRatio, int logicalWidth,
                                     BorderWidthRounding rounding) {
    const qreal scaledWidth = devicePixelRatio * static_cast<qreal>(logicalWidth);
    const int roundedWidth = rounding == BorderWidthRounding::Floor
                                 ? static_cast<int>(std::floor(scaledWidth))
                                 : qRound(scaledWidth);
    return roundedWidth;
}

void drawSolidBorderLayer(QPainter* painter, const QColor& color, int widthInDevicePixels,
                          int heightInDevicePixels, int borderWidthInDevicePixels,
                          int radiusInDevicePixels) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->setRenderHint(QPainter::Antialiasing, false);

    const int edgeInset = radiusInDevicePixels;
    if (widthInDevicePixels > 2 * edgeInset) {
        painter->fillRect(
            QRect(edgeInset, 0, widthInDevicePixels - 2 * edgeInset, borderWidthInDevicePixels),
            color);
        painter->fillRect(QRect(edgeInset, heightInDevicePixels - borderWidthInDevicePixels,
                                widthInDevicePixels - 2 * edgeInset, borderWidthInDevicePixels),
                          color);
    }
    if (heightInDevicePixels > 2 * edgeInset) {
        painter->fillRect(
            QRect(0, edgeInset, borderWidthInDevicePixels, heightInDevicePixels - 2 * edgeInset),
            color);
        painter->fillRect(QRect(widthInDevicePixels - borderWidthInDevicePixels, edgeInset,
                                borderWidthInDevicePixels, heightInDevicePixels - 2 * edgeInset),
                          color);
    }

    if (radiusInDevicePixels <= 0) {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    const qreal outerRadius = static_cast<qreal>(radiusInDevicePixels);
    const qreal innerRadius = outerRadius - static_cast<qreal>(borderWidthInDevicePixels);

    auto drawCornerRing = [&](qreal centerX, qreal centerY, const QRectF& clipRect) {
        QPainterPath ringPath;
        ringPath.setFillRule(Qt::OddEvenFill);
        ringPath.addEllipse(QPointF(centerX, centerY), outerRadius, outerRadius);
        if (innerRadius > 0.0) {
            ringPath.addEllipse(QPointF(centerX, centerY), innerRadius, innerRadius);
        }

        QPainterPath clipPath;
        clipPath.addRect(clipRect);
        painter->drawPath(ringPath.intersected(clipPath));
    };

    drawCornerRing(radiusInDevicePixels, radiusInDevicePixels,
                   QRectF(0.0, 0.0, radiusInDevicePixels, radiusInDevicePixels));
    drawCornerRing(widthInDevicePixels - radiusInDevicePixels, radiusInDevicePixels,
                   QRectF(widthInDevicePixels - radiusInDevicePixels, 0.0, radiusInDevicePixels,
                          radiusInDevicePixels));
    drawCornerRing(radiusInDevicePixels, heightInDevicePixels - radiusInDevicePixels,
                   QRectF(0.0, heightInDevicePixels - radiusInDevicePixels, radiusInDevicePixels,
                          radiusInDevicePixels));
    drawCornerRing(widthInDevicePixels - radiusInDevicePixels,
                   heightInDevicePixels - radiusInDevicePixels,
                   QRectF(widthInDevicePixels - radiusInDevicePixels,
                          heightInDevicePixels - radiusInDevicePixels, radiusInDevicePixels,
                          radiusInDevicePixels));
}

void drawDashedBorderLayer(QPainter* painter, const QColor& color, int widthInDevicePixels,
                           int heightInDevicePixels, int borderWidthInDevicePixels,
                           int radiusInDevicePixels) {
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);

    QPen pen(color);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(borderWidthInDevicePixels);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setDashPattern({3.0, 2.0});
    painter->setPen(pen);

    const qreal inset = static_cast<qreal>(borderWidthInDevicePixels) / 2.0;
    const QRectF borderRect(inset, inset, static_cast<qreal>(widthInDevicePixels) - 2.0 * inset,
                            static_cast<qreal>(heightInDevicePixels) - 2.0 * inset);
    const qreal radius = static_cast<qreal>(radiusInDevicePixels) - inset;
    painter->drawRoundedRect(borderRect, radius, radius);
}
} // namespace

void drawButtonBorder(QPainter* painter, const QSize& logicalSize, const ButtonBorderSpec& spec) {
    if (painter == nullptr || logicalSize.isEmpty() || spec.width <= 0 || !spec.color.isValid()) {
        return;
    }

    const qreal devicePixelRatio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const int widthInDevicePixels =
        static_cast<int>(std::ceil(static_cast<qreal>(logicalSize.width()) * devicePixelRatio));
    const int heightInDevicePixels =
        static_cast<int>(std::ceil(static_cast<qreal>(logicalSize.height()) * devicePixelRatio));
    const int borderWidthInDevicePixels =
        computeBorderWidthInDevicePixels(devicePixelRatio, spec.width, spec.widthRounding);
    const int radiusInDevicePixels =
        std::clamp(qRound(static_cast<qreal>(spec.radius) * devicePixelRatio), 0,
                   std::min(widthInDevicePixels, heightInDevicePixels) / 2);

    QImage layer(QSize(widthInDevicePixels, heightInDevicePixels),
                 QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);

    QPainter layerPainter(&layer);
    if (spec.pattern == BorderPattern::Solid) {
        drawSolidBorderLayer(&layerPainter, spec.color, widthInDevicePixels, heightInDevicePixels,
                             borderWidthInDevicePixels, radiusInDevicePixels);
    } else {
        drawDashedBorderLayer(&layerPainter, spec.color, widthInDevicePixels, heightInDevicePixels,
                              borderWidthInDevicePixels, radiusInDevicePixels);
    }
    layerPainter.end();

    layer.setDevicePixelRatio(devicePixelRatio);
    painter->drawImage(QPoint(0, 0), layer);
}
} // namespace snow_shot::presentation::styles
