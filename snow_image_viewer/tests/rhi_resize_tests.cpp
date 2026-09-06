#include "render/rhi_image_window.h"
#include "core/image_raster_store.h"
#include "core/image_tile_store.h"

#include <snow/image/service.h>
#include <snow/image/raster_conversion.h>

#include <QApplication>
#include <QColorSpace>
#include <QImage>
#include <QDir>
#include <QTimer>
#include <QUuid>
#include <QtCore/qfloat16.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

namespace {

std::array<unsigned char, 4> sourcePixel(int x, int y) {
    return {static_cast<unsigned char>((x & 1) != 0 ? 0xFF : 0x00),
            static_cast<unsigned char>(x % 3 != 0 ? 0xFF : 0x00),
            static_cast<unsigned char>((y * 37) & 0xFF), 0xFF};
}

float srgbToLinear(float value) {
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

float linearToSrgb(float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    return value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

std::array<float, 4> decodePixel(const std::array<unsigned char, 4>& pixel) {
    return {srgbToLinear(static_cast<float>(pixel[0]) / 255.0F),
            srgbToLinear(static_cast<float>(pixel[1]) / 255.0F),
            srgbToLinear(static_cast<float>(pixel[2]) / 255.0F),
            static_cast<float>(pixel[3]) / 255.0F};
}

std::array<unsigned char, 4> encodePixel(const std::array<float, 4>& pixel) {
    std::array<unsigned char, 4> encoded{};
    for (int channel = 0; channel < 3; ++channel) {
        encoded[channel] =
            static_cast<unsigned char>(std::lround(linearToSrgb(pixel[channel]) * 255.0F));
    }
    encoded[3] = static_cast<unsigned char>(std::lround(std::clamp(pixel[3], 0.0F, 1.0F) * 255.0F));
    return encoded;
}

float lanczos3(float value) {
    value = std::abs(value);
    if (value >= 3.0F)
        return 0.0F;
    const auto sinc = [](float argument) {
        if (std::abs(argument) < 0.00001F)
            return 1.0F;
        constexpr float kPi = 3.14159265359F;
        const float scaled = kPi * argument;
        return std::sin(scaled) / scaled;
    };
    return sinc(value) * sinc(value / 3.0F);
}

template <typename Fetch>
std::array<float, 4> filterAxis(int outputCoordinate, int outputExtent, int sourceExtent,
                                Fetch&& fetch) {
    const float position = (static_cast<float>(outputCoordinate) + 0.5F) *
                               static_cast<float>(sourceExtent) / static_cast<float>(outputExtent) -
                           0.5F;
    const int base = static_cast<int>(std::floor(position));
    const float fraction = position - static_cast<float>(base);
    std::array<float, 4> result{};
    float totalWeight = 0.0F;
    for (int tap = -2; tap <= 3; ++tap) {
        const float weight = lanczos3(static_cast<float>(tap) - fraction);
        const auto sample = fetch(std::clamp(base + tap, 0, sourceExtent - 1));
        for (int channel = 0; channel < 4; ++channel)
            result[channel] += sample[channel] * weight;
        totalWeight += weight;
    }
    for (float& channel : result)
        channel /= totalWeight;
    return result;
}

std::array<unsigned char, 4> linearReferencePixel(int outputX, int outputY, const QSize& outputSize,
                                                  const QSize& sourceSize) {
    const float sourceX = (static_cast<float>(outputX) + 0.5F) *
                              static_cast<float>(sourceSize.width()) /
                              static_cast<float>(outputSize.width()) -
                          0.5F;
    const float sourceY = (static_cast<float>(outputY) + 0.5F) *
                              static_cast<float>(sourceSize.height()) /
                              static_cast<float>(outputSize.height()) -
                          0.5F;
    const int baseX = static_cast<int>(std::floor(sourceX));
    const int baseY = static_cast<int>(std::floor(sourceY));
    const float fractionX = sourceX - static_cast<float>(baseX);
    const float fractionY = sourceY - static_cast<float>(baseY);
    const auto sample = [&](int x, int y) {
        return decodePixel(sourcePixel(std::clamp(x, 0, sourceSize.width() - 1),
                                       std::clamp(y, 0, sourceSize.height() - 1)));
    };
    const auto topLeft = sample(baseX, baseY);
    const auto topRight = sample(baseX + 1, baseY);
    const auto bottomLeft = sample(baseX, baseY + 1);
    const auto bottomRight = sample(baseX + 1, baseY + 1);
    std::array<float, 4> result{};
    for (int channel = 0; channel < 4; ++channel) {
        const float top = std::lerp(topLeft[channel], topRight[channel], fractionX);
        const float bottom = std::lerp(bottomLeft[channel], bottomRight[channel], fractionX);
        result[channel] = std::lerp(top, bottom, fractionY);
    }
    return encodePixel(result);
}

std::array<unsigned char, 4>
lanczosReferencePixel(int outputX, int outputY, const QSize& outputSize, const QSize& sourceSize) {
    // The GPU uses an RGBA8 sRGB intermediate between its horizontal and
    // vertical passes. Quantize that intermediate here to match the real path.
    std::array<std::array<unsigned char, 4>, 6> intermediate{};
    const float sourceY = (static_cast<float>(outputY) + 0.5F) *
                              static_cast<float>(sourceSize.height()) /
                              static_cast<float>(outputSize.height()) -
                          0.5F;
    const int baseY = static_cast<int>(std::floor(sourceY));
    std::size_t tapIndex = 0;
    for (int tap = -2; tap <= 3; ++tap, ++tapIndex) {
        const int y = std::clamp(baseY + tap, 0, sourceSize.height() - 1);
        intermediate[tapIndex] =
            encodePixel(filterAxis(outputX, outputSize.width(), sourceSize.width(),
                                   [&](int x) { return decodePixel(sourcePixel(x, y)); }));
    }
    const float fractionY = sourceY - static_cast<float>(baseY);
    std::array<float, 4> result{};
    float totalWeight = 0.0F;
    tapIndex = 0;
    for (int tap = -2; tap <= 3; ++tap, ++tapIndex) {
        const float weight = lanczos3(static_cast<float>(tap) - fractionY);
        const auto sample = decodePixel(intermediate[tapIndex]);
        for (int channel = 0; channel < 4; ++channel)
            result[channel] += sample[channel] * weight;
        totalWeight += weight;
    }
    for (float& channel : result)
        channel /= totalWeight;
    return encodePixel(result);
}

bool readPixel(const snow::image_viewer::GpuRasterResult& readback, int x, int y,
               std::array<unsigned char, 4>* pixel) {
    if (!pixel)
        return false;
    if (readback.storage) {
        const std::size_t offset = static_cast<std::size_t>(y) * readback.rowStride +
                                   static_cast<std::size_t>(x) * pixel->size();
        std::memcpy(pixel->data(), readback.storage->constData() + offset, pixel->size());
        return true;
    }
    for (const snow::image_viewer::GpuRasterTile& tile : readback.tiles) {
        if (!tile.pixelRect.contains(x, y))
            continue;
        const std::size_t offset =
            static_cast<std::size_t>(y - tile.pixelRect.y()) * tile.rowStride +
            static_cast<std::size_t>(x - tile.pixelRect.x()) * pixel->size();
        std::memcpy(pixel->data(), tile.storage->constData() + offset, pixel->size());
        return true;
    }
    return false;
}

bool readHalfPixel(const snow::image_viewer::GpuRasterResult& readback, int x, int y,
                   std::array<qfloat16, 4>* pixel) {
    if (!pixel)
        return false;
    const auto copyPixel = [pixel](const QByteArray& storage, std::size_t rowStride, int localX,
                                   int localY) {
        const std::size_t offset = static_cast<std::size_t>(localY) * rowStride +
                                   static_cast<std::size_t>(localX) * sizeof(*pixel);
        if (offset > static_cast<std::size_t>(storage.size()) ||
            sizeof(*pixel) > static_cast<std::size_t>(storage.size()) - offset) {
            return false;
        }
        std::memcpy(pixel->data(), storage.constData() + offset, sizeof(*pixel));
        return true;
    };
    if (readback.storage) {
        return copyPixel(*readback.storage, readback.rowStride, x, y);
    }
    for (const snow::image_viewer::GpuRasterTile& tile : readback.tiles) {
        if (!tile.pixelRect.contains(x, y))
            continue;
        return copyPixel(*tile.storage, tile.rowStride, x - tile.pixelRect.x(),
                         y - tile.pixelRect.y());
    }
    return false;
}

bool verifyNearestPixels(const snow::image_viewer::GpuRasterResult& readback,
                         const QSize& outputSize, const QSize& sourceSize, QString* error) {
    if (!readback.isValid() || readback.pixelSize != outputSize) {
        if (error)
            *error = QStringLiteral("QRhi returned an invalid resize result.");
        return false;
    }
    for (int y = 0; y < outputSize.height(); ++y) {
        const int sourceY =
            std::min(sourceSize.height() - 1,
                     static_cast<int>((static_cast<std::int64_t>(2 * y + 1) * sourceSize.height()) /
                                      (static_cast<std::int64_t>(2) * outputSize.height())));
        for (int x = 0; x < outputSize.width(); ++x) {
            const int sourceX = std::min(
                sourceSize.width() - 1,
                static_cast<int>((static_cast<std::int64_t>(2) * x + 1) * sourceSize.width() /
                                 (static_cast<std::int64_t>(2) * outputSize.width())));
            std::array<unsigned char, 4> actual{};
            if (!readPixel(readback, x, y, &actual) || actual != sourcePixel(sourceX, sourceY)) {
                if (error) {
                    const auto expected = sourcePixel(sourceX, sourceY);
                    *error =
                        QStringLiteral("QRhi nearest sampling disagrees at output pixel (%1, %2): "
                                       "actual %3/%4/%5/%6, expected %7/%8/%9/%10 from %11/%12.")
                            .arg(x)
                            .arg(y)
                            .arg(actual[0])
                            .arg(actual[1])
                            .arg(actual[2])
                            .arg(actual[3])
                            .arg(expected[0])
                            .arg(expected[1])
                            .arg(expected[2])
                            .arg(expected[3])
                            .arg(sourceX)
                            .arg(sourceY);
                }
                return false;
            }
        }
    }
    return true;
}

bool verifyNearestHalfPixels(const snow::image_viewer::GpuRasterResult& readback,
                             const QSize& outputSize, const QSize& sourceSize, QString* error) {
    if (!readback.isValid() || readback.pixelSize != outputSize ||
        readback.encoding != snow::image_viewer::PixelEncoding::LinearScRgb16F) {
        if (error)
            *error = QStringLiteral("QRhi returned an invalid RGBA16F resize result.");
        return false;
    }
    for (int y = 0; y < outputSize.height(); ++y) {
        const int sourceY =
            std::min(sourceSize.height() - 1,
                     static_cast<int>((static_cast<std::int64_t>(2 * y + 1) * sourceSize.height()) /
                                      (static_cast<std::int64_t>(2) * outputSize.height())));
        for (int x = 0; x < outputSize.width(); ++x) {
            const int sourceX = std::min(
                sourceSize.width() - 1,
                static_cast<int>((static_cast<std::int64_t>(2) * x + 1) * sourceSize.width() /
                                 (static_cast<std::int64_t>(2) * outputSize.width())));
            const auto expected = sourcePixel(sourceX, sourceY);
            std::array<qfloat16, 4> actual{};
            if (!readHalfPixel(readback, x, y, &actual)) {
                if (error)
                    *error = QStringLiteral("An RGBA16F QRhi pixel is missing.");
                return false;
            }
            for (int channel = 0; channel < 4; ++channel) {
                const float expectedValue = static_cast<float>(expected[channel]) / 255.0F;
                const qfloat16 expectedHalf(expectedValue);
                if (actual[channel] == expectedHalf) {
                    continue;
                }
                if (error) {
                    *error = QStringLiteral("QRhi RGBA16F nearest sampling disagrees at (%1, %2), "
                                            "channel %3: actual %4, expected %5.")
                                 .arg(x)
                                 .arg(y)
                                 .arg(channel)
                                 .arg(static_cast<float>(actual[channel]))
                                 .arg(expectedValue);
                }
                return false;
            }
        }
    }
    return true;
}

bool verifyNearestReadback(const snow::image_viewer::GpuRasterResult& readback, int textureLimit,
                           const QSize& sourceSize, QString* error) {
    const int width = textureLimit + 1;
    if (!readback.isValid() || readback.pixelSize != QSize(width, 3) ||
        readback.tiles.size() != 2 || readback.tiles[0].pixelRect != QRect(0, 0, textureLimit, 3) ||
        readback.tiles[1].pixelRect != QRect(textureLimit, 0, 1, 3)) {
        if (error)
            *error = QStringLiteral("QRhi returned an invalid tiled partition.");
        return false;
    }
    const std::array<int, 4> sampleX{0, textureLimit - 1, textureLimit, width - 1};
    for (int y = 0; y < 3; ++y) {
        const int sourceY = std::min(
            sourceSize.height() - 1,
            static_cast<int>((static_cast<std::int64_t>(2 * y + 1) * sourceSize.height()) / 6));
        for (const int x : sampleX) {
            const int sourceX = std::min(
                sourceSize.width() - 1,
                static_cast<int>((static_cast<std::int64_t>(2) * x + 1) * sourceSize.width() /
                                 (static_cast<std::int64_t>(2) * width)));
            std::array<unsigned char, 4> actual{};
            if (!readPixel(readback, x, y, &actual) || actual != sourcePixel(sourceX, sourceY)) {
                if (error) {
                    const auto expected = sourcePixel(sourceX, sourceY);
                    *error =
                        QStringLiteral("QRhi nearest sampling disagrees at output pixel (%1, %2): "
                                       "actual %3/%4/%5/%6, expected %7/%8/%9/%10 from %11/%12.")
                            .arg(x)
                            .arg(y)
                            .arg(actual[0])
                            .arg(actual[1])
                            .arg(actual[2])
                            .arg(actual[3])
                            .arg(expected[0])
                            .arg(expected[1])
                            .arg(expected[2])
                            .arg(expected[3])
                            .arg(sourceX)
                            .arg(sourceY);
                }
                return false;
            }
        }
    }
    return true;
}

bool verifyFilteredPixels(const snow::image_viewer::GpuRasterResult& readback,
                          snow::image::ResamplingMethod method, const QSize& outputSize,
                          const QSize& sourceSize, QString* error) {
    if (!readback.isValid() || readback.pixelSize != outputSize) {
        if (error)
            *error = QStringLiteral("QRhi returned an invalid filtered result.");
        return false;
    }
    constexpr int kTolerance = 4;
    for (int y = 0; y < outputSize.height(); ++y) {
        for (int x = 0; x < outputSize.width(); ++x) {
            std::array<unsigned char, 4> actual{};
            if (!readPixel(readback, x, y, &actual)) {
                if (error)
                    *error = QStringLiteral("A filtered QRhi pixel is missing.");
                return false;
            }
            const auto expected = method == snow::image::ResamplingMethod::linear
                                      ? linearReferencePixel(x, y, outputSize, sourceSize)
                                      : lanczosReferencePixel(x, y, outputSize, sourceSize);
            for (int channel = 0; channel < 4; ++channel) {
                if (std::abs(static_cast<int>(actual[channel]) -
                             static_cast<int>(expected[channel])) <= kTolerance) {
                    continue;
                }
                if (error) {
                    QString neighborhood;
                    for (int sampleX = std::max(0, x - 2);
                         sampleX <= std::min(outputSize.width() - 1, x + 2); ++sampleX) {
                        std::array<unsigned char, 4> sample{};
                        readPixel(readback, sampleX, y, &sample);
                        neighborhood += QStringLiteral(" %1:%2/%3/%4")
                                            .arg(sampleX)
                                            .arg(sample[0])
                                            .arg(sample[1])
                                            .arg(sample[2]);
                    }
                    *error = QStringLiteral("QRhi %1 sampling disagrees at (%2, %3), channel %4: "
                                            "actual %5/%6/%7/%8, expected %9/%10/%11/%12; row:%13.")
                                 .arg(method == snow::image::ResamplingMethod::linear
                                          ? QStringLiteral("linear")
                                          : QStringLiteral("Lanczos3"))
                                 .arg(x)
                                 .arg(y)
                                 .arg(channel)
                                 .arg(actual[0])
                                 .arg(actual[1])
                                 .arg(actual[2])
                                 .arg(actual[3])
                                 .arg(expected[0])
                                 .arg(expected[1])
                                 .arg(expected[2])
                                 .arg(expected[3])
                                 .arg(neighborhood);
                }
                return false;
            }
        }
    }
    return true;
}

snow::image_viewer::EditExportSettings resizeSettings(const QSize& sourceSize,
                                                      const QSize& targetSize,
                                                      snow::image::ResamplingMethod resampling) {
    snow::image_viewer::EditExportSettings settings;
    settings.sourceSize = sourceSize;
    settings.width = targetSize.width();
    settings.height = targetSize.height();
    settings.resampling = resampling;
    settings.premultiplyAlpha = false;
    settings.linearRgb = true;
    settings.format = snow::image::Format::png;
    settings.encode.format = settings.format;
    return settings;
}

snow::image_viewer::DecodedImage testImage(const QSize& size, const QString& path) {
    QImage pixels(size, QImage::Format_RGBA8888);
    for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x) {
            const auto value = sourcePixel(x, y);
            std::memcpy(pixels.scanLine(y) + static_cast<std::size_t>(x) * value.size(),
                        value.data(), value.size());
        }
    }
    pixels.setColorSpace(QColorSpace(QColorSpace::SRgb));
    snow::image_viewer::DecodedImage image;
    image.filePath = path;
    image.sourceSize = size;
    image.pixels = std::move(pixels);
    image.pixelEncoding = snow::image_viewer::PixelEncoding::Srgb8;
    image.pixelsPremultiplied = false;
    image.color.sourceColorSpace = QColorSpace(QColorSpace::SRgb);
    return image;
}

snow::image_viewer::DecodedImage testHalfImage(const QSize& size, const QString& path) {
    QImage pixels(size, QImage::Format_RGBA16FPx4);
    for (int y = 0; y < pixels.height(); ++y) {
        auto* row = reinterpret_cast<qfloat16*>(pixels.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            const auto value = sourcePixel(x, y);
            for (int channel = 0; channel < 4; ++channel) {
                row[static_cast<std::size_t>(x) * 4U + channel] =
                    qfloat16(static_cast<float>(value[channel]) / 255.0F);
            }
        }
    }
    pixels.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    snow::image_viewer::DecodedImage image;
    image.filePath = path;
    image.sourceSize = size;
    image.pixels = std::move(pixels);
    image.pixelEncoding = snow::image_viewer::PixelEncoding::LinearScRgb16F;
    image.pixelsPremultiplied = false;
    image.color.sourceColorSpace = QColorSpace(QColorSpace::SRgbLinear);
    return image;
}

snow::image_viewer::DecodedImage nativeJpegComparison(QString* error, bool* pixelsAgree) {
    constexpr QSize kSize(513, 257);
    auto allocated =
        snow::image::MutableImage::allocate(kSize.width(), kSize.height(), snow::image::kRgb8);
    if (!allocated) {
        if (error)
            *error = QString::fromStdString(allocated.error().message);
        return {};
    }
    snow::image::MutableImage pixels = std::move(allocated).value();
    for (int y = 0; y < kSize.height(); ++y) {
        for (int x = 0; x < kSize.width(); ++x) {
            std::byte* pixel = pixels.pixels().data() +
                               static_cast<std::size_t>(y) * pixels.row_stride() +
                               static_cast<std::size_t>(x) * 3U;
            pixel[0] = static_cast<std::byte>((x * 17 + y * 3) & 0xFF);
            pixel[1] = static_cast<std::byte>((x * 5 + y * 19) & 0xFF);
            pixel[2] = static_cast<std::byte>((x * 11 + y * 7) & 0xFF);
        }
    }
    snow::image::Document source;
    source.canvas_width = kSize.width();
    source.canvas_height = kSize.height();
    snow::image::Frame frame;
    frame.image = std::move(pixels).freeze();
    source.frames.push_back(std::move(frame));
    auto bytes = std::make_shared<std::vector<std::byte>>();
    snow::image::EncodeOptions encode;
    encode.format = snow::image::Format::jpeg;
    encode.quality = 75;
    encode.progressive = true;
    encode.chroma_subsampling = snow::image::ChromaSubsampling::yuv420;
    snow::image::Service service;
    auto encoded = service.encode(source, snow::image::memory_output(bytes), encode);
    if (!encoded) {
        if (error)
            *error = QString::fromStdString(encoded.error().message);
        return {};
    }
    const QString path = QDir::temp().filePath(
        QStringLiteral("snow-rhi-native-%1.srs").arg(QUuid::createUuid().toString(QUuid::Id128)));
    snow::image::DecodeOptions decode;
    decode.raster_layout = snow::image::RasterLayoutPolicy::native;
    snow::image::RasterStoreOptions storeOptions;
    storeOptions.analysis.alpha_content = snow::image::AlphaContent::opaque;
    auto stored =
        service.decode_to_store(snow::image::memory_input(bytes),
                                std::filesystem::path(path.toStdWString()), decode, storeOptions);
    if (!stored) {
        if (error)
            *error = QString::fromStdString(stored.error().message);
        return {};
    }
    std::vector<std::byte> referencePixels(static_cast<std::size_t>(kSize.width()) *
                                           kSize.height() * 4U);
    snow::image::MutablePlaneView reference{
        static_cast<std::uint32_t>(kSize.width()), static_cast<std::uint32_t>(kSize.height()),
        snow::image::kRgba8, static_cast<std::size_t>(kSize.width()) * 4U, referencePixels};
    auto convertedReference =
        snow::image::read_rgba8_region(*stored.value(), 0,
                                       {0, 0, static_cast<std::uint32_t>(kSize.width()),
                                        static_cast<std::uint32_t>(kSize.height())},
                                       reference);
    if (!convertedReference) {
        if (error)
            *error = QString::fromStdString(convertedReference.error().message);
        return {};
    }
    auto backing = snow::image_viewer::ImageRasterStore::adopt(path, std::move(stored).value());
    auto tiles = std::make_shared<snow::image_viewer::ImageTileStore>(backing, kSize, QImage{},
                                                                      QSize(256, 128));
    bool agreement = true;
    int maximumDelta = 0;
    const auto storedTiles = tiles->tilesIntersecting(QRectF(QPointF(0.0, 0.0), kSize));
    for (const auto& tile : storedTiles) {
        QString tileError;
        const QImage converted = tiles->load(tile, &tileError);
        if (converted.isNull()) {
            if (error)
                *error = tileError;
            return {};
        }
        for (int y = 0; agreement && y < converted.height(); ++y) {
            for (int x = 0; x < converted.width(); ++x) {
                const auto* actual = converted.constScanLine(y) + x * 4;
                const std::size_t referenceOffset =
                    static_cast<std::size_t>(tile.sourceRect.y() + y) * reference.row_stride +
                    static_cast<std::size_t>(tile.sourceRect.x() + x) * 4U;
                for (int channel = 0; channel < 4; ++channel) {
                    const unsigned expected = std::to_integer<unsigned>(
                        referencePixels[referenceOffset + static_cast<std::size_t>(channel)]);
                    const int delta =
                        std::abs(static_cast<int>(actual[channel]) - static_cast<int>(expected));
                    maximumDelta = std::max(maximumDelta, delta);
                    if (delta > 1) {
                        agreement = false;
                        break;
                    }
                }
            }
        }
    }
    if (!agreement && error) {
        *error =
            QStringLiteral("Native tile conversion differs by %1 code values.").arg(maximumDelta);
    }
    if (pixelsAgree)
        *pixelsAgree = agreement && storedTiles.size() == 9;
    snow::image_viewer::DecodedImage result;
    result.filePath = QStringLiteral("rhi-native-comparison.jpg");
    result.sourceSize = kSize;
    result.pixelEncoding = snow::image_viewer::PixelEncoding::Srgb8;
    result.pixelsPremultiplied = true;
    result.color.sourceColorSpace = QColorSpace(QColorSpace::SRgb);
    result.rasterStore = std::move(backing);
    result.tileStore = std::move(tiles);
    return result;
}

} // namespace

int main(int argc, char** argv) {
    snow::image_viewer::RhiBackend backend = snow::image_viewer::RhiBackend::platform_default;
    bool allowUnavailable = false;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--allow-unavailable")) {
            allowUnavailable = true;
        } else if (argument == QStringLiteral("--backend=d3d11")) {
            backend = snow::image_viewer::RhiBackend::d3d11;
        } else if (argument == QStringLiteral("--backend=opengl")) {
            backend = snow::image_viewer::RhiBackend::opengl;
        } else if (argument == QStringLiteral("--backend=vulkan")) {
            backend = snow::image_viewer::RhiBackend::vulkan;
        }
    }
    QApplication application(argc, argv);
    snow::image_viewer::RhiImageWindow window(nullptr, backend);
    window.resize(320, 200);

    constexpr QSize kSourceSize(64, 8);
    auto image = testImage(kSourceSize, QStringLiteral("rhi-tiled-resize-fixture.png"));

    QString failure;
    bool nativeComparisonPixelsAgree = false;
    auto nativeComparison = nativeJpegComparison(&failure, &nativeComparisonPixelsAgree);
    if (!failure.isEmpty() || !nativeComparison.isValid() || !nativeComparisonPixelsAgree) {
        std::cerr << "FAILED: native JPEG comparison fixture: " << failure.toStdString() << '\n';
        return 1;
    }
    bool backendUnavailable = false;
    int textureLimit = 0;
    int completedRequests = 0;
    bool secondRequestCacheHit = false;
    bool cancelledJobSubmitted = false;
    bool virtualSourceResident = false;
    bool virtualResizeVerified = false;
    bool virtualLinearVerified = false;
    bool virtualLanczosAxisVerified = false;
    bool virtualLanczosVerified = false;
    bool virtualTiledTargetVerified = false;
    bool virtualCancellationSubmitted = false;
    bool virtualVisualSubmitted = false;
    bool virtualReadbackVerified = false;
    bool virtualHalfResident = false;
    bool virtualHalfVerified = false;
    int nativeComparisonFrames = 0;

    QObject::connect(&window, &snow::image_viewer::RhiImageWindow::renderError, &application,
                     [&](const QString& message) {
                         if (allowUnavailable &&
                             window.backendName() == QStringLiteral("uninitialized")) {
                             backendUnavailable = true;
                         }
                         if (failure.isEmpty())
                             failure = message;
                         QApplication::quit();
                     });
    QObject::connect(&window, &snow::image_viewer::RhiImageWindow::gpuOperationFailed, &application,
                     [&](quint64, const QString& message) {
                         if (failure.isEmpty())
                             failure = message;
                         QApplication::quit();
                     });
    QObject::connect(
        &window, &snow::image_viewer::RhiImageWindow::staticTextureResident, &application,
        [&](const QString& path) {
            if (path == QStringLiteral("rhi-virtual-half-source-fixture.exr")) {
                virtualHalfResident =
                    window.sourceTextureCount() > 1 && window.hasGpuOnlyStaticImage();
                window.requestEditResize(
                    13, resizeSettings(QSize(textureLimit + 1, 8), QSize(textureLimit + 1, 3),
                                       snow::image::ResamplingMethod::nearest));
                return;
            }
            if (path == QStringLiteral("rhi-native-comparison-base.png"))
                return;
            if (path == QStringLiteral("rhi-virtual-source-fixture.png")) {
                virtualSourceResident =
                    window.sourceTextureCount() > 1 && window.hasGpuOnlyStaticImage();
                const auto settings = resizeSettings(QSize(textureLimit + 1, 8), QSize(257, 3),
                                                     snow::image::ResamplingMethod::nearest);
                window.requestEditResize(5, settings);
                return;
            }
            textureLimit = window.maximumTextureSize();
            if (textureLimit <= 1 || textureLimit > (std::numeric_limits<int>::max() - 1) / 2) {
                failure = QStringLiteral("QRhi reported an unusable texture limit.");
                QApplication::quit();
                return;
            }
            const auto settings = resizeSettings(kSourceSize, QSize(textureLimit + 1, 3),
                                                 snow::image::ResamplingMethod::nearest);
            if (!window.canGpuResize(QSize(settings.width, settings.height))) {
                failure = QStringLiteral("QRhi rejected a narrow target larger than one texture.");
                QApplication::quit();
                return;
            }
            window.requestEditResize(1, settings);
        });
    QObject::connect(&window, &snow::image_viewer::RhiImageWindow::editResizeResourceCacheResult,
                     &application, [&](quint64 requestId, bool cacheHit) {
                         if (requestId == 2)
                             secondRequestCacheHit = cacheHit;
                     });
    QObject::connect(&window, &snow::image_viewer::RhiImageWindow::editPerformanceStageCompleted,
                     &application, [&](quint64 requestId, const QString& stage, qint64) {
                         if ((requestId != 3 && requestId != 10) ||
                             stage != QStringLiteral("exact.gpu_submission")) {
                             return;
                         }
                         if (requestId == 3)
                             cancelledJobSubmitted = true;
                         else
                             virtualCancellationSubmitted = true;
                         window.cancelEditRequests();
                         QTimer::singleShot(100, &window, [&, requestId]() {
                             if (requestId == 3) {
                                 window.requestEditResize(
                                     4, resizeSettings(kSourceSize, QSize(textureLimit + 1, 3),
                                                       snow::image::ResamplingMethod::nearest));
                             } else {
                                 window.requestEditVisual(
                                     11, resizeSettings(QSize(textureLimit + 1, 8), QSize(257, 3),
                                                        snow::image::ResamplingMethod::lanczos3));
                             }
                         });
                     });
    QObject::connect(&window, &snow::image_viewer::RhiImageWindow::editVisualFrameSubmitted,
                     &application, [&](quint64 requestId) {
                         if (requestId != 11)
                             return;
                         virtualVisualSubmitted = true;
                         window.requestMainImageReadback(6);
                     });
    QObject::connect(
        &window, &snow::image_viewer::RhiImageWindow::editResizeReadbackReady, &application,
        [&](quint64 requestId, const snow::image_viewer::GpuRasterResult& readback) {
            if (requestId == 3 || requestId == 10) {
                failure = QStringLiteral("A cancelled QRhi resize was published.");
                QApplication::quit();
                return;
            }
            bool validReadback = false;
            if (requestId == 13) {
                validReadback = verifyNearestHalfPixels(readback, QSize(textureLimit + 1, 3),
                                                        QSize(textureLimit + 1, 8), &failure);
            } else if (requestId == 5) {
                validReadback = verifyNearestPixels(readback, QSize(257, 3),
                                                    QSize(textureLimit + 1, 8), &failure);
            } else if (requestId == 7 || requestId == 8 || requestId == 12) {
                validReadback =
                    verifyFilteredPixels(readback,
                                         requestId == 7 ? snow::image::ResamplingMethod::linear
                                                        : snow::image::ResamplingMethod::lanczos3,
                                         requestId == 8 ? QSize(257, 8) : QSize(257, 3),
                                         QSize(textureLimit + 1, 8), &failure);
            } else if (requestId == 9) {
                validReadback = verifyNearestReadback(readback, textureLimit,
                                                      QSize(textureLimit + 1, 8), &failure);
            } else {
                validReadback =
                    verifyNearestReadback(readback, textureLimit, kSourceSize, &failure);
            }
            if (!validReadback) {
                QApplication::quit();
                return;
            }
            ++completedRequests;
            if (requestId == 1) {
                window.requestEditResize(2, resizeSettings(kSourceSize, QSize(textureLimit + 1, 3),
                                                           snow::image::ResamplingMethod::nearest));
            } else if (requestId == 2) {
                window.requestEditResize(3,
                                         resizeSettings(kSourceSize, QSize(textureLimit * 2 + 1, 3),
                                                        snow::image::ResamplingMethod::nearest));
            } else if (requestId == 4) {
                window.setImage(testImage(QSize(textureLimit + 1, 8),
                                          QStringLiteral("rhi-virtual-source-fixture.png")));
            } else if (requestId == 5) {
                virtualResizeVerified = true;
                window.requestEditResize(7,
                                         resizeSettings(QSize(textureLimit + 1, 8), QSize(257, 3),
                                                        snow::image::ResamplingMethod::linear));
            } else if (requestId == 7) {
                virtualLinearVerified = true;
                window.requestEditResize(8,
                                         resizeSettings(QSize(textureLimit + 1, 8), QSize(257, 8),
                                                        snow::image::ResamplingMethod::lanczos3));
            } else if (requestId == 8) {
                virtualLanczosAxisVerified = true;
                window.requestEditResize(12,
                                         resizeSettings(QSize(textureLimit + 1, 8), QSize(257, 3),
                                                        snow::image::ResamplingMethod::lanczos3));
            } else if (requestId == 12) {
                virtualLanczosVerified = true;
                window.requestEditResize(9, resizeSettings(QSize(textureLimit + 1, 8),
                                                           QSize(textureLimit + 1, 3),
                                                           snow::image::ResamplingMethod::nearest));
            } else if (requestId == 9) {
                virtualTiledTargetVerified = true;
                window.requestEditResize(
                    10, resizeSettings(QSize(textureLimit + 1, 8), QSize(textureLimit * 2 + 1, 3),
                                       snow::image::ResamplingMethod::nearest));
            } else if (requestId == 13) {
                virtualHalfVerified = true;
                window.setImage(testImage(nativeComparison.sourceSize,
                                          QStringLiteral("rhi-native-comparison-base.png")));
                window.setComparisonImage(nativeComparison);
                QTimer::singleShot(0, &window,
                                   [&]() { window.setComparisonImage(nativeComparison); });
            }
        });
    QObject::connect(&window, &snow::image_viewer::RhiImageWindow::comparisonFrameSubmitted,
                     &application, [&](const QString& path) {
                         if (path != nativeComparison.filePath)
                             return;
                         ++nativeComparisonFrames;
                         if (nativeComparisonFrames == 1) {
                             window.zoomBy(4.0);
                             window.setComparisonImage(nativeComparison);
                         } else {
                             QApplication::quit();
                         }
                     });
    QObject::connect(&window, &snow::image_viewer::RhiImageWindow::mainImageReadbackReady,
                     &application, [&](quint64 requestId, const QImage& readback) {
                         if (requestId != 6)
                             return;
                         virtualReadbackVerified = readback.size() == QSize(textureLimit + 1, 8) &&
                                                   readback.format() == QImage::Format_RGBA8888;
                         for (int y = 0; virtualReadbackVerified && y < readback.height(); ++y) {
                             for (int x = 0; x < readback.width(); ++x) {
                                 std::array<unsigned char, 4> actual{};
                                 std::memcpy(actual.data(),
                                             readback.constScanLine(y) +
                                                 static_cast<std::size_t>(x) * actual.size(),
                                             actual.size());
                                 if (actual != sourcePixel(x, y)) {
                                     virtualReadbackVerified = false;
                                     break;
                                 }
                             }
                         }
                         window.clearComparison();
                         window.setImage(
                             testHalfImage(QSize(textureLimit + 1, 8),
                                           QStringLiteral("rhi-virtual-half-source-fixture.exr")));
                     });

    QTimer::singleShot(15'000, &application, [&]() {
        if (failure.isEmpty())
            failure = QStringLiteral("QRhi tiled resize timed out.");
        QApplication::quit();
    });
    window.setImage(image);
    window.show();
    const int exitCode = QApplication::exec();
    window.hide();

    if (backendUnavailable) {
        std::cout << "SKIPPED: " << failure.toStdString() << '\n';
        return 77;
    }

    if (exitCode != 0 || !failure.isEmpty() || completedRequests != 9 || !secondRequestCacheHit ||
        !cancelledJobSubmitted || !virtualSourceResident || !virtualResizeVerified ||
        !virtualLinearVerified || !virtualLanczosAxisVerified || !virtualLanczosVerified ||
        !virtualTiledTargetVerified || !virtualCancellationSubmitted || !virtualVisualSubmitted ||
        !virtualReadbackVerified || !virtualHalfResident || !virtualHalfVerified ||
        !nativeComparisonPixelsAgree || nativeComparisonFrames != 2) {
        std::cerr << "FAILED: " << failure.toStdString() << " completed=" << completedRequests
                  << " cacheHit=" << secondRequestCacheHit
                  << " cancelledSubmitted=" << cancelledJobSubmitted
                  << " virtualResident=" << virtualSourceResident
                  << " virtualResize=" << virtualResizeVerified
                  << " virtualLinear=" << virtualLinearVerified
                  << " virtualLanczosAxis=" << virtualLanczosAxisVerified
                  << " virtualLanczos=" << virtualLanczosVerified
                  << " virtualTiledTarget=" << virtualTiledTargetVerified
                  << " virtualCancelled=" << virtualCancellationSubmitted
                  << " virtualVisual=" << virtualVisualSubmitted
                  << " virtualReadback=" << virtualReadbackVerified
                  << " virtualHalfResident=" << virtualHalfResident
                  << " virtualHalf=" << virtualHalfVerified
                  << " nativeComparisonPixels=" << nativeComparisonPixelsAgree
                  << " nativeComparisonFrames=" << nativeComparisonFrames << '\n';
        return 1;
    }
    std::cout << "QRhi tiled resize tests passed (backend=" << window.backendName().toStdString()
              << ", limit=" << textureLimit << ")\n";
    return 0;
}
