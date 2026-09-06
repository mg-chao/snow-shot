#pragma once

#include "core/image_types.h"

#include <QByteArray>
#include <QMetaType>
#include <QRect>
#include <QSize>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace snow::image_viewer {

enum class RasterProvenance : quint8 {
    cpu_reference,
    gpu_approximate,
    source_exact,
};

struct GpuRasterTile final {
    std::shared_ptr<QByteArray> storage;
    QRect pixelRect;
    std::size_t rowStride = 0;

    bool isValid(std::size_t bytesPerPixel) const {
        if (!storage || storage->isEmpty() || !pixelRect.isValid() || bytesPerPixel == 0 ||
            static_cast<std::size_t>(pixelRect.width()) >
                std::numeric_limits<std::size_t>::max() / bytesPerPixel) {
            return false;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(pixelRect.width()) * bytesPerPixel;
        if (rowStride < rowBytes || static_cast<std::size_t>(pixelRect.height()) >
                                        std::numeric_limits<std::size_t>::max() / rowStride) {
            return false;
        }
        const std::size_t required = rowStride * static_cast<std::size_t>(pixelRect.height());
        return required <= static_cast<std::size_t>(std::numeric_limits<qsizetype>::max()) &&
               storage->size() >= static_cast<qsizetype>(required);
    }
};

struct GpuRasterResult final {
    std::shared_ptr<QByteArray> storage;
    QSize pixelSize;
    std::size_t rowStride = 0;
    PixelEncoding encoding = PixelEncoding::Srgb8;
    ColorMetadata color;
    quint64 requestId = 0;
    RasterProvenance provenance = RasterProvenance::gpu_approximate;
    std::vector<GpuRasterTile> tiles;

    std::size_t bytesPerPixel() const {
        return encoding == PixelEncoding::LinearScRgb16F ? 8U : 4U;
    }

    bool isValid() const {
        if (!pixelSize.isValid())
            return false;
        const std::size_t pixelBytes = bytesPerPixel();
        if (storage) {
            if (storage->isEmpty() || rowStride == 0 ||
                static_cast<std::size_t>(pixelSize.width()) >
                    std::numeric_limits<std::size_t>::max() / pixelBytes) {
                return false;
            }
            const std::size_t rowBytes = static_cast<std::size_t>(pixelSize.width()) * pixelBytes;
            if (rowStride < rowBytes || static_cast<std::size_t>(pixelSize.height()) >
                                            std::numeric_limits<std::size_t>::max() / rowStride) {
                return false;
            }
            const std::size_t required = rowStride * static_cast<std::size_t>(pixelSize.height());
            return tiles.empty() &&
                   required <= static_cast<std::size_t>(std::numeric_limits<qsizetype>::max()) &&
                   storage->size() >= static_cast<qsizetype>(required);
        }
        if (tiles.empty() || rowStride != 0)
            return false;

        std::size_t index = 0;
        int expectedY = 0;
        while (expectedY < pixelSize.height()) {
            if (index >= tiles.size())
                return false;
            const int rowHeight = tiles[index].pixelRect.height();
            if (rowHeight <= 0 || rowHeight > pixelSize.height() - expectedY)
                return false;
            int expectedX = 0;
            while (expectedX < pixelSize.width()) {
                if (index >= tiles.size())
                    return false;
                const GpuRasterTile& tile = tiles[index++];
                if (tile.pixelRect.x() != expectedX || tile.pixelRect.y() != expectedY ||
                    tile.pixelRect.height() != rowHeight || tile.pixelRect.width() <= 0 ||
                    tile.pixelRect.width() > pixelSize.width() - expectedX ||
                    !tile.isValid(pixelBytes)) {
                    return false;
                }
                expectedX += tile.pixelRect.width();
            }
            expectedY += rowHeight;
        }
        return index == tiles.size();
    }

    std::uint64_t storageBytes() const {
        std::uint64_t total = storage ? static_cast<std::uint64_t>(storage->size()) : 0U;
        for (const GpuRasterTile& tile : tiles) {
            const std::uint64_t bytes =
                tile.storage ? static_cast<std::uint64_t>(tile.storage->size()) : 0U;
            if (bytes > std::numeric_limits<std::uint64_t>::max() - total)
                return std::numeric_limits<std::uint64_t>::max();
            total += bytes;
        }
        return total;
    }
};

} // namespace snow::image_viewer

Q_DECLARE_METATYPE(snow::image_viewer::GpuRasterResult)
