#include "screenshotsaveexportpipeline.h"
#include "snowimagecodecbridge.h"
#include "snowimageqtcodec.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QStorageInfo>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace screenshot_save_export {
namespace {
constexpr qint64 kMaximumRasterBytes = qint64{8} * 1024 * 1024 * 1024;
int32_t SNOW_SHOT_IMAGE_CODEC_CALL cancelled(void* context) {
    return static_cast<const ScreenshotExportCancellation*>(context)->isCancellationRequested() ? 1
                                                                                                : 0;
}
uint32_t bridgeFormat(ScreenshotImageFileFormat format) {
    switch (format) {
    case ScreenshotImageFileFormat::Png:
        return SNOW_SHOT_IMAGE_CODEC_FORMAT_PNG;
    case ScreenshotImageFileFormat::Jpeg:
        return SNOW_SHOT_IMAGE_CODEC_FORMAT_JPEG;
    case ScreenshotImageFileFormat::Webp:
        return SNOW_SHOT_IMAGE_CODEC_FORMAT_WEBP;
    case ScreenshotImageFileFormat::Jxl:
        return SNOW_SHOT_IMAGE_CODEC_FORMAT_JXL;
    case ScreenshotImageFileFormat::Avif:
        return SNOW_SHOT_IMAGE_CODEC_FORMAT_AVIF;
    }
    return SNOW_SHOT_IMAGE_CODEC_FORMAT_UNKNOWN;
}
} // namespace

MappedRaster::~MappedRaster() {
    if (pixels)
        m_file.unmap(pixels);
}
std::shared_ptr<MappedRaster> MappedRaster::create(QSize size, QString* error) {
    if (size.isEmpty() || size.width() > std::numeric_limits<int>::max() / 4 ||
        qint64(size.width()) * size.height() > kMaximumRasterBytes / 4) {
        *error = QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                             "The image dimensions exceed the export limit");
        return {};
    }
    const qint64 bytes = qint64(size.width()) * size.height() * 4;
    auto raster = std::make_shared<MappedRaster>();
    if (!raster->m_directory.isValid()) {
        *error = raster->m_directory.errorString();
        return {};
    }
    const QStorageInfo storage(raster->m_directory.path());
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < bytes) {
        *error =
            QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                        "There is not enough temporary disk space for this export");
        return {};
    }
    raster->size = size;
    raster->m_file.setFileName(raster->m_directory.filePath(QStringLiteral("pixels.rgba")));
    if (!raster->m_file.open(QIODevice::ReadWrite) || !raster->m_file.resize(bytes) ||
        !(raster->pixels = raster->m_file.map(0, bytes))) {
        *error = raster->m_file.errorString();
        return {};
    }
    return raster;
}
ScreenshotImageRowSource MappedRaster::rows(std::function<bool()> cancellation) const {
    ScreenshotImageRowSource result;
    result.size = size;
    result.cancellationRequested = std::move(cancellation);
    const uchar* data = pixels;
    const QSize dimensions = size;
    result.readRows = [data, dimensions](int first, int count, qsizetype stride, uchar* target,
                                         qsizetype capacity) {
        const qsizetype rowBytes = qsizetype(dimensions.width()) * 4;
        if (!target || first < 0 || count < 1 || first > dimensions.height() ||
            count > dimensions.height() - first || stride < rowBytes || capacity < rowBytes ||
            count - 1 > (capacity - rowBytes) / stride)
            return false;
        for (int row = 0; row < count; ++row)
            std::memcpy(target + row * stride, data + (first + row) * rowBytes, size_t(rowBytes));
        return true;
    };
    return result;
}
QImage MappedRaster::thumbnail(int maximumExtent) const {
    const QImage mapped(static_cast<const uchar*>(pixels), size.width(), size.height(),
                        qsizetype(size.width()) * 4, QImage::Format_RGBA8888);
    QImage result = size.width() <= maximumExtent && size.height() <= maximumExtent
                        ? mapped.copy()
                        : mapped.scaled(maximumExtent, maximumExtent, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
    result.setColorSpace(QColorSpace::SRgb);
    return result;
}
Source prepare(const ScreenshotImageRowSource& rows,
               const ScreenshotExportCancellation& cancellation, QString* error) {
    if (!rows.isValid() || cancellation.isCancellationRequested())
        return {};
    QSize size = rows.size;
    if (size.width() > 2048 || size.height() > 2048)
        size.scale(2048, 2048, Qt::KeepAspectRatio);
    size = size.expandedTo(QSize(1, 1));
    if (size == rows.size) {
        QImage preview(size, QImage::Format_RGBA8888);
        if (preview.isNull() || !rows.readRows(0, size.height(), preview.bytesPerLine(),
                                               preview.bits(), preview.sizeInBytes())) {
            *error = QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                                 "The screenshot pixels could not be read");
            return {};
        }
        preview.setColorSpace(QColorSpace::SRgb);
        return {rows, preview};
    }
    QImage preview(size, QImage::Format_RGBA8888_Premultiplied);
    if (preview.isNull())
        return {};
    // Retain the immutable row source. Downsample in bounded strips without materializing
    // a scrolling capture or keeping a second full-resolution pixel buffer.
    std::vector<quint64> sums(size_t(size.width()) * 4);
    for (int y = 0; y < size.height(); ++y) {
        std::fill(sums.begin(), sums.end(), 0);
        const int first = int(qint64(y) * rows.size.height() / size.height());
        const int end = int(qint64(y + 1) * rows.size.height() / size.height());
        for (int row = first; row < end; row += 64) {
            if (cancellation.isCancellationRequested())
                return {};
            const int count = qMin(64, end - row);
            QImage strip(rows.size.width(), count, QImage::Format_RGBA8888);
            if (strip.isNull() || !rows.readRows(row, count, strip.bytesPerLine(), strip.bits(),
                                                 strip.sizeInBytes())) {
                *error = QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                                     "The screenshot pixels could not be read");
                return {};
            }
            strip = strip.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            if (strip.width() != size.width())
                strip = strip.scaled(size.width(), count, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
            for (int line = 0; line < count; ++line) {
                const uchar* pixels = strip.constScanLine(line);
                for (size_t x = 0; x < sums.size(); ++x)
                    sums[x] += pixels[x];
            }
        }
        uchar* pixels = preview.scanLine(y);
        for (size_t x = 0; x < sums.size(); ++x)
            pixels[x] = uchar((sums[x] + (end - first) / 2) / (end - first));
    }
    preview = preview.convertToFormat(QImage::Format_RGBA8888);
    preview.setColorSpace(QColorSpace::SRgb);
    return {rows, preview};
}
QSize encoderLimits(ScreenshotImageFileFormat format) {
    uint32_t width = 0;
    uint32_t height = 0;
    if (!snow_shot_image_codec_export_limits(bridgeFormat(format), &width, &height))
        return {};
    return {int(qMin(width, uint32_t(std::numeric_limits<int>::max() / 4))),
            int(qMin(height, uint32_t(std::numeric_limits<int>::max() / 4)))};
}
std::shared_ptr<Encoded> render(const Source& source, const ScreenshotSaveExportOptions& options,
                                const ScreenshotExportCancellation& cancellation, QString* error,
                                bool previewOnly) {
    if (!source.rows.isValid() || cancellation.isCancellationRequested())
        return {};
    const QSize limits = encoderLimits(options.format);
    if (options.size.isEmpty() || options.size.width() > limits.width() ||
        options.size.height() > limits.height()) {
        *error = QCoreApplication::translate(
            "ScreenshotSaveAsFileDialog", "The dimensions are not supported by this image format");
        return {};
    }
    QSize outputSize = options.size;
    if (previewOnly && (outputSize.width() > 2048 || outputSize.height() > 2048))
        outputSize.scale(2048, 2048, Qt::KeepAspectRatio);
    outputSize = outputSize.expandedTo(QSize(1, 1));
    auto rows = previewOnly ? snow_shot::image_codec::srgbRowSource(source.preview) : source.rows;
    std::shared_ptr<MappedRaster> raster;
    std::array<char, 1024> backendError{};
    auto* context = const_cast<ScreenshotExportCancellation*>(&cancellation);
    if (outputSize != rows.size) {
        auto input = MappedRaster::create(rows.size, error);
        if (!input)
            return {};
        const qsizetype stride = qsizetype(rows.size.width()) * 4;
        for (int first = 0; first < rows.size.height(); first += 64) {
            if (cancellation.isCancellationRequested())
                return {};
            const int count = qMin(64, rows.size.height() - first);
            if (!rows.readRows(first, count, stride, input->pixels + first * stride,
                               count * stride)) {
                *error = QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                                     "The screenshot pixels could not be read");
                return {};
            }
        }
        raster = MappedRaster::create(outputSize, error);
        if (!raster)
            return {};
        if (!snow_shot_image_codec_resize_rgba8(
                input->pixels, uint32_t(input->size.width()), uint32_t(input->size.height()),
                raster->pixels, uint32_t(outputSize.width()), uint32_t(outputSize.height()),
                context, cancelled, backendError.data(), backendError.size())) {
            *error = QString::fromUtf8(backendError.data());
            return {};
        }
        rows = raster->rows();
    }
    if (cancellation.isCancellationRequested())
        return {};
    auto result = std::make_shared<Encoded>();
    if (!result->directory.isValid()) {
        *error = result->directory.errorString();
        return {};
    }
    result->options = options;
    result->path = result->directory.filePath(
        QStringLiteral("export.") + ScreenshotImageFileService::extension(options.format));
    QFile output(result->path);
    if (!output.open(QIODevice::WriteOnly)) {
        *error = output.errorString();
        return {};
    }
    rows.cancellationRequested = [&cancellation] { return cancellation.isCancellationRequested(); };
    if (!snow_shot::image_codec::encodeToDevice(
            rows, &output, ScreenshotImageFileService::snowImageFormat(options.format),
            ScreenshotImageFileService::encodeOptions(options.format, options.quality), error))
        return {};
    if (!output.flush()) {
        *error = output.errorString();
        return {};
    }
    output.close();
    if (cancellation.isCancellationRequested())
        return {};
    if (!previewOnly)
        return result;
    auto decoded = MappedRaster::create(outputSize, &result->previewError);
    if (decoded && snow_shot_image_codec_decode_file_into(
                       result->path.toUtf8().constData(), decoded->pixels,
                       uint32_t(outputSize.width()), uint32_t(outputSize.height()), context,
                       cancelled, backendError.data(), backendError.size())) {
        result->preview = decoded->thumbnail();
    } else if (result->previewError.isEmpty()) {
        result->previewError = QString::fromUtf8(backendError.data());
    }
    return cancellation.isCancellationRequested() ? nullptr : result;
}
} // namespace screenshot_save_export
