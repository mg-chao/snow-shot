// SPDX-License-Identifier: Apache-2.0
#include "snow_draw_engine_qt/snow_canvas_image.h"

#include <snow/memory/pixel_buffer.h>

#include <QPainter>
#include <QColorTransform>
#include <QList>
#include <QPolygonF>
#include <QRgba64>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
#if defined(Q_PROCESSOR_ARM_64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#elif defined(Q_PROCESSOR_X86_64)
#include <emmintrin.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_set>

namespace {
struct ImageStorageRegistry {
    std::mutex mutex;
    std::unordered_set<const void*> writableImages;
};

std::shared_ptr<ImageStorageRegistry> imageStorageRegistry() {
    static const auto registry = std::make_shared<ImageStorageRegistry>();
    return registry;
}

struct ImageStorage {
    snow::memory::PixelBuffer pixels;
    std::shared_ptr<ImageStorageRegistry> registry = imageStorageRegistry();
    const void* imageIdentity = nullptr;

    ~ImageStorage() {
        const std::lock_guard<std::mutex> lock(registry->mutex);
        registry->writableImages.erase(imageIdentity);
    }
};

void releaseImageStorage(void* owner) {
    delete static_cast<ImageStorage*>(owner);
}

bool ownsWritableStorage(QImage& image) {
    const auto registry = imageStorageRegistry();
    const std::lock_guard<std::mutex> lock(registry->mutex);
    // The public opaque data identity distinguishes a read-only external view
    // from the writable image owning the same address. No Qt private fields are
    // inspected. Cleanup unregisters it before the pixel address can be reused.
    return registry->writableImages.find(image.data_ptr()) != registry->writableImages.end();
}

void copyMetadata(const QImage& source, QImage& destination) {
    destination.setColorTable(source.colorTable());
    const QColorSpace colorSpace = source.colorSpace();
    const bool cmykProfile = colorSpace.colorModel() == QColorSpace::ColorModel::Cmyk;
    if (destination.depth() == 32 && colorSpace.isValid() &&
        cmykProfile != (destination.format() == QImage::Format_CMYK8888)) {
        // Qt carries the original profile through format-only conversions,
        // even RGB/CMYK conversions. Its public setter validates the current
        // model; a same-depth metadata-only retag preserves that existing value.
        const auto format = destination.format();
        const auto carrier = cmykProfile ? QImage::Format_CMYK8888 : QImage::Format_ARGB32;
        if (destination.reinterpretAsFormat(carrier)) {
            destination.setColorSpace(colorSpace);
            destination.reinterpretAsFormat(format);
        }
    } else
        destination.setColorSpace(colorSpace);
    destination.setDevicePixelRatio(source.devicePixelRatio());
    destination.setDotsPerMeterX(source.dotsPerMeterX());
    destination.setDotsPerMeterY(source.dotsPerMeterY());
    destination.setOffset(source.offset());
    for (const QString& key : source.textKeys())
        destination.setText(key, source.text(key));
}

struct ImageFormatInfo {
    quint8 depth = 0;
    QList<QRgb> defaultColorTable;
};

const ImageFormatInfo& imageFormatInfo(QImage::Format format) {
    // Pixel-format channel bits omit unused storage bits (RGB444 is stored in
    // sixteen bits, RGB666 in twenty-four). Derive both storage depth and the
    // default palette from Qt so external allocations retain its format contract.
    static const auto formats = [] {
        std::array<ImageFormatInfo, QImage::NImageFormats> result{};
        for (int index = 1; index < QImage::NImageFormats; ++index) {
            const QImage prototype(1, 1, static_cast<QImage::Format>(index));
            result[static_cast<std::size_t>(index)] = {static_cast<quint8>(prototype.depth()),
                                                       prototype.colorTable()};
        }
        return result;
    }();
    return formats[static_cast<std::size_t>(format)];
}

bool largeRaster(const QSize& size, QImage::Format format) {
    const quint64 depth = imageFormatInfo(format).depth;
    const quint64 stride = ((static_cast<quint64>(size.width()) * depth + 31) / 32) * 4;
    return stride != 0 && static_cast<quint64>(size.height()) >=
                              (snow::memory::kMappedPixelBufferMinimum + stride - 1) / stride;
}

template <typename Work> void processChunks(qsizetype chunks, qsizetype pixels, Work work) {
    // Borrow available workers instead of queuing jobs behind a calling worker.
    // At most eight bounded work items run together; no pixels survive this call.
    const int workers = static_cast<int>(std::min(
        chunks, std::min(qsizetype{8},
                         std::max(qsizetype{1}, qsizetype(QThread::idealThreadCount()) / 2))));
    if (pixels < 256 * 1024 || workers <= 1) {
        for (qsizetype chunk = 0; chunk < chunks; ++chunk)
            work(chunk);
        return;
    }
    std::atomic<qsizetype> next{0};
    QSemaphore finished;
    const auto consume = [&] {
        for (qsizetype chunk = next.fetch_add(1, std::memory_order_relaxed); chunk < chunks;
             chunk = next.fetch_add(1, std::memory_order_relaxed))
            work(chunk);
    };
    int started = 0;
    QThreadPool* pool = QThreadPool::globalInstance();
    for (int worker = 1; worker < workers; ++worker) {
        try {
            if (pool->tryStart([&] {
                    consume();
                    finished.release();
                }))
                ++started;
        } catch (const std::bad_alloc&) {
            // Already-started workers still refer to this call's state. Finish
            // their work before returning, even if another runnable cannot fit.
            break;
        }
    }
    consume();
    finished.acquire(started);
}

template <typename Convert>
QImage convertStrips(const QImage& source, QImage::Format format, Convert convert,
                     Qt::Orientations mirrored = {}, bool orderedDither = false) {
    QImage result = snowCanvasAllocateImage(source.size(), format);
    if (result.isNull())
        return {};
    // Include Qt's highest-precision intermediate format in the work limit.
    // Independent chunks restore parallel conversion without a full heap raster.
    constexpr qsizetype kWorkingBytes = 256 * 1024;
    constexpr qsizetype kWorkingPixelBytes = 16;
    // Bayer dithering repeats every sixteen pixels in each direction. Aligned
    // tiles preserve Qt's global phase when its local converter starts at zero.
    const int alignment = orderedDither ? 16 : 1;
    const int maximumColumns = static_cast<int>(kWorkingBytes / (kWorkingPixelBytes * alignment));
    const int columns = std::min(source.width(), maximumColumns / 16 * 16);
    const int maximumRows =
        static_cast<int>(kWorkingBytes / (qsizetype(columns) * kWorkingPixelBytes));
    const int rows = orderedDither ? std::max(16, maximumRows / 16 * 16) : maximumRows;
    const qsizetype columnsOfTiles = (qsizetype(source.width()) + columns - 1) / columns;
    const qsizetype rowsOfTiles = (qsizetype(source.height()) + rows - 1) / rows;
    const qsizetype sourceStride = source.bytesPerLine();
    const qsizetype destinationStride = result.bytesPerLine();
    const uchar* sourcePixels = source.constBits();
    uchar* destinationPixels = result.bits();
    const int sourceDepth = source.depth();
    const int destinationPixelBytes = result.depth() / 8;
    const auto palette = source.colorTable();
    const auto colorSpace = source.colorSpace();
    std::atomic<bool> failed{false};
    processChunks(columnsOfTiles * rowsOfTiles, qsizetype(source.width()) * source.height(),
                  [&](qsizetype chunk) {
                      if (failed.load(std::memory_order_relaxed))
                          return;
                      try {
                          const int x = static_cast<int>(chunk % columnsOfTiles) * columns;
                          const int y = static_cast<int>(chunk / columnsOfTiles) * rows;
                          const int width = std::min(columns, source.width() - x);
                          const int height = std::min(rows, source.height() - y);
                          const int sourceX =
                              mirrored.testFlag(Qt::Horizontal) ? source.width() - x - width : x;
                          const int sourceY =
                              mirrored.testFlag(Qt::Vertical) ? source.height() - y - height : y;
                          QImage view(sourcePixels + qsizetype(sourceY) * sourceStride +
                                          qsizetype(sourceX) * sourceDepth / 8,
                                      width, height, sourceStride, source.format());
                          view.setColorTable(palette);
                          view.setColorSpace(colorSpace);
                          const QImage converted = convert(view);
                          if (converted.isNull()) {
                              failed.store(true, std::memory_order_relaxed);
                              return;
                          }
                          const uchar* convertedPixels = converted.constBits();
                          for (int row = 0; row < height; ++row)
                              std::memcpy(
                                  destinationPixels + qsizetype(y + row) * destinationStride +
                                      qsizetype(x) * destinationPixelBytes,
                                  convertedPixels + qsizetype(row) * converted.bytesPerLine(),
                                  static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(destinationPixelBytes));
                      } catch (const std::bad_alloc&) {
                          failed.store(true, std::memory_order_relaxed);
                      }
                  });
    if (failed.load(std::memory_order_relaxed))
        return {};
    copyMetadata(source, result);
    return result;
}

template <int PixelBytes>
void transformPixels(const QImage& source, QImage& result, int a, int b, int c, int d) {
    constexpr int kTileSize = 32;
    const uchar* sourcePixels = source.constBits();
    uchar* destinationPixels = result.bits();
    const qsizetype sourceStride = source.bytesPerLine();
    const qsizetype destinationStride = result.bytesPerLine();
    const int offsetX = (a < 0 ? source.width() - 1 : 0) + (c < 0 ? source.height() - 1 : 0);
    const int offsetY = (b < 0 ? source.width() - 1 : 0) + (d < 0 ? source.height() - 1 : 0);
    const qsizetype columns = (qsizetype(source.width()) + kTileSize - 1) / kTileSize;
    const qsizetype rows = (qsizetype(source.height()) + kTileSize - 1) / kTileSize;
    processChunks(
        columns * rows, qsizetype(source.width()) * source.height(), [&](qsizetype chunk) {
            const int firstX = static_cast<int>(chunk % columns) * kTileSize;
            const int firstY = static_cast<int>(chunk / columns) * kTileSize;
            const int lastX = std::min(firstX + kTileSize, source.width());
            const int lastY = std::min(firstY + kTileSize, source.height());
            const auto copyPixel = [&](int x, int y) {
                const int targetX = a * x + c * y + offsetX;
                const int targetY = b * x + d * y + offsetY;
                std::memcpy(destinationPixels + qsizetype(targetY) * destinationStride +
                                qsizetype(targetX) * PixelBytes,
                            sourcePixels + qsizetype(y) * sourceStride + qsizetype(x) * PixelBytes,
                            PixelBytes);
            };
            if (b != 0) {
                for (int x = firstX; x < lastX; ++x)
                    for (int y = firstY; y < lastY; ++y)
                        copyPixel(x, y);
            } else {
                for (int y = firstY; y < lastY; ++y)
                    for (int x = firstX; x < lastX; ++x)
                        copyPixel(x, y);
            }
        });
}

void transformPixels(const QImage& source, QImage& result, const QTransform& matrix) {
    const int a = qRound(matrix.m11());
    const int b = qRound(matrix.m12());
    const int c = qRound(matrix.m21());
    const int d = qRound(matrix.m22());
    switch (source.depth()) {
    case 8:
        transformPixels<1>(source, result, a, b, c, d);
        break;
    case 16:
        transformPixels<2>(source, result, a, b, c, d);
        break;
    case 24:
        transformPixels<3>(source, result, a, b, c, d);
        break;
    case 32:
        transformPixels<4>(source, result, a, b, c, d);
        break;
    case 64:
        transformPixels<8>(source, result, a, b, c, d);
        break;
    case 128:
        transformPixels<16>(source, result, a, b, c, d);
        break;
    default:
        Q_UNREACHABLE();
    }
}

QRgb loadPremultipliedPixel(const uchar* source) {
    QRgb pixel;
    std::memcpy(&pixel, source, sizeof(pixel));
    return qPremultiply(pixel);
}

template <bool SwapChannels>
void premultiplyRows(const uchar* source, uchar* destination, qsizetype sourceStride,
                     qsizetype destinationStride, int width, int first, int last) {
    // Scalar bounds and a fixed channel order let the compiler vectorize the
    // byte-safe loads/stores without treating worker captures as pixel aliases.
    for (int y = first; y < last; ++y) {
        const uchar* input = source + qsizetype(y) * sourceStride;
        uchar* output = destination + qsizetype(y) * destinationStride;
        for (int x = 0; x < width; ++x) {
            QRgb pixel = loadPremultipliedPixel(input + qsizetype(x) * 4);
            if constexpr (SwapChannels)
                pixel = (pixel & 0xff00ff00u) | ((pixel & 0xffu) << 16) | ((pixel >> 16) & 0xffu);
            std::memcpy(output + qsizetype(x) * 4, &pixel, 4);
        }
    }
}

QImage premultiplyImage(const QImage& source, QImage::Format format) {
    QImage result = snowCanvasAllocateImage(source.size(), format);
    if (result.isNull())
        return {};
    const uchar* sourcePixels = source.constBits();
    uchar* destinationPixels = result.bits();
    const qsizetype sourceStride = source.bytesPerLine();
    const qsizetype destinationStride = result.bytesPerLine();
    const int width = source.width();
    const int height = source.height();
    const bool swapChannels = (source.format() == QImage::Format_RGBA8888) !=
                              (format == QImage::Format_RGBA8888_Premultiplied);
    constexpr int kRows = 16;
    processChunks((qsizetype(height) + kRows - 1) / kRows, qsizetype(width) * height,
                  [=](qsizetype chunk) {
                      const int first = static_cast<int>(chunk) * kRows;
                      const int last = std::min(first + kRows, height);
                      if (swapChannels)
                          premultiplyRows<true>(sourcePixels, destinationPixels, sourceStride,
                                                destinationStride, width, first, last);
                      else
                          premultiplyRows<false>(sourcePixels, destinationPixels, sourceStride,
                                                 destinationStride, width, first, last);
                  });
    copyMetadata(source, result);
    return result;
}

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
QImage scaleThumbnailByStrips(const QImage& source, const QSize& size) {
    // An integral vertical ratio gives each output row the same source footprint
    // and lets each strip start at the original area's sampling phase.
    const int sourceRowsPerOutput = source.height() / size.height();
    const qsizetype rowBytes = qsizetype(source.width()) * 4;
    const qsizetype bytesPerOutputRow = rowBytes * sourceRowsPerOutput;
    constexpr qsizetype kMaximumStripBytes = snow::memory::kMappedPixelBufferMinimum;
    if (bytesPerOutputRow > kMaximumStripBytes)
        return {};

    // Keep multirow strips small enough for Qt to scale within the current task.
    constexpr qsizetype kPreferredStripBytes = 256 * 1024;
    const int rowsPerStrip =
        static_cast<int>(std::max(qsizetype{1}, kPreferredStripBytes / bytesPerOutputRow));
    const int strips = 1 + (size.height() - 1) / rowsPerStrip;
    if (strips < 2)
        return {};

    constexpr int kMaximumTasks = 8;
    constexpr auto kPremultiplied = QImage::Format_ARGB32_Premultiplied;
    const int tasks = std::min(strips, kMaximumTasks);
    const int rowsPerScratch = rowsPerStrip * sourceRowsPerOutput;
    const int minimumMappedRows = static_cast<int>((kMaximumStripBytes + rowBytes - 1) / rowBytes);
    QImage scratch = snowCanvasAllocateImage(
        {source.width(), std::max(tasks * rowsPerScratch, minimumMappedRows)}, kPremultiplied);
    QImage result(size, kPremultiplied);
    if (scratch.isNull() || result.isNull())
        return {};

    const auto* input = source.constBits();
    const qsizetype inputStride = source.bytesPerLine();
    auto* buffers = scratch.bits();
    auto* output = result.bits();
    const qsizetype outputStride = result.bytesPerLine();
    const bool swap = source.format() == QImage::Format_RGBA8888;
    std::atomic<bool> failed{false};
    processChunks(tasks, qsizetype(source.width()) * source.height(), [&](qsizetype task) {
        auto* buffer = buffers + task * rowsPerScratch * rowBytes;
        const int firstStrip = static_cast<int>(strips * task / tasks);
        const int endStrip = static_cast<int>(strips * (task + 1) / tasks);
        try {
            for (int strip = firstStrip; strip < endStrip; ++strip) {
                if (failed.load(std::memory_order_relaxed))
                    return;
                const int first = strip * rowsPerStrip;
                const int count = std::min(rowsPerStrip, size.height() - first);
                const int sourceRows = count * sourceRowsPerOutput;
                const auto* start = input + qsizetype(first) * sourceRowsPerOutput * inputStride;
                if (swap)
                    premultiplyRows<true>(start, buffer, inputStride, rowBytes, source.width(), 0,
                                          sourceRows);
                else
                    premultiplyRows<false>(start, buffer, inputStride, rowBytes, source.width(), 0,
                                           sourceRows);
                const QImage view(static_cast<const uchar*>(buffer), source.width(), sourceRows,
                                  rowBytes, kPremultiplied);
                const auto scaled = view.scaled(size.width(), count, Qt::IgnoreAspectRatio,
                                                Qt::SmoothTransformation);
                if (scaled.isNull()) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                std::memcpy(output + qsizetype(first) * outputStride, scaled.constBits(),
                            static_cast<std::size_t>(count * outputStride));
            }
        } catch (const std::bad_alloc&) {
            failed.store(true, std::memory_order_relaxed);
        }
    });
    if (failed.load(std::memory_order_relaxed))
        return {};
    copyMetadata(source, result);
    return result.convertToFormat(source.format());
}
#endif

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN && (defined(Q_PROCESSOR_ARM_64) || defined(Q_PROCESSOR_X86_64))
constexpr int kMaximumThumbnailExtent = 512;
constexpr quint32 kAreaWeight = 1u << 14;
constexpr qint64 kAreaPhase = 1 << 16;

struct ThumbnailSpan {
    int first;
    int count;
    quint32 firstWeight;
    quint32 lastWeight;
};

struct ThumbnailAxis {
    quint32 weight;
    std::array<ThumbnailSpan, kMaximumThumbnailExtent> spans;
};

bool thumbnailAxis(int input, int output, ThumbnailAxis& axis) {
    // Qt's 8-bit area filter uses a 16-bit source phase and 14-bit weights.
    // The last pixel takes the remainder so each span has the same total weight.
    axis.weight = static_cast<quint32>((qint64(output) * kAreaWeight + input - 1) / input);
    const qint64 step = (qint64(input) * kAreaPhase) / output;
    for (std::size_t i = 0; i < static_cast<std::size_t>(output); ++i) {
        const qint64 phase = static_cast<qint64>(i) * step;
        const int first = static_cast<int>(phase / kAreaPhase);
        const quint32 firstWeight =
            static_cast<quint32>((kAreaPhase - phase % kAreaPhase) * axis.weight / kAreaPhase);
        const quint32 interior = (kAreaWeight - firstWeight - 1) / axis.weight;
        const int count = static_cast<int>(interior) + 2;
        if (count > input - first)
            return false;
        axis.spans[i] = {first, count, firstWeight,
                         kAreaWeight - firstWeight - interior * axis.weight};
    }
    return true;
}

// Channels stay in source byte order for both straight-alpha formats.
using ThumbnailChannels = std::array<quint32, 4>;

ThumbnailChannels thumbnailChannels(const uchar* pixel) {
    const QRgb value = loadPremultipliedPixel(pixel);
    return {value & 0xffu, (value >> 8) & 0xffu, (value >> 16) & 0xffu, value >> 24};
}

#if defined(Q_PROCESSOR_ARM_64)
constexpr int kMinimumHorizontalReduction = 8;

uint8x8_t premultiplyThumbnailChannels(uint8x8_t color, uint8x8_t alpha) {
    const auto product = vmull_u8(color, alpha);
    return vrshrn_n_u16(vaddq_u16(product, vshrq_n_u16(product, 8)), 8);
}

uint32x4_t addThumbnailPixels8(uint32x4_t sum, const uchar* pixels, uint8x8_t mask) {
    const auto channels = vld4_u8(pixels);
    const auto alpha = vand_u8(channels.val[3], mask);
    const std::array<std::uint16_t, 4> totals{
        vaddlv_u8(premultiplyThumbnailChannels(channels.val[0], alpha)),
        vaddlv_u8(premultiplyThumbnailChannels(channels.val[1], alpha)),
        vaddlv_u8(premultiplyThumbnailChannels(channels.val[2], alpha)), vaddlv_u8(alpha)};
    return vaddw_u16(sum, vld1_u16(totals.data()));
}

ThumbnailChannels sumThumbnailPixels(const uchar* start, int count) {
    auto total = vdupq_n_u32(0);
    int i = 0;
    for (; i <= count - 8; i += 8)
        total = addThumbnailPixels8(total, start + i * 4, vdup_n_u8(255));
    if (i < count) {
        // Anchor the last load inside the span and suppress pixels already summed.
        const auto tail = std::numeric_limits<std::uint64_t>::max() << ((i + 8 - count) * 8);
        total = addThumbnailPixels8(total, start + (count - 8) * 4,
                                    vreinterpret_u8_u64(vdup_n_u64(tail)));
    }
    ThumbnailChannels channels;
    vst1q_u32(channels.data(), total);
    return channels;
}
#else
constexpr int kMinimumHorizontalReduction = 4;

__m128i premultiplyThumbnailPair(__m128i channels) {
    const auto replicateAlpha = _MM_SHUFFLE(3, 3, 3, 3);
    const auto alpha =
        _mm_shufflehi_epi16(_mm_shufflelo_epi16(channels, replicateAlpha), replicateAlpha);
    // Multiplying alpha by 255 makes the rounded quotient retain that channel.
    channels = _mm_or_si128(channels, _mm_setr_epi16(0, 0, 0, 255, 0, 0, 0, 255));
    const auto product = _mm_mullo_epi16(channels, alpha);
    return _mm_srli_epi16(
        _mm_add_epi16(_mm_add_epi16(product, _mm_srli_epi16(product, 8)), _mm_set1_epi16(128)), 8);
}

ThumbnailChannels sumThumbnailPixels(const uchar* start, int count) {
    const auto zero = _mm_setzero_si128();
    auto wide = zero;
    // Widen before summing more than 256 pixels into the unsigned 16-bit lanes.
    while (count >= 4) {
        const int pixels = std::min(count / 4, 64) * 4;
        auto totals = zero;
        for (int i = 0; i < pixels; i += 4) {
            __m128i bytes;
            std::memcpy(&bytes, start + i * 4, sizeof(bytes));
            const auto low = premultiplyThumbnailPair(_mm_unpacklo_epi8(bytes, zero));
            const auto high = premultiplyThumbnailPair(_mm_unpackhi_epi8(bytes, zero));
            totals = _mm_add_epi16(totals, _mm_add_epi16(low, high));
        }
        const auto reduced = _mm_add_epi16(totals, _mm_srli_si128(totals, 8));
        wide = _mm_add_epi32(wide, _mm_unpacklo_epi16(reduced, zero));
        start += pixels * 4;
        count -= pixels;
    }
    ThumbnailChannels sum;
    std::memcpy(sum.data(), &wide, sizeof(wide));
    for (int i = 0; i < count; ++i) {
        const auto pixel = thumbnailChannels(start + i * 4);
        for (std::size_t c = 0; c < sum.size(); ++c)
            sum[c] += pixel[c];
    }
    return sum;
}
#endif

void accumulateThumbnailSpan(ThumbnailChannels& output, const uchar* row, const ThumbnailSpan& span,
                             quint32 horizontalWeight, quint32 verticalWeight) {
    const auto* start = row + qsizetype(span.first) * 4;
    const auto total = sumThumbnailPixels(start, span.count);
    const auto first = thumbnailChannels(start);
    const auto last = thumbnailChannels(start + (span.count - 1) * 4);
    for (std::size_t c = 0; c < output.size(); ++c) {
        const auto weighted = total[c] * horizontalWeight -
                              first[c] * (horizontalWeight - span.firstWeight) -
                              last[c] * (horizontalWeight - span.lastWeight);
        // Qt truncates four horizontal bits before applying the vertical weight.
        output[c] += (weighted >> 4) * verticalWeight;
    }
}

QImage scaleThumbnailByArea(const QImage& source, const QSize& size) {
    // A full vector of source pixels per output column amortizes endpoint handling.
    if (size.width() > kMaximumThumbnailExtent || size.height() > kMaximumThumbnailExtent ||
        size.height() >= source.height() ||
        qint64(size.width()) * kMinimumHorizontalReduction > source.width())
        return {};

    ThumbnailAxis horizontal;
    ThumbnailAxis vertical;
    if (!thumbnailAxis(source.width(), size.width(), horizontal) ||
        !thumbnailAxis(source.height(), size.height(), vertical))
        return {};

    // Filtering all four channels alike lets RGBA stay in byte order instead of
    // swapping red and blue to ARGB and back. Convert only the small result.
    const auto format = source.format() == QImage::Format_RGBA8888
                            ? QImage::Format_RGBA8888_Premultiplied
                            : QImage::Format_ARGB32_Premultiplied;
    QImage result(size, format);
    if (result.isNull())
        return {};
    const auto* input = source.constBits();
    auto* output = result.bits();
    const auto inputStride = source.bytesPerLine();
    const auto outputStride = result.bytesPerLine();
    const auto width = static_cast<std::size_t>(size.width());
    processChunks(size.height(), qsizetype(source.width()) * source.height(), [&](qsizetype y) {
        // Accumulate one output row at a time so no full premultiplied raster
        // has to be allocated or written.
        std::array<ThumbnailChannels, kMaximumThumbnailExtent> sums;
        std::fill_n(sums.begin(), width, ThumbnailChannels{});
        const auto& span = vertical.spans[static_cast<std::size_t>(y)];
        for (int i = 0; i < span.count; ++i) {
            const auto weight = i == 0 ? span.firstWeight
                                       : (i == span.count - 1 ? span.lastWeight : vertical.weight);
            if (weight == 0)
                continue;
            const auto* row = input + (qsizetype(span.first) + i) * inputStride;
            for (std::size_t x = 0; x < width; ++x)
                accumulateThumbnailSpan(sums[x], row, horizontal.spans[x], horizontal.weight,
                                        weight);
        }
        auto* dst = output + y * outputStride;
        for (std::size_t x = 0; x < width; ++x)
            for (std::size_t c = 0; c < sums[x].size(); ++c)
                dst[x * 4 + c] = static_cast<uchar>(sums[x][c] >> 24);
    });
    copyMetadata(source, result);
    return result.convertToFormat(source.format());
}
#endif

template <bool RgbaBytes>
void expandRgba64Rows(const uchar* source, uchar* destination, qsizetype sourceStride,
                      qsizetype destinationStride, int width, int first, int last) {
    for (int y = first; y < last; ++y) {
        const uchar* input = source + qsizetype(y) * sourceStride;
        uchar* output = destination + qsizetype(y) * destinationStride;
        int x = 0;
        // Replicating adjacent bytes is the native-endian RGBA64 layout. Keep
        // this operation in SIMD registers instead of widening each channel to
        // a scalar QRgba64 and repacking its vectorized lanes.
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN && defined(Q_PROCESSOR_ARM_64)
        for (; x <= width - 4; x += 4) {
            uint8x16_t bytes = vld1q_u8(input + qsizetype(x) * 4);
            if constexpr (!RgbaBytes) {
                constexpr std::array<uchar, 16> order{2,  1, 0, 3,  6,  5,  4,  7,
                                                      10, 9, 8, 11, 14, 13, 12, 15};
                bytes = vqtbl1q_u8(bytes, vld1q_u8(order.data()));
            }
            vst1q_u8(output + qsizetype(x) * 8, vzip1q_u8(bytes, bytes));
            vst1q_u8(output + qsizetype(x) * 8 + 16, vzip2q_u8(bytes, bytes));
        }
#elif Q_BYTE_ORDER == Q_LITTLE_ENDIAN && defined(Q_PROCESSOR_X86_64)
        for (; x <= width - 4; x += 4) {
            __m128i bytes;
            std::memcpy(&bytes, input + qsizetype(x) * 4, sizeof(bytes));
            if constexpr (!RgbaBytes) {
                const __m128i redBlue = _mm_and_si128(bytes, _mm_set1_epi32(0x00ff00ff));
                bytes = _mm_or_si128(
                    _mm_and_si128(bytes, _mm_set1_epi32(~0x00ff00ff)),
                    _mm_or_si128(_mm_slli_epi32(redBlue, 16), _mm_srli_epi32(redBlue, 16)));
            }
            const __m128i low = _mm_unpacklo_epi8(bytes, bytes);
            const __m128i high = _mm_unpackhi_epi8(bytes, bytes);
            std::memcpy(output + qsizetype(x) * 8, &low, sizeof(low));
            std::memcpy(output + qsizetype(x) * 8 + 16, &high, sizeof(high));
        }
#endif
        for (; x < width; ++x) {
            const uchar* channels = input + qsizetype(x) * 4;
            QRgba64 expanded;
            if constexpr (RgbaBytes) {
                expanded = QRgba64::fromRgba(channels[0], channels[1], channels[2], channels[3]);
            } else {
                QRgb pixel;
                std::memcpy(&pixel, channels, sizeof(pixel));
                expanded = QRgba64::fromArgb32(pixel);
            }
            std::memcpy(output + qsizetype(x) * 8, &expanded, sizeof(expanded));
        }
    }
}

QImage expandRgba64Image(const QImage& source) {
    QImage result = snowCanvasAllocateImage(source.size(), QImage::Format_RGBA64);
    if (result.isNull())
        return {};
    const uchar* sourcePixels = source.constBits();
    uchar* destinationPixels = result.bits();
    const qsizetype sourceStride = source.bytesPerLine();
    const qsizetype destinationStride = result.bytesPerLine();
    const int width = source.width();
    const int height = source.height();
    const bool rgbaBytes = source.format() == QImage::Format_RGBA8888;
    constexpr int kRows = 16;
    // Qt's direct 8-to-16-bit converter replicates each channel's byte and
    // ignores conversion flags. Write those values into the final allocation,
    // without a native converted raster or a second pixel transfer.
    processChunks((qsizetype(height) + kRows - 1) / kRows, qsizetype(width) * height,
                  [=](qsizetype chunk) {
                      const int first = static_cast<int>(chunk) * kRows;
                      const int last = std::min(first + kRows, height);
                      if (rgbaBytes)
                          expandRgba64Rows<true>(sourcePixels, destinationPixels, sourceStride,
                                                 destinationStride, width, first, last);
                      else
                          expandRgba64Rows<false>(sourcePixels, destinationPixels, sourceStride,
                                                  destinationStride, width, first, last);
                  });
    copyMetadata(source, result);
    return result;
}
} // namespace

QImage snowCanvasAllocateImage(const QSize& size, QImage::Format format) {
    if (size.width() <= 0 || size.height() <= 0 || format <= QImage::Format_Invalid ||
        format >= QImage::NImageFormats)
        return {};
    const auto& info = imageFormatInfo(format);
    const quint64 depth = info.depth;
    const quint64 stride = ((static_cast<quint64>(size.width()) * depth + 31) / 32) * 4;
    const auto maximum = static_cast<quint64>(std::numeric_limits<qsizetype>::max());
    if (stride == 0 || stride > maximum / static_cast<quint64>(size.height()))
        return {};
    const quint64 bytes = stride * static_cast<quint64>(size.height());
    if (bytes < snow::memory::kMappedPixelBufferMinimum)
        return QImage(size, format);

    try {
        auto storage = snow::memory::allocatePixelBuffer(static_cast<std::size_t>(bytes));
        if (!storage)
            return {};
        auto owner =
            std::unique_ptr<ImageStorage>(new (std::nothrow) ImageStorage{std::move(storage)});
        if (!owner)
            return {};
        QImage image(owner->pixels.get(), size.width(), size.height(),
                     static_cast<qsizetype>(stride), format, releaseImageStorage, owner.get());
        if (!image.isNull()) {
            auto* holder = owner.release();
            holder->imageIdentity = image.data_ptr();
            const std::lock_guard<std::mutex> lock(holder->registry->mutex);
            holder->registry->writableImages.insert(holder->imageIdentity);
        }
        if (!info.defaultColorTable.isEmpty())
            image.setColorTable(info.defaultColorTable);
        return image;
    } catch (const std::bad_alloc&) {
        return {};
    }
}

QImage snowCanvasAllocateZeroedImage(const QSize& size, QImage::Format format) {
    QImage image = snowCanvasAllocateImage(size, format);
    if (!image.isNull() && image.sizeInBytes() < qsizetype(snow::memory::kMappedPixelBufferMinimum))
        std::memset(image.bits(), 0, static_cast<std::size_t>(image.sizeInBytes()));
    return image;
}

bool snowCanvasCopyRgba8888Rows(const QImage& image, int firstRow, int rowCount, uchar* destination,
                                qsizetype destinationBytes, qsizetype destinationStride,
                                const QColorSpace& colorSpace, Qt::ImageConversionFlags flags) {
    if (image.isNull() || firstRow < 0 || rowCount <= 0 || rowCount > image.height() ||
        firstRow > image.height() - rowCount || destination == nullptr || destinationBytes < 0)
        return false;
    constexpr auto maximum = std::numeric_limits<qsizetype>::max();
    if (qsizetype(image.width()) > maximum / 4)
        return false;
    const qsizetype rowBytes = qsizetype(image.width()) * 4;
    if (destinationStride < rowBytes ||
        qsizetype(rowCount - 1) > (maximum - rowBytes) / destinationStride)
        return false;
    const qsizetype required = qsizetype(rowCount - 1) * destinationStride + rowBytes;
    if (destinationBytes < required || (colorSpace.isValid() && !image.colorSpace().isValid()))
        return false;
    const auto sourceAddress = reinterpret_cast<std::uintptr_t>(image.constBits());
    const auto destinationAddress = reinterpret_cast<std::uintptr_t>(destination);
    constexpr auto addressMaximum = std::numeric_limits<std::uintptr_t>::max();
    // Borrowed crops keep their original stride; padding after their final pixel row is not
    // readable source storage and must not falsely overlap an adjacent destination buffer.
    const auto sourceBytes =
        static_cast<std::uintptr_t>(image.sizeInBytes() - image.bytesPerLine() +
                                    (qsizetype(image.width()) * image.depth() + 7) / 8);
    const auto destinationExtent = static_cast<std::uintptr_t>(required);
    if (sourceBytes > addressMaximum - sourceAddress ||
        destinationExtent > addressMaximum - destinationAddress ||
        (sourceAddress < destinationAddress + destinationExtent &&
         destinationAddress < sourceAddress + sourceBytes))
        return false;

    const bool transformColor = colorSpace.isValid() && image.colorSpace() != colorSpace;
    const bool preservePremultiplication =
        transformColor && image.pixelFormat().premultiplied() == QPixelFormat::Premultiplied;
    const auto copyRows = [&](const QImage& converted) {
        if (converted.isNull())
            return false;
        for (int row = 0; row < rowCount; ++row)
            std::memcpy(destination + qsizetype(row) * destinationStride,
                        converted.constScanLine(firstRow + row),
                        static_cast<std::size_t>(rowBytes));
        return true;
    };
    if (!transformColor && image.format() == QImage::Format_RGBA8888)
        return copyRows(image);

    try {
        if ((flags & Qt::PreferDither) && (flags & Qt::Dither_Mask) != Qt::ThresholdDither &&
            !preservePremultiplication) {
            // Qt 6.11's indexed color converter writes the palette of its
            // shallow source copy. Give that path independent image metadata
            // while borrowing the same immutable pixels for this call.
            QImage view(image.constBits(), image.width(), image.height(), image.bytesPerLine(),
                        image.format());
            view.setColorTable(image.colorTable());
            view.setColorSpace(image.colorSpace());
            return copyRows(transformColor ? view.convertedToColorSpace(
                                                 colorSpace, QImage::Format_RGBA8888, flags)
                                           : view.convertToFormat(QImage::Format_RGBA8888, flags));
        }

        // A tile, including Qt's highest-precision intermediate format, is at
        // most 256 KiB. Columns are byte-aligned even for monochrome sources.
        constexpr int maximumColumns = 256 * 1024 / 16;
        const int columns = std::min(image.width(), maximumColumns);
        const int rows = 256 * 1024 / (columns * 16);
        const qsizetype columnsOfTiles = (qsizetype(image.width()) + columns - 1) / columns;
        const qsizetype rowsOfTiles = (qsizetype(rowCount) + rows - 1) / rows;
        const auto palette = image.colorTable();
        const auto sourceColorSpace = image.colorSpace();
        const QColorTransform transform =
            transformColor ? sourceColorSpace.transformationToColorSpace(colorSpace)
                           : QColorTransform{};
        const uchar* source = image.constBits();
        const qsizetype sourceStride = image.bytesPerLine();
        const int sourceDepth = image.depth();
        std::atomic<bool> failed{false};
        processChunks(
            columnsOfTiles * rowsOfTiles, qsizetype(image.width()) * rowCount,
            [&](qsizetype chunk) {
                if (failed.load(std::memory_order_relaxed))
                    return;
                try {
                    const int x = static_cast<int>(chunk % columnsOfTiles) * columns;
                    const int y = static_cast<int>(chunk / columnsOfTiles) * rows;
                    const int width = std::min(columns, image.width() - x);
                    const int height = std::min(rows, rowCount - y);
                    QImage view(source + qsizetype(firstRow + y) * sourceStride +
                                    qsizetype(x) * sourceDepth / 8,
                                width, height, sourceStride, image.format());
                    view.setColorTable(palette);
                    view.setColorSpace(sourceColorSpace);
                    const QImage converted =
                        !transformColor ? view.convertToFormat(QImage::Format_RGBA8888, flags)
                        : preservePremultiplication
                            // Qt's combined color/format conversion can mark premultiplied
                            // channels as straight RGBA. Preserve native precision/alpha for
                            // color conversion, then explicitly unpremultiply this bounded tile.
                            ? view.colorTransformed(transform).convertToFormat(
                                  QImage::Format_RGBA8888, flags)
                            : image.format() <= QImage::Format_Indexed8
                                  // Qt converts the global palette before expanding
                                  // pixels; colorTransformed follows a different route.
                                  ? view.convertedToColorSpace(colorSpace, QImage::Format_RGBA8888,
                                                               flags)
                                  : view.colorTransformed(transform, QImage::Format_RGBA8888,
                                                          flags);
                    if (converted.isNull()) {
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                    for (int row = 0; row < height; ++row)
                        std::memcpy(
                            destination + qsizetype(y + row) * destinationStride + qsizetype(x) * 4,
                            converted.constScanLine(row), static_cast<std::size_t>(width) * 4);
                } catch (const std::bad_alloc&) {
                    failed.store(true, std::memory_order_relaxed);
                }
            });
        return !failed.load(std::memory_order_relaxed);
    } catch (const std::bad_alloc&) {
        return false;
    }
}

QImage snowCanvasCopyImage(const QImage& image, const QRect& rect) {
    if (image.isNull())
        return {};
    const QRect requested = rect.isNull() ? image.rect() : rect;
    if (requested.isEmpty())
        return {};
    QImage copy = snowCanvasAllocateImage(requested.size(), image.format());
    if (copy.isNull())
        return {};
    copyMetadata(image, copy);
    const qsizetype destinationStride = copy.bytesPerLine();
    const qsizetype sourceStride = image.bytesPerLine();
    if (requested == image.rect() && destinationStride == sourceStride) {
        std::memcpy(copy.bits(), image.constBits(), static_cast<std::size_t>(copy.sizeInBytes()));
        return copy;
    }
    const QRect available = requested.intersected(image.rect());
    if (available.isEmpty()) {
        copy.fill(0);
        return copy;
    }
    const int destinationX = available.left() - requested.left();
    const int destinationY = available.top() - requested.top();
    const int depth = image.depth();
    const bool byteAligned =
        depth % 8 == 0 || ((available.left() | destinationX | available.width()) & 7) == 0;
    if (available != requested || !byteAligned)
        copy.fill(0);
    const uchar* source = image.constScanLine(available.top());
    uchar* destination = copy.scanLine(destinationY);
    if (!byteAligned) {
        // A packed crop can start on different bit boundaries in each image.
        // The destination bits are zero, so only copy set source bits.
        const int bitOrder = image.format() == QImage::Format_Mono ? 7 : 0;
        for (int row = 0; row < available.height(); ++row) {
            const uchar* input = source + qsizetype(row) * sourceStride;
            uchar* output = destination + qsizetype(row) * destinationStride;
            for (int column = 0; column < available.width(); ++column) {
                const int x = available.left() + column;
                const int target = destinationX + column;
                if (input[x / 8] & (1u << ((x & 7) ^ bitOrder)))
                    output[target / 8] |= static_cast<uchar>(1u << ((target & 7) ^ bitOrder));
            }
        }
        return copy;
    }

    source += qsizetype(available.left()) * depth / 8;
    destination += qsizetype(destinationX) * depth / 8;
    const qsizetype rowBytes = qsizetype(available.width()) * depth / 8;
    const qsizetype height = available.height();
    const auto copyRows = [=](qsizetype first, qsizetype last) {
        if (rowBytes == sourceStride && rowBytes == destinationStride) {
            std::memcpy(destination + first * destinationStride, source + first * sourceStride,
                        static_cast<std::size_t>((last - first) * rowBytes));
            return;
        }
        for (auto row = first; row < last; ++row)
            std::memcpy(destination + row * destinationStride, source + row * sourceStride,
                        static_cast<std::size_t>(rowBytes));
    };
    constexpr qsizetype kChunkBytes = 1024 * 1024;
    if (available == requested && copy.sizeInBytes() >= 2 * kChunkBytes) {
        // Spread first writes to freshly mapped pages across available workers.
        const auto rowsPerChunk = std::max(qsizetype{1}, kChunkBytes / rowBytes);
        processChunks(1 + (height - 1) / rowsPerChunk, height * available.width(),
                      [=](qsizetype chunk) {
                          const auto first = chunk * rowsPerChunk;
                          copyRows(first, std::min(height, first + rowsPerChunk));
                      });
    } else {
        copyRows(0, height);
    }
    return copy;
}

bool snowCanvasDetachImage(QImage& image, bool writableStorage) {
    if (image.isNull())
        return false;
    if (image.isDetached() && (writableStorage || ownsWritableStorage(image)))
        return true;
    if (image.sizeInBytes() < qsizetype(snow::memory::kMappedPixelBufferMinimum)) {
        image.detach();
        return !image.isNull();
    }
    QImage copy = snowCanvasCopyImage(image);
    if (copy.isNull())
        return false;
    image = std::move(copy);
    return true;
}

QImage snowCanvasConvertImage(const QImage& image, QImage::Format format,
                              Qt::ImageConversionFlags flags) {
    if (image.isNull() || format <= QImage::Format_Invalid || format >= QImage::NImageFormats)
        return {};
    if (format == image.format())
        return image;
    if (format <= QImage::Format_Indexed8) {
        QImage result = image.convertToFormat(format, flags);
        return result.sizeInBytes() < qsizetype(snow::memory::kMappedPixelBufferMinimum)
                   ? result
                   : snowCanvasCopyImage(result);
    }
    if (!largeRaster(image.size(), image.format()) && !largeRaster(image.size(), format))
        return image.convertToFormat(format, flags);
    if (format == QImage::Format_RGBA64 &&
        (image.format() == QImage::Format_ARGB32 || image.format() == QImage::Format_RGBA8888))
        return expandRgba64Image(image);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    if ((image.format() == QImage::Format_ARGB32 || image.format() == QImage::Format_RGBA8888) &&
        (format == QImage::Format_ARGB32_Premultiplied ||
         format == QImage::Format_RGBA8888_Premultiplied))
        return premultiplyImage(image, format);
#endif
    return convertStrips(
        image, format,
        [format, flags](const QImage& strip) { return strip.convertToFormat(format, flags); }, {},
        (flags & Qt::PreferDither) && (flags & Qt::Dither_Mask) != Qt::ThresholdDither);
}

QImage snowCanvasColorConvertedImage(const QImage& image, const QColorSpace& colorSpace,
                                     QImage::Format format, Qt::ImageConversionFlags flags) {
    if (image.isNull() || !image.colorSpace().isValid() || !colorSpace.isValid() ||
        format <= QImage::Format_Invalid || format >= QImage::NImageFormats)
        return {};
    if (image.colorSpace() == colorSpace)
        return snowCanvasConvertImage(image, format, flags);
    if (format <= QImage::Format_Indexed8) {
        QImage result = image.convertedToColorSpace(colorSpace, format, flags);
        return result.sizeInBytes() < qsizetype(snow::memory::kMappedPixelBufferMinimum)
                   ? result
                   : snowCanvasCopyImage(result);
    }
    if (!largeRaster(image.size(), image.format()) && !largeRaster(image.size(), format))
        return image.convertedToColorSpace(colorSpace, format, flags);
    const QColorTransform transform = image.colorSpace().transformationToColorSpace(colorSpace);
    QImage result = convertStrips(
        image, format,
        [&transform, format, flags](const QImage& strip) {
            return strip.colorTransformed(transform, format, flags);
        },
        {}, (flags & Qt::PreferDither) && (flags & Qt::Dither_Mask) != Qt::ThresholdDither);
    result.setColorSpace(colorSpace);
    return result;
}

QImage snowCanvasTransformImage(const QImage& image, const QTransform& transform,
                                Qt::TransformationMode mode) {
    if (image.isNull() || transform.isIdentity())
        return image;
    const QTransform matrix = QImage::trueMatrix(transform, image.width(), image.height());
    const QRectF bounds = matrix.mapRect(QRectF(image.rect()));
    if (!std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) || bounds.width() <= 0 ||
        bounds.height() <= 0 || bounds.width() > std::numeric_limits<int>::max() ||
        bounds.height() > std::numeric_limits<int>::max())
        return {};
    const QSize size(qCeil(bounds.width()), qCeil(bounds.height()));
    const auto unit = [](qreal value) { return value == 0 || value == 1 || value == -1; };
    const bool orthogonal = matrix.isAffine() && unit(matrix.m11()) && unit(matrix.m12()) &&
                            unit(matrix.m21()) && unit(matrix.m22()) &&
                            matrix.m11() * matrix.m11() + matrix.m12() * matrix.m12() == 1 &&
                            matrix.m21() * matrix.m21() + matrix.m22() * matrix.m22() == 1 &&
                            matrix.m11() * matrix.m21() + matrix.m12() * matrix.m22() == 0;
    const bool scaleTransform = matrix.type() <= QTransform::TxScale;
    const bool reflection = matrix.determinant() < 0;
    const bool nativePainterFormat = image.format() == QImage::Format_RGB32 ||
                                     image.format() == QImage::Format_ARGB32 ||
                                     image.format() == QImage::Format_ARGB32_Premultiplied ||
                                     image.format() == QImage::Format_RGBA8888 ||
                                     image.format() == QImage::Format_RGBA8888_Premultiplied;
    const bool integralOrigin =
        matrix.m31() == std::floor(matrix.m31()) && matrix.m32() == std::floor(matrix.m32());
    if (!orthogonal && matrix.isAffine() && matrix.type() == QTransform::TxRotate &&
        matrix.determinant() > 0 && nativePainterFormat && image.width() <= 32767 &&
        image.height() <= 32767 &&
        (image.devicePixelRatio() == 1 || image.bytesPerLine() == qsizetype(image.width()) * 4)) {
        // Qt paints ordinary arbitrary rotations itself. Its aligned polygon
        // bounds include fractional edges that ceil(width/height) would omit.
        const QPolygonF polygon(QRectF(0, 0, image.width(), image.height()));
        const QSize paintSize = matrix.map(polygon).boundingRect().toAlignedRect().size();
        if (!paintSize.isEmpty() && paintSize.width() <= 32767 && paintSize.height() <= 32767) {
            const auto format = image.format() == QImage::Format_RGB32
                                    ? QImage::Format_ARGB32_Premultiplied
                                    : image.format();
            QImage result = snowCanvasAllocateZeroedImage(paintSize, format);
            if (result.isNull())
                return {};
            // Match Qt's DPR-neutral source view. Unusual strides with a DPR
            // retain the native fallback rather than changing its row layout.
            const QImage view =
                image.devicePixelRatio() != 1
                    ? QImage(image.constBits(), image.width(), image.height(), image.format())
                    : image;
            QPainter painter(&result);
            if (mode == Qt::SmoothTransformation) {
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
            }
            painter.setTransform(matrix);
            painter.drawImage(QPoint(), view);
            painter.end();
            copyMetadata(image, result);
            return result;
        }
    }
    const bool exactQtFallback =
        !orthogonal || !integralOrigin || image.format() < QImage::Format_RGB32 ||
        image.format() == QImage::Format_CMYK8888 ||
        // Qt's identity smooth scaler still computes sample origins using a
        // signed 0x8000 * dimension expression. Beyond this range its native
        // resampling is not equivalent to a byte mirror; retain that behavior.
        (reflection && scaleTransform && mode == Qt::SmoothTransformation &&
         (image.width() > 65535 || image.height() > 65535)) ||
        (reflection && mode == Qt::FastTransformation &&
         ((!scaleTransform && !image.hasAlphaChannel()) ||
          image.format() == QImage::Format_RGBA64)) ||
        (reflection && !scaleTransform && mode == Qt::SmoothTransformation && !nativePainterFormat);
    if (exactQtFallback) {
        QImage transformed = image.transformed(transform, mode);
        return transformed.sizeInBytes() < qsizetype(snow::memory::kMappedPixelBufferMinimum)
                   ? transformed
                   : snowCanvasCopyImage(transformed);
    }
    if (orthogonal && reflection && scaleTransform && mode == Qt::SmoothTransformation &&
        qint64(image.width()) * image.height() >= (1 << 20)) {
        // Qt's large smooth mirror normalizes to premultiplied color before
        // flipping. A unit scale needs no resampling, so only those conversions
        // and the final mapped mirror are necessary.
        auto workingFormat = image.format();
        switch (workingFormat) {
        case QImage::Format_RGB32:
        case QImage::Format_ARGB32_Premultiplied:
        case QImage::Format_RGBX8888:
        case QImage::Format_RGBA8888_Premultiplied:
        case QImage::Format_RGBX64:
        case QImage::Format_RGBA64_Premultiplied:
        case QImage::Format_RGBX32FPx4:
        case QImage::Format_RGBA32FPx4_Premultiplied:
            break;
        case QImage::Format_RGBA64:
        case QImage::Format_Grayscale16:
            workingFormat = QImage::Format_RGBA64_Premultiplied;
            break;
        case QImage::Format_RGBX16FPx4:
            workingFormat = QImage::Format_RGBX32FPx4;
            break;
        case QImage::Format_RGBA16FPx4:
        case QImage::Format_RGBA16FPx4_Premultiplied:
        case QImage::Format_RGBA32FPx4:
            workingFormat = QImage::Format_RGBA32FPx4_Premultiplied;
            break;
        default:
            workingFormat = image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                                    : QImage::Format_RGB32;
            break;
        }
        if (workingFormat != image.format()) {
            const Qt::Orientations mirrored =
                (matrix.m11() < 0 ? Qt::Horizontal : Qt::Orientations{}) |
                (matrix.m22() < 0 ? Qt::Vertical : Qt::Orientations{});
            return convertStrips(
                image, image.format(),
                [workingFormat, mirrored](const QImage& strip) {
                    QImage working = strip.convertToFormat(workingFormat);
                    // This bounded temporary is owned by Qt, so its public in-place
                    // flip retains its allocation. Managed external images cannot.
                    working.flip(mirrored);
                    return std::move(working).convertToFormat(strip.format());
                },
                mirrored);
        }
    }
    const bool directReflection =
        scaleTransform &&
        ((mode == Qt::FastTransformation &&
          (image.format() == QImage::Format_RGB32 ||
           image.format() == QImage::Format_ARGB32_Premultiplied ||
           image.format() == QImage::Format_RGBA8888_Premultiplied)) ||
         (mode == Qt::SmoothTransformation && qint64(image.width()) * image.height() >= (1 << 20) &&
          image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_RGBA8888));
    if (orthogonal && (!reflection || directReflection) && image.depth() % 8 == 0) {
        QImage result = snowCanvasAllocateImage(size, image.format());
        if (result.isNull())
            return {};
        copyMetadata(image, result);
        transformPixels(image, result, matrix);
        return result;
    }
    // Qt's paint-based transform has a signed 16-bit coordinate domain. Tiling
    // an image outside it changes native clipping/rounding, so retain its single
    // mapped paint target for those dimensions instead of changing the result.
    if (orthogonal && reflection && scaleTransform && mode == Qt::FastTransformation &&
        image.width() <= 32767 && image.height() <= 32767 &&
        (image.format() == QImage::Format_ARGB32 || image.format() == QImage::Format_RGBA8888)) {
        const Qt::Orientations mirrored = (matrix.m11() < 0 ? Qt::Horizontal : Qt::Orientations{}) |
                                          (matrix.m22() < 0 ? Qt::Vertical : Qt::Orientations{});
        return convertStrips(
            image, image.format(),
            [&transform, mode](const QImage& strip) { return strip.transformed(transform, mode); },
            mirrored);
    }
    // QPainter writes into the final external raster. Choose an alpha-capable
    // format for transparent corners introduced by non-orthogonal transforms.
    auto format = image.format();
    if (format < QImage::Format_RGB32 || (!scaleTransform && !image.hasAlphaChannel())) {
        format = image.depth() > 32 ? QImage::Format_RGBA64_Premultiplied
                                    : QImage::Format_ARGB32_Premultiplied;
    }
    const bool transparentZero = image.hasAlphaChannel() ||
                                 format == QImage::Format_ARGB32_Premultiplied ||
                                 format == QImage::Format_RGBA64_Premultiplied;
    QImage result = transparentZero ? snowCanvasAllocateZeroedImage(size, format)
                                    : snowCanvasAllocateImage(size, format);
    if (result.isNull())
        return {};
    if (!transparentZero)
        result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, mode == Qt::SmoothTransformation);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, mode == Qt::SmoothTransformation);
    painter.setTransform(matrix);
    QImage view(image.constBits(), image.width(), image.height(), image.bytesPerLine(),
                image.format());
    view.setColorTable(image.colorTable());
    painter.drawImage(QPoint(), view);
    painter.end();
    copyMetadata(image, result);
    return result;
}

QImage snowCanvasScaleImage(const QImage& image, const QSize& size, Qt::AspectRatioMode aspectRatio,
                            Qt::TransformationMode mode) {
    if (image.isNull() || size.isEmpty())
        return {};
    const QSize outputSize = image.size().scaled(size, aspectRatio).expandedTo(QSize(1, 1));
    if (outputSize == image.size())
        return image;
    const auto format = image.format();
    const bool straightAlpha = format == QImage::Format_ARGB32 || format == QImage::Format_RGBA8888;
    const bool areaDownscale = qint64(outputSize.width()) * 2 < image.width() ||
                               qint64(outputSize.height()) * 2 < image.height();
    if (mode == Qt::SmoothTransformation && straightAlpha && areaDownscale &&
        largeRaster(image.size(), format) && !largeRaster(outputSize, format)) {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN && (defined(Q_PROCESSOR_ARM_64) || defined(Q_PROCESSOR_X86_64))
        if (auto thumbnail = scaleThumbnailByArea(image, outputSize); !thumbnail.isNull())
            return thumbnail;
#endif
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        if (image.height() % outputSize.height() == 0) {
            QImage thumbnail = scaleThumbnailByStrips(image, outputSize);
            if (!thumbnail.isNull())
                return thumbnail;
        }
#endif
        // Qt's area scaler converts straight alpha to this format before filtering.
        // Put that large intermediate in managed pages and keep Qt's sampling and output format.
        const auto premultiplied =
            snowCanvasConvertImage(image, QImage::Format_ARGB32_Premultiplied);
        if (premultiplied.isNull())
            return {};
        return premultiplied.scaled(outputSize, Qt::IgnoreAspectRatio, mode)
            .convertToFormat(format);
    }
    QImage result = image.scaled(outputSize, Qt::IgnoreAspectRatio, mode);
    return result.sizeInBytes() < qsizetype(snow::memory::kMappedPixelBufferMinimum)
               ? result
               : snowCanvasCopyImage(result);
}
