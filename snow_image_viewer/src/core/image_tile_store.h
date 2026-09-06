#pragma once

#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

namespace snow::image {
class RasterStore;
}

namespace snow::image_viewer {

class ImageRasterStore;

struct StoredImageTile final {
    QRect sourceRect;
};

class ImageTileStore final {
  public:
    ImageTileStore(QString filePath, std::shared_ptr<snow::image::RasterStore> rasterStore,
                   QSize sourceSize, QImage preview, QSize tileSize);
    ImageTileStore(std::shared_ptr<ImageRasterStore> rasterStore, QSize sourceSize, QImage preview,
                   QSize tileSize);
    ~ImageTileStore();

    ImageTileStore(const ImageTileStore&) = delete;
    ImageTileStore& operator=(const ImageTileStore&) = delete;

    QSize sourceSize() const noexcept;
    const QImage& preview() const noexcept;
    const std::shared_ptr<ImageRasterStore>& rasterStore() const noexcept;
    std::vector<StoredImageTile> tilesIntersecting(const QRectF& sourceRect) const;
    QImage load(const StoredImageTile& tile, QString* error = nullptr) const;

  private:
    std::shared_ptr<ImageRasterStore> rasterStore_;
    QSize sourceSize_;
    QImage preview_;
    QSize tileSize_;
};

} // namespace snow::image_viewer
