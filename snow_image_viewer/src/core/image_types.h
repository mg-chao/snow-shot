#pragma once

#include <QColorSpace>
#include <QImage>
#include <QMetaType>
#include <QSize>
#include <QString>
#include <QVector>

#include <snow/image/raster.h>

#include <memory>
#include <utility>

namespace snow::image_viewer {

class ImageTileStore;
class ImageRasterStore;

enum class DynamicRange {
    Standard,
    High,
};

enum class PixelEncoding {
    Srgb8,
    LinearScRgb16F,
};

struct ColorMetadata {
    QColorSpace sourceColorSpace;
    QString description;
    QString transferDescription;
    DynamicRange dynamicRange = DynamicRange::Standard;
    float sourcePeakNits = 0.0F;
    float diffuseWhiteNits = 80.0F;
    float linearScaleToScRgb = 1.0F;
    bool usedFallbackProfile = false;
    bool hasGainMap = false;
};

struct DecodedAnimationFrame final {
    QImage pixels;
    qint64 durationMilliseconds = 0;
};

struct DecodedImage {
    QString filePath;
    QSize sourceSize;
    QImage pixels;
    PixelEncoding pixelEncoding = PixelEncoding::Srgb8;
    bool pixelsPremultiplied = true;
    ColorMetadata color;
    QString decoderName;
    snow::image::RasterAnalysis analysis;
    QVector<DecodedAnimationFrame> animationFrames;
    std::shared_ptr<ImageTileStore> tileStore;
    std::shared_ptr<ImageRasterStore> rasterStore;
    int loopCount = 1;

    // A decoded image remains a valid viewer document after an ordinary static
    // image has transferred its pixel payload to the GPU.
    bool isValid() const {
        return !filePath.isEmpty() && sourceSize.isValid();
    }
    bool hasCpuPixels() const {
        return !pixels.isNull();
    }
    bool usesStoredTiles() const {
        return tileStore != nullptr;
    }
    bool isAnimated() const {
        return animationFrames.size() > 1;
    }
};

struct ImageThumbnail {
    QString filePath;
    QSize sourceSize;
    QImage pixels;

    bool isValid() const {
        return !filePath.isEmpty() && sourceSize.isValid() && !pixels.isNull();
    }
};

struct DecodeResult {
    DecodedImage image;
    QString error;

    bool succeeded() const {
        return error.isEmpty() && image.isValid() && image.hasCpuPixels();
    }

    static DecodeResult failure(QString message) {
        DecodeResult result;
        result.error = std::move(message);
        return result;
    }
};

bool isHdrColorSpace(const QColorSpace& colorSpace);
QString describeColorSpace(const QColorSpace& colorSpace);
QString describeTransferFunction(const QColorSpace& colorSpace);
DecodeResult prepareDecodedImage(const QString& filePath, QImage source, ColorMetadata metadata,
                                 const QString& decoderName,
                                 snow::image::RasterAnalysis analysis = {});

} // namespace snow::image_viewer

Q_DECLARE_METATYPE(snow::image_viewer::DecodedImage)
Q_DECLARE_METATYPE(snow::image_viewer::ImageThumbnail)
Q_DECLARE_METATYPE(snow::image_viewer::DecodeResult)
