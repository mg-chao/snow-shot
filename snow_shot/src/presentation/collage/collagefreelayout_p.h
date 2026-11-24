// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/presentation/collagedocument.h"

namespace snow_shot::presentation::image_stitching_detail {

struct FreeLayoutImage {
    ImageStitchingFreePlacement placement;
    QRectF rect;
};

// Resolve items in list order. A tie on the right edge favors the topmost item.
class FreeLayoutCursor {
  public:
    FreeLayoutImage resolve(const ImageStitchingItem& item, const ImageStitchingSettings& settings,
                            int referenceHeight);
    [[nodiscard]] std::optional<ImageStitchingFreePlacement>
    tryAppend(const ImageStitchingItem& item, const ImageStitchingSettings& settings,
              int referenceHeight);
    [[nodiscard]] QRectF imageBounds() const {
        return m_bounds;
    }

  private:
    FreeLayoutImage resolveImage(const ImageStitchingItem& item,
                                 const ImageStitchingSettings& settings, int referenceHeight) const;
    void includeImage(const FreeLayoutImage& image);

    QRectF m_bounds;
    qreal m_centerY = 0;
};

QRectF scaledFreeImageRect(QPointF center, QSize source, qreal scale);
QRectF freeCanvasBounds(QRectF imageUnion, int border);
ImageStitchingLayout calculateFreeLayout(const QVector<ImageStitchingItem>& items,
                                         const ImageStitchingSettings& settings);
void adjustFreeSpacing(QVector<ImageStitchingItem>& items, int delta);

} // namespace snow_shot::presentation::image_stitching_detail
