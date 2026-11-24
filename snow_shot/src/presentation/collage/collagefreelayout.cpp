// SPDX-License-Identifier: GPL-3.0-or-later
#include "collagefreelayout_p.h"
#include "collageresourcebudget_p.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace snow_shot::presentation::image_stitching_detail {
namespace {
QSizeF scaledSize(QSize source, qreal scale) {
    if (source.isEmpty() || !std::isfinite(scale) || scale <= 0)
        return {};
    return {std::max(1.0, std::round(source.width() * scale)),
            std::max(1.0, std::round(source.height() * scale))};
}

QRectF freeImageRect(QPointF center, QSizeF rasterSize) {
    if (rasterSize.isEmpty())
        return {};
    // Round ties consistently under integer translation, including across the world origin.
    return {QPointF(std::floor(center.x() - rasterSize.width() / 2 + .5),
                    std::floor(center.y() - rasterSize.height() / 2 + .5)),
            rasterSize};
}

} // namespace

QRectF scaledFreeImageRect(QPointF center, QSize source, qreal scale) {
    return freeImageRect(center, scaledSize(source, scale));
}

QRectF freeCanvasBounds(QRectF imageUnion, int border) {
    if (imageUnion.isEmpty())
        return {};
    const auto canvas = imageUnion.adjusted(-qreal(border), -qreal(border), border, border);
    constexpr qreal exactPixelLimit = qreal(1LL << 51);
    qint64 bytes = 0;
    if (!(std::abs(canvas.x()) <= exactPixelLimit && std::abs(canvas.y()) <= exactPixelLimit) ||
        !(canvas.width() > 0 && canvas.width() <= std::numeric_limits<int>::max() / 4 &&
          canvas.height() > 0 && canvas.height() <= std::numeric_limits<int>::max()) ||
        !outputBytes(canvas.size().toSize(), &bytes))
        return {};
    return canvas;
}

FreeLayoutImage FreeLayoutCursor::resolveImage(const ImageStitchingItem& item,
                                               const ImageStitchingSettings& settings,
                                               int referenceHeight) const {
    const auto source = transformedSize(item);
    QSizeF size = source;
    if (item.freePlacement && item.freePlacement->pixelsPerPixel) {
        size = scaledSize(source, *item.freePlacement->pixelsPerPixel);
    } else if (settings.sizing == ImageStitchingSizing::Uniform && !source.isEmpty()) {
        // Match horizontal layout's half-up rounding, even at exact half-pixel ties.
        const auto width =
            (qint64(source.width()) * referenceHeight + source.height() / 2) / source.height();
        size = {qreal(std::max<qint64>(1, width)), qreal(referenceHeight)};
    }
    const QPointF nextCenter((m_bounds.isEmpty() ? 0.0 : m_bounds.right() + settings.spacing) +
                                 size.width() / 2,
                             m_centerY);
    const auto placement = item.freePlacement.value_or(ImageStitchingFreePlacement{nextCenter, {}});
    return {placement, freeImageRect(placement.centerPixels, size)};
}

void FreeLayoutCursor::includeImage(const FreeLayoutImage& image) {
    if (m_bounds.isEmpty() || image.rect.right() >= m_bounds.right())
        m_centerY = image.placement.centerPixels.y();
    m_bounds = m_bounds.united(image.rect);
}

FreeLayoutImage FreeLayoutCursor::resolve(const ImageStitchingItem& item,
                                          const ImageStitchingSettings& settings,
                                          int referenceHeight) {
    const auto image = resolveImage(item, settings, referenceHeight);
    includeImage(image);
    return image;
}

std::optional<ImageStitchingFreePlacement>
FreeLayoutCursor::tryAppend(const ImageStitchingItem& item, const ImageStitchingSettings& settings,
                            int referenceHeight) {
    const auto fits = [&](const QRectF& rect) {
        return !freeCanvasBounds(rect, 0).isEmpty() &&
               !freeCanvasBounds(m_bounds.united(rect), settings.border).isEmpty();
    };
    auto image = resolveImage(item, settings, referenceHeight);
    if (!fits(image.rect)) {
        if (m_bounds.isEmpty())
            return std::nullopt;
        // Aligning both top-left corners minimizes the union without moving existing images.
        image.rect.moveTopLeft(m_bounds.topLeft());
        image.placement.centerPixels = image.rect.center();
        if (!fits(image.rect))
            return std::nullopt;
    }
    includeImage(image);
    return image.placement;
}

ImageStitchingLayout calculateFreeLayout(const QVector<ImageStitchingItem>& items,
                                         const ImageStitchingSettings& settings) {
    ImageStitchingLayout layout;
    if (items.isEmpty())
        return layout;
    const int reference = transformedSize(items.front()).height();
    FreeLayoutCursor cursor;
    QVector<QRectF> world;
    world.reserve(items.size());
    for (const auto& item : items) {
        const auto rect = cursor.resolve(item, settings, reference).rect;
        if (freeCanvasBounds(rect, 0).isEmpty())
            return {};
        world.append(rect);
    }
    const auto canvas = freeCanvasBounds(cursor.imageBounds(), settings.border);
    if (canvas.isEmpty())
        return {};
    layout.origin = canvas.topLeft();
    layout.size = canvas.size().toSize();
    layout.imageRects.reserve(world.size());
    for (const auto& rect : world)
        layout.imageRects.append(rect.translated(-layout.origin).toRect());
    layout.rects = layout.imageRects;
    layout.clipRects = layout.imageRects;
    return layout;
}

void adjustFreeSpacing(QVector<ImageStitchingItem>& items, int delta) {
    if (!delta || items.isEmpty())
        return;
    QVector<qsizetype> order;
    for (qsizetype i = 0; i < items.size(); ++i)
        if (items[i].freePlacement)
            order.append(i);
    if (order.isEmpty())
        return;
    for (const bool horizontal : {true, false}) {
        const auto coordinate = [&](qsizetype i) -> qreal& {
            auto& center = items[i].freePlacement->centerPixels;
            return horizontal ? center.rx() : center.ry();
        };
        std::sort(order.begin(), order.end(),
                  [&](auto a, auto b) { return coordinate(a) < coordinate(b); });
        qreal previous = coordinate(order.front());
        qreal adjusted = previous;
        for (const auto i : order) {
            const qreal current = coordinate(i);
            if (current != previous)
                adjusted += std::max(0.0, current - previous + delta);
            coordinate(i) = adjusted;
            previous = current;
        }
    }
}
} // namespace snow_shot::presentation::image_stitching_detail
