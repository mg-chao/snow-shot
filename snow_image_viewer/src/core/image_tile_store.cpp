#include "core/image_tile_store.h"

#include "core/image_raster_store.h"

#include <snow/image/raster.h>
#include <snow/image/raster_conversion.h>

#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace snow::image_viewer {

ImageTileStore::ImageTileStore(QString filePath,
                               std::shared_ptr<snow::image::RasterStore> rasterStore,
                               QSize sourceSize, QImage preview, QSize tileSize)
    : ImageTileStore(ImageRasterStore::adopt(std::move(filePath), std::move(rasterStore)),
                     sourceSize, std::move(preview), tileSize) {}

ImageTileStore::ImageTileStore(std::shared_ptr<ImageRasterStore> rasterStore, QSize sourceSize,
                               QImage preview, QSize tileSize)
    : rasterStore_(std::move(rasterStore)), sourceSize_(sourceSize), preview_(std::move(preview)),
      tileSize_(tileSize) {}

ImageTileStore::~ImageTileStore() = default;

QSize ImageTileStore::sourceSize() const noexcept {
    return sourceSize_;
}

const QImage& ImageTileStore::preview() const noexcept {
    return preview_;
}

const std::shared_ptr<ImageRasterStore>& ImageTileStore::rasterStore() const noexcept {
    return rasterStore_;
}

std::vector<StoredImageTile> ImageTileStore::tilesIntersecting(const QRectF& sourceRect) const {
    std::vector<StoredImageTile> result;
    if (!sourceRect.isValid() || !sourceSize_.isValid() || !tileSize_.isValid()) {
        return result;
    }
    const QRectF clipped = sourceRect.intersected(QRectF(QPointF(0.0, 0.0), sourceSize_));
    if (clipped.isEmpty())
        return result;
    const int firstColumn =
        std::max(0, static_cast<int>(std::floor(clipped.x() / tileSize_.width())));
    const int firstRow =
        std::max(0, static_cast<int>(std::floor(clipped.y() / tileSize_.height())));
    const int lastColumn = std::min(
        (sourceSize_.width() - 1) / tileSize_.width(),
        static_cast<int>(std::ceil((clipped.x() + clipped.width()) / tileSize_.width())) - 1);
    const int lastRow = std::min(
        (sourceSize_.height() - 1) / tileSize_.height(),
        static_cast<int>(std::ceil((clipped.y() + clipped.height()) / tileSize_.height())) - 1);
    result.reserve(static_cast<std::size_t>(lastColumn - firstColumn + 1) *
                   static_cast<std::size_t>(lastRow - firstRow + 1));
    for (int row = firstRow; row <= lastRow; ++row) {
        const int y = row * tileSize_.height();
        const int height = std::min(tileSize_.height(), sourceSize_.height() - y);
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const int x = column * tileSize_.width();
            const int width = std::min(tileSize_.width(), sourceSize_.width() - x);
            result.push_back({QRect(x, y, width, height)});
        }
    }
    return result;
}

QImage ImageTileStore::load(const StoredImageTile& tile, QString* error) const {
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return QImage{};
    };
    const auto& store =
        rasterStore_ ? rasterStore_->store() : std::shared_ptr<snow::image::RasterStore>{};
    if (!store || !store->complete() || !tile.sourceRect.isValid() ||
        !QRect(QPoint(0, 0), sourceSize_).contains(tile.sourceRect)) {
        return fail(QStringLiteral("The image tile index is invalid."));
    }

    QImage image(tile.sourceRect.size(), QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull()) {
        return fail(QStringLiteral("The image tile could not be allocated."));
    }
    snow::image::MutablePlaneView destination;
    destination.width = static_cast<std::uint32_t>(image.width());
    destination.height = static_cast<std::uint32_t>(image.height());
    destination.format = snow::image::kRgba8;
    destination.row_stride = static_cast<std::size_t>(image.bytesPerLine());
    destination.pixels = std::span(reinterpret_cast<std::byte*>(image.bits()),
                                   static_cast<std::size_t>(image.sizeInBytes()));
    const snow::image::RasterRect region{static_cast<std::uint32_t>(tile.sourceRect.x()),
                                         static_cast<std::uint32_t>(tile.sourceRect.y()),
                                         static_cast<std::uint32_t>(tile.sourceRect.width()),
                                         static_cast<std::uint32_t>(tile.sourceRect.height())};
    snow::image::RasterConversionOptions conversion;
    conversion.output_alpha = snow::image::AlphaMode::premultiplied;
    const snow::image::Result<void> read =
        snow::image::read_rgba8_region(*store, 0, region, destination, conversion);
    if (!read) {
        return fail(QString::fromStdString(read.error().message));
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

} // namespace snow::image_viewer
