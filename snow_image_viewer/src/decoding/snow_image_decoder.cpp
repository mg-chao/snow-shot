#include "decoding/snow_image_decoder.h"

#include "core/image_raster_store.h"
#include "core/image_tile_store.h"

#include <snow/image/service.h>
#include <snow/image/processing.h>

#include <QFileInfo>
#include <QPainter>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace snow::image_viewer {
namespace {

constexpr std::uint64_t kMaximumResidentBytes = std::uint64_t{384} << 20U;
constexpr std::uint64_t kMaximumTileStripeBytes = std::uint64_t{128} << 20U;
constexpr std::uint64_t kMaximumTileCacheBytes = std::uint64_t{16} << 30U;
constexpr int kTileWidth = 512;
constexpr int kPreviewExtent = 4096;

QString statusText(const snow::image::Status& status) {
    QString message = QString::fromStdString(status.message).trimmed();
    if (message.isEmpty()) {
        message = QStringLiteral("snow_image could not decode this image.");
    }
    if (!status.codec.empty()) {
        message += QStringLiteral(" (%1)").arg(QString::fromStdString(status.codec));
    }
    return message;
}

snow::image::Result<snow::image::RasterAnalysis>
analyzeDocument(const snow::image::Document& document, std::stop_token stop) {
    snow::image::RasterAnalysis analysis;
    analysis.alpha_content = snow::image::AlphaContent::opaque;
    for (const snow::image::Frame& frame : document.frames) {
        const auto content = snow::image::classify_alpha(frame.image, stop);
        if (!content)
            return content.error();
        if (content.value() == snow::image::AlphaContent::non_opaque) {
            analysis.alpha_content = content.value();
            break;
        }
    }
    return analysis;
}

std::filesystem::path nativePath(const QString& filePath) {
    return std::filesystem::path(filePath.toStdU16String());
}

QColorSpace::Primaries qtPrimaries(snow::image::ColorPrimaries primaries) {
    switch (primaries) {
    case snow::image::ColorPrimaries::display_p3:
        return QColorSpace::Primaries::DciP3D65;
    case snow::image::ColorPrimaries::adobe_rgb:
        return QColorSpace::Primaries::AdobeRgb;
    case snow::image::ColorPrimaries::rec2020:
        return QColorSpace::Primaries::Bt2020;
    case snow::image::ColorPrimaries::srgb:
    case snow::image::ColorPrimaries::custom:
    case snow::image::ColorPrimaries::unknown:
        return QColorSpace::Primaries::SRgb;
    }
    return QColorSpace::Primaries::SRgb;
}

QColorSpace::TransferFunction qtTransfer(snow::image::TransferFunction transfer) {
    switch (transfer) {
    case snow::image::TransferFunction::linear:
        return QColorSpace::TransferFunction::Linear;
    case snow::image::TransferFunction::pq:
        return QColorSpace::TransferFunction::St2084;
    case snow::image::TransferFunction::hlg:
        return QColorSpace::TransferFunction::Hlg;
    case snow::image::TransferFunction::gamma:
        return QColorSpace::TransferFunction::Gamma;
    case snow::image::TransferFunction::srgb:
    case snow::image::TransferFunction::unknown:
        return QColorSpace::TransferFunction::SRgb;
    }
    return QColorSpace::TransferFunction::SRgb;
}

ColorMetadata colorMetadata(const snow::image::ColorEncoding& color) {
    ColorMetadata metadata;
    if (!color.icc_profile.empty() &&
        color.icc_profile.size() <=
            static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        const QByteArray profile(reinterpret_cast<const char*>(color.icc_profile.data()),
                                 static_cast<qsizetype>(color.icc_profile.size()));
        metadata.sourceColorSpace = QColorSpace::fromIccProfile(profile);
    }
    if (!metadata.sourceColorSpace.isValid()) {
        metadata.sourceColorSpace =
            QColorSpace(qtPrimaries(color.primaries), qtTransfer(color.transfer));
    }
    metadata.dynamicRange = color.dynamic_range == snow::image::DynamicRange::high
                                ? DynamicRange::High
                                : DynamicRange::Standard;
    metadata.sourcePeakNits = color.source_peak_nits;
    metadata.diffuseWhiteNits = color.diffuse_white_nits;
    return metadata;
}

void releaseWrappedImage(void* owner) {
    delete static_cast<std::shared_ptr<const void>*>(owner);
}

struct ResidentImageOwner final {
    // Destruction is reverse declaration order: release the mapping before the
    // backing-store owner attempts to remove its file on Windows.
    std::shared_ptr<ImageRasterStore> backingStore;
    std::shared_ptr<const void> mapping;
};

void releaseResidentImage(void* owner) {
    delete static_cast<ResidentImageOwner*>(owner);
}

QImage wrapImage(const snow::image::Image& source, QString* error) {
    if (source.width() == 0 || source.height() == 0 ||
        source.width() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        source.height() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        source.row_stride() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        *error = QStringLiteral("The decoded image dimensions exceed Qt's raster limits.");
        return {};
    }
    const snow::image::PixelFormat& format = source.format();
    QImage::Format qtFormat = QImage::Format_Invalid;
    if (format == snow::image::kGray8)
        qtFormat = QImage::Format_Grayscale8;
    else if (format == snow::image::kGray16)
        qtFormat = QImage::Format_Grayscale16;
    else if (format == snow::image::kRgb8)
        qtFormat = QImage::Format_RGB888;
    else if (format == snow::image::kRgba8)
        qtFormat = QImage::Format_RGBA8888;
    else if (format == snow::image::kBgra8)
        qtFormat = QImage::Format_ARGB32;
    else if (format == snow::image::kRgba16)
        qtFormat = QImage::Format_RGBA64;
    else if (format == snow::image::kRgba16Float)
        qtFormat = QImage::Format_RGBA16FPx4;
    else if (format == snow::image::kRgba32Float)
        qtFormat = QImage::Format_RGBA32FPx4;
    if (qtFormat == QImage::Format_Invalid) {
        *error = QStringLiteral("The decoded pixel layout is not supported by the viewer.");
        return {};
    }

    if (!source.storage().owner()) {
        *error = QStringLiteral("The decoded image storage has no lifetime owner.");
        return {};
    }
    auto* owner = new std::shared_ptr<const void>(source.storage().owner());
    QImage image(reinterpret_cast<const uchar*>(source.pixels().data()),
                 static_cast<int>(source.width()), static_cast<int>(source.height()),
                 static_cast<qsizetype>(source.row_stride()), qtFormat, &releaseWrappedImage,
                 owner);
    if (image.isNull()) {
        delete owner;
        *error = QStringLiteral("The decoded image could not be wrapped for display.");
    }
    return image;
}

class TileSink final : public snow::image::PixelSink {
  public:
    explicit TileSink(const DecodeCancellation& cancellation) : cancellation_(cancellation) {}

    ~TileSink() override {
        rasterStore_.reset();
        backingStore_.reset();
    }

    snow::image::Result<void> begin(const snow::image::DocumentInfo& document) override {
        if (document.canvas_width == 0 || document.canvas_height == 0 ||
            document.canvas_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            document.canvas_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            return failure(snow::image::ErrorCode::limit_exceeded,
                           "The tiled image dimensions exceed viewer limits.");
        }
        sourceSize_ = QSize(static_cast<int>(document.canvas_width),
                            static_cast<int>(document.canvas_height));
        if (document.frames.size() != 1 || document.frames.front().width != document.canvas_width ||
            document.frames.front().height != document.canvas_height ||
            document.frames.front().x != 0 || document.frames.front().y != 0 ||
            document.frames.front().native_format != snow::image::kRgba8) {
            return failure(snow::image::ErrorCode::unsupported_feature,
                           "Oversized tiled decoding requires one full-canvas RGBA8 frame.");
        }
        rowBytes_ = static_cast<std::size_t>(document.canvas_width) * 4U;
        if (rowBytes_ == 0 || rowBytes_ > kMaximumTileStripeBytes) {
            return failure(snow::image::ErrorCode::limit_exceeded,
                           "One image row exceeds the tile working-memory budget.");
        }
        stripeRows_ = static_cast<int>(std::max<std::uint64_t>(
            1, std::min<std::uint64_t>(512, kMaximumTileStripeBytes / rowBytes_)));
        stripe_.resize(rowBytes_ * static_cast<std::size_t>(stripeRows_));

        const QSize previewSize =
            sourceSize_.scaled(kPreviewExtent, kPreviewExtent, Qt::KeepAspectRatio);
        preview_ = QImage(previewSize, QImage::Format_RGBA8888_Premultiplied);
        if (preview_.isNull()) {
            return failure(snow::image::ErrorCode::io_error,
                           "Could not create the temporary image tile cache.");
        }
        snow::image::Result<snow::image::DocumentDescriptor> descriptor =
            snow::image::describe_document(document);
        if (!descriptor)
            return descriptor.error();
        descriptor.value().frames.front().layout.alpha = snow::image::AlphaMode::premultiplied;
        snow::image::RasterStoreOptions options;
        options.chunk_rows = static_cast<std::uint32_t>(stripeRows_);
        options.limits.maximum_pixel_bytes = kMaximumTileCacheBytes;
        QString error;
        backingStore_ = ImageRasterStore::create(std::move(descriptor).value(), options, &error);
        if (!backingStore_) {
            return failure(snow::image::ErrorCode::io_error, error.toUtf8().constData());
        }
        rasterStore_ = backingStore_->store();
        preview_.fill(Qt::transparent);
        preview_.setColorSpace(QColorSpace(QColorSpace::SRgb));
        return {};
    }

    snow::image::Result<void> begin_frame(std::uint32_t frameIndex,
                                          const snow::image::FrameInfo& frame) override {
        if (frameIndex != 0 || frame.width != static_cast<std::uint32_t>(sourceSize_.width()) ||
            frame.height != static_cast<std::uint32_t>(sourceSize_.height()) || frame.x != 0 ||
            frame.y != 0 || frame.native_format != snow::image::kRgba8) {
            return failure(snow::image::ErrorCode::unsupported_feature,
                           "Oversized tiled decoding requires one full-canvas RGBA8 frame.");
        }
        if (!rasterStore_) {
            return failure(snow::image::ErrorCode::internal_error,
                           "The temporary raster store was not initialized.");
        }
        return {};
    }

    snow::image::Result<void> write_rows(std::uint32_t firstRow, std::uint32_t rowCount,
                                         std::size_t rowStride,
                                         std::span<const std::byte> pixels) override {
        if (firstRow != expectedRow_ || rowStride < rowBytes_ ||
            rowCount > (pixels.size() / rowStride)) {
            return failure(snow::image::ErrorCode::corrupt_data,
                           "The streaming decoder emitted an invalid row sequence.");
        }
        for (std::uint32_t relative = 0; relative < rowCount; ++relative) {
            if (cancellation_.isCancelled()) {
                return failure(snow::image::ErrorCode::cancelled, "Image decoding was cancelled.");
            }
            const std::byte* source =
                pixels.data() + static_cast<std::size_t>(relative) * rowStride;
            std::byte* destination =
                stripe_.data() + static_cast<std::size_t>(stripeRowsFilled_) * rowBytes_;
            for (int x = 0; x < sourceSize_.width(); ++x) {
                const std::size_t offset = static_cast<std::size_t>(x) * 4U;
                const std::uint32_t alpha = std::to_integer<std::uint8_t>(source[offset + 3U]);
                if (alpha != 0xFFU)
                    alphaOpaque_ = false;
                destination[offset] = static_cast<std::byte>(
                    (std::to_integer<std::uint8_t>(source[offset]) * alpha + 127U) / 255U);
                destination[offset + 1U] = static_cast<std::byte>(
                    (std::to_integer<std::uint8_t>(source[offset + 1U]) * alpha + 127U) / 255U);
                destination[offset + 2U] = static_cast<std::byte>(
                    (std::to_integer<std::uint8_t>(source[offset + 2U]) * alpha + 127U) / 255U);
                destination[offset + 3U] = source[offset + 3U];
            }
            writePreviewRow(expectedRow_, destination);
            ++expectedRow_;
            ++stripeRowsFilled_;
            if (stripeRowsFilled_ == stripeRows_ ||
                expectedRow_ == static_cast<std::uint32_t>(sourceSize_.height())) {
                snow::image::Result<void> status = flushStripe();
                if (!status)
                    return status;
            }
        }
        return {};
    }

    snow::image::Result<void> end_frame(std::uint32_t frameIndex) override {
        if (frameIndex != 0 || expectedRow_ != static_cast<std::uint32_t>(sourceSize_.height()) ||
            stripeRowsFilled_ != 0) {
            return failure(snow::image::ErrorCode::truncated_data,
                           "The streaming decoder did not emit a complete image.");
        }
        return {};
    }

    snow::image::Result<void> end() override {
        if (!rasterStore_) {
            return failure(snow::image::ErrorCode::internal_error,
                           "The temporary raster store was not initialized.");
        }
        backingStore_->setVerifiedAlphaContent(alphaOpaque_
                                                   ? snow::image::AlphaContent::opaque
                                                   : snow::image::AlphaContent::non_opaque);
        snow::image::Result<void> committed = rasterStore_->commit();
        if (!committed)
            return committed;
        store_ = std::make_shared<ImageTileStore>(backingStore_, sourceSize_, preview_,
                                                  QSize(kTileWidth, stripeRows_));
        rasterStore_.reset();
        backingStore_.reset();
        return {};
    }

    std::shared_ptr<ImageTileStore> takeStore() {
        return std::move(store_);
    }

  private:
    snow::image::Status failure(snow::image::ErrorCode code, const char* message) const {
        return snow::image::Status::error(code, message, "snow_image_viewer tile cache");
    }

    void writePreviewRow(std::uint32_t sourceY, const std::byte* source) {
        while (nextPreviewRow_ < preview_.height() &&
               (static_cast<qint64>(nextPreviewRow_) * sourceSize_.height()) / preview_.height() ==
                   sourceY) {
            auto* destination = reinterpret_cast<std::byte*>(preview_.scanLine(nextPreviewRow_));
            for (int x = 0; x < preview_.width(); ++x) {
                const int sourceX = static_cast<int>(
                    (static_cast<qint64>(x) * sourceSize_.width()) / preview_.width());
                std::memcpy(destination + static_cast<std::size_t>(x) * 4U,
                            source + static_cast<std::size_t>(sourceX) * 4U, 4U);
            }
            ++nextPreviewRow_;
        }
    }

    snow::image::Result<void> flushStripe() {
        const int stripeStartY = static_cast<int>(expectedRow_) - stripeRowsFilled_;
        if (!rasterStore_) {
            return failure(snow::image::ErrorCode::internal_error,
                           "The temporary raster store was not initialized.");
        }
        snow::image::Result<void> written = rasterStore_->write_rows(
            0, 0, static_cast<std::uint32_t>(stripeStartY),
            static_cast<std::uint32_t>(stripeRowsFilled_), rowBytes_,
            std::span<const std::byte>(stripe_.data(),
                                       rowBytes_ * static_cast<std::size_t>(stripeRowsFilled_)),
            cancellation_.token());
        if (!written)
            return written;
        stripeRowsFilled_ = 0;
        return {};
    }

    const DecodeCancellation& cancellation_;
    std::shared_ptr<ImageRasterStore> backingStore_;
    std::shared_ptr<snow::image::RasterStore> rasterStore_;
    QSize sourceSize_;
    QImage preview_;
    std::vector<std::byte> stripe_;
    std::shared_ptr<ImageTileStore> store_;
    std::size_t rowBytes_ = 0;
    std::uint32_t expectedRow_ = 0;
    bool alphaOpaque_ = true;
    int stripeRows_ = 0;
    int stripeRowsFilled_ = 0;
    int nextPreviewRow_ = 0;
};

std::uint64_t residentEstimate(const snow::image::DocumentInfo& info) {
    std::uint64_t total = 0;
    const bool highPrecisionWorking =
        info.color.dynamic_range == snow::image::DynamicRange::high ||
        (info.color.primaries != snow::image::ColorPrimaries::unknown &&
         info.color.primaries != snow::image::ColorPrimaries::srgb) ||
        (info.color.transfer != snow::image::TransferFunction::unknown &&
         info.color.transfer != snow::image::TransferFunction::srgb);
    for (const snow::image::FrameInfo& frame : info.frames) {
        snow::image::Result<std::size_t> bytesPerPixel = frame.native_format.bytes_per_pixel();
        std::uint64_t pixelBytes = bytesPerPixel ? bytesPerPixel.value() : 16U;
        const bool directSrgbRgba =
            !highPrecisionWorking && frame.native_format == snow::image::kRgba8;
        if (!directSrgbRgba) {
            const std::uint64_t workingBytes = highPrecisionWorking ? 8U : 4U;
            if (pixelBytes > std::numeric_limits<std::uint64_t>::max() - workingBytes)
                return std::numeric_limits<std::uint64_t>::max();
            pixelBytes += workingBytes;
        }
        const std::uint64_t pixels = static_cast<std::uint64_t>(frame.width) * frame.height;
        if (pixels > (std::numeric_limits<std::uint64_t>::max() - total) / pixelBytes) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        total += pixels * pixelBytes;
    }
    return total;
}

bool hasStreamingCapability(const snow::image::Service& service, snow::image::Format format) {
    for (const snow::image::FormatCapability& capability : service.formats()) {
        if (capability.format == format) {
            return snow::image::has_capability(capability.capabilities,
                                               snow::image::CodecCapability::streaming_decode);
        }
    }
    return false;
}

DecodeResult tiledDecode(const QString& filePath, snow::image::DecodeSession& session,
                         const snow::image::DocumentInfo& info,
                         const DecodeCancellation& cancellation) {
    TileSink sink(cancellation);
    snow::image::DecodeOptions options;
    options.orientation = snow::image::OrientationPolicy::apply;
    options.output_format = snow::image::kRgba8;
    options.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
    options.limits.maximum_working_bytes = std::uint64_t{256} << 20U;
    snow::image::Result<void> decoded = session.decode_to_sink(sink, options, cancellation.token());
    if (!decoded)
        return DecodeResult::failure(statusText(decoded.error()));
    std::shared_ptr<ImageTileStore> store = sink.takeStore();
    if (!store) {
        return DecodeResult::failure(QStringLiteral("The tiled decoder returned no image cache."));
    }

    DecodedImage result;
    result.filePath = QFileInfo(filePath).absoluteFilePath();
    result.sourceSize = store->sourceSize();
    result.pixels = store->preview();
    result.pixelEncoding = PixelEncoding::Srgb8;
    result.color = colorMetadata(info.color);
    result.color.sourceColorSpace = QColorSpace(QColorSpace::SRgb);
    result.color.description = QStringLiteral("sRGB tiled image");
    result.color.transferDescription = QStringLiteral("sRGB");
    result.decoderName = QString::fromUtf8(session.codec_name().data(),
                                           static_cast<qsizetype>(session.codec_name().size()));
    result.rasterStore = store->rasterStore();
    if (result.rasterStore) {
        result.analysis = result.rasterStore->analysis();
    }
    result.tileStore = std::move(store);
    return {std::move(result), {}};
}

bool supportsNativeTileStore(const snow::image::DocumentDescriptor& descriptor) {
    if (descriptor.frames.size() != 1 || descriptor.canvas_width == 0 ||
        descriptor.canvas_height == 0 ||
        descriptor.canvas_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        descriptor.canvas_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        return false;
    const snow::image::RasterFrameDescriptor& frame = descriptor.frames.front();
    if (frame.width != descriptor.canvas_width || frame.height != descriptor.canvas_height ||
        frame.x != 0 || frame.y != 0 || frame.layout.planes.empty() ||
        frame.layout.planes.front().semantic == snow::image::PlaneSemantic::packed)
        return false;
    return frame.layout.color_model == snow::image::ColorModel::gray ||
           frame.layout.color_model == snow::image::ColorModel::ycbcr;
}

DecodeResult nativeTiledDecode(const QString& filePath, snow::image::DecodeSession& session,
                               snow::image::DocumentDescriptor descriptor,
                               const DecodeCancellation& cancellation) {
    snow::image::RasterStoreOptions storeOptions;
    storeOptions.chunk_rows = 256;
    storeOptions.limits.maximum_pixel_bytes = kMaximumTileCacheBytes;
    bool alphaCapable = false;
    for (const auto& plane : descriptor.frames.front().layout.planes) {
        alphaCapable = alphaCapable || plane.format.alpha != snow::image::AlphaMode::none;
    }
    if (!alphaCapable)
        storeOptions.analysis.alpha_content = snow::image::AlphaContent::opaque;
    QString backingError;
    std::shared_ptr<ImageRasterStore> backingStore =
        ImageRasterStore::create(descriptor, storeOptions, &backingError);
    if (!backingStore)
        return DecodeResult::failure(backingError);
    const std::shared_ptr<snow::image::RasterStore>& rasterStore = backingStore->store();

    snow::image::DecodeOptions nativeOptions;
    nativeOptions.orientation = snow::image::OrientationPolicy::apply;
    nativeOptions.raster_layout = snow::image::RasterLayoutPolicy::native;
    nativeOptions.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
    nativeOptions.limits.maximum_working_bytes = std::uint64_t{256} << 20U;
    snow::image::Result<void> decoded =
        session.decode_into(*rasterStore, nativeOptions, cancellation.token());
    if (!decoded)
        return DecodeResult::failure(statusText(decoded.error()));

    snow::image::DecodeOptions previewOptions;
    previewOptions.orientation = snow::image::OrientationPolicy::apply;
    previewOptions.output_format = snow::image::kRgba8;
    previewOptions.maximum_extent = kPreviewExtent;
    previewOptions.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
    previewOptions.limits.maximum_working_bytes = std::uint64_t{256} << 20U;
    snow::image::Result<snow::image::Document> previewDocument =
        session.decode(previewOptions, cancellation.token());
    if (!previewDocument)
        return DecodeResult::failure(statusText(previewDocument.error()));
    const QString decoderName = QString::fromUtf8(
        session.codec_name().data(), static_cast<qsizetype>(session.codec_name().size()));
    DecodeResult preview = prepareSnowDocument(filePath, std::move(previewDocument).value(),
                                               decoderName, cancellation);
    if (!preview.succeeded())
        return preview;
    if (preview.image.analysis.alpha_content)
        backingStore->setAnalysis(preview.image.analysis);

    const QSize sourceSize(static_cast<int>(descriptor.canvas_width),
                           static_cast<int>(descriptor.canvas_height));
    std::shared_ptr<ImageTileStore> store = std::make_shared<ImageTileStore>(
        backingStore, sourceSize, preview.image.pixels, QSize(kTileWidth, 256));
    preview.image.sourceSize = sourceSize;
    preview.image.rasterStore = std::move(backingStore);
    preview.image.tileStore = std::move(store);
    return preview;
}

qint64 frameDurationMilliseconds(std::chrono::nanoseconds duration) {
    if (duration.count() <= 0)
        return 100;
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    return std::max<qint64>(1, milliseconds.count());
}

bool supportsPackedResidentStore(const snow::image::DocumentDescriptor& descriptor) {
    if (descriptor.frames.size() != 1 || descriptor.canvas_width == 0 ||
        descriptor.canvas_height == 0 ||
        descriptor.canvas_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        descriptor.canvas_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const snow::image::RasterFrameDescriptor& frame = descriptor.frames.front();
    if (frame.layout.planes.size() != 1)
        return false;
    const snow::image::PixelFormat& format = frame.layout.planes.front().format;
    return frame.width == descriptor.canvas_width && frame.height == descriptor.canvas_height &&
           frame.x == 0 && frame.y == 0 &&
           frame.layout.planes.front().semantic == snow::image::PlaneSemantic::packed &&
           (format == snow::image::kRgba8 || format == snow::image::kRgba16);
}

DecodeResult residentRasterDecode(const QString& filePath, snow::image::DecodeSession& session,
                                  snow::image::DocumentDescriptor descriptor,
                                  const DecodeCancellation& cancellation) {
    snow::image::RasterStoreOptions storeOptions;
    storeOptions.chunk_rows = 256;
    storeOptions.limits.maximum_pixel_bytes = kMaximumTileCacheBytes;
    QString backingError;
    std::shared_ptr<ImageRasterStore> backingStore =
        ImageRasterStore::create(descriptor, storeOptions, &backingError);
    if (!backingStore)
        return DecodeResult::failure(backingError);

    snow::image::DecodeOptions options;
    options.orientation = snow::image::OrientationPolicy::apply;
    options.output_format = descriptor.frames.front().layout.planes.front().format;
    options.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
    options.limits.maximum_working_bytes = std::uint64_t{256} << 20U;
    const auto decoded = session.decode_into(*backingStore->store(), options, cancellation.token());
    if (!decoded)
        return DecodeResult::failure(statusText(decoded.error()));

    const snow::image::RasterFrameDescriptor& frame = descriptor.frames.front();
    auto mapped = backingStore->store()->map_plane(0, 0);
    if (!mapped)
        return DecodeResult::failure(statusText(mapped.error()));
    if (mapped.value().row_stride >
        static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        return DecodeResult::failure(
            QStringLiteral("The decoded raster stride exceeds Qt's image limit."));
    }
    const snow::image::PixelFormat& format = frame.layout.planes.front().format;
    const auto alphaContent = snow::image::classify_alpha(
        snow::image::ImageView{frame.width, frame.height, format, mapped.value().row_stride,
                               mapped.value().pixels},
        cancellation.token());
    if (!alphaContent)
        return DecodeResult::failure(statusText(alphaContent.error()));
    backingStore->setVerifiedAlphaContent(alphaContent.value());
    snow::image::RasterAnalysis analysis;
    analysis.alpha_content = alphaContent.value();
    const QImage::Format qtFormat =
        format == snow::image::kRgba16 ? QImage::Format_RGBA64 : QImage::Format_RGBA8888;
    auto* owner = new ResidentImageOwner{backingStore, mapped.value().owner};
    QImage pixels(reinterpret_cast<const uchar*>(mapped.value().pixels.data()),
                  static_cast<int>(frame.width), static_cast<int>(frame.height),
                  static_cast<qsizetype>(mapped.value().row_stride), qtFormat,
                  &releaseResidentImage, owner);
    if (pixels.isNull()) {
        delete owner;
        return DecodeResult::failure(
            QStringLiteral("The decoded raster could not be mapped for display."));
    }
    ColorMetadata metadata = colorMetadata(descriptor.color);
    pixels.setColorSpace(metadata.sourceColorSpace);
    const QString decoderName = QString::fromUtf8(
        session.codec_name().data(), static_cast<qsizetype>(session.codec_name().size()));
    DecodeResult prepared = prepareDecodedImage(filePath, std::move(pixels), std::move(metadata),
                                                decoderName, analysis);
    if (!prepared.succeeded())
        return prepared;
    prepared.image.rasterStore = std::move(backingStore);
    return prepared;
}

DecodeResult residentDecode(const QString& filePath, snow::image::DecodeSession& session,
                            const DecodeCancellation& cancellation) {
    snow::image::DecodeOptions options;
    options.orientation = snow::image::OrientationPolicy::apply;
    options.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
    snow::image::Result<snow::image::Document> decoded =
        session.decode(options, cancellation.token());
    if (!decoded)
        return DecodeResult::failure(statusText(decoded.error()));
    snow::image::Document document = std::move(decoded).value();
    const QString decoderName = QString::fromUtf8(
        session.codec_name().data(), static_cast<qsizetype>(session.codec_name().size()));
    return prepareSnowDocument(filePath, std::move(document), decoderName, cancellation);
}

} // namespace

DecodeResult prepareSnowDocument(const QString& filePath, snow::image::Document document,
                                 const QString& decoderName,
                                 const DecodeCancellation& cancellation) {
    if (document.frames.empty()) {
        return DecodeResult::failure(
            QStringLiteral("The image document contains no display frame."));
    }
    const auto documentAnalysis = analyzeDocument(document, cancellation.token());
    if (!documentAnalysis)
        return DecodeResult::failure(statusText(documentAnalysis.error()));
    const ColorMetadata metadata = colorMetadata(document.color);
    const bool animated =
        document.frames.size() > 1 &&
        std::any_of(document.frames.begin(), document.frames.end(),
                    [](const snow::image::Frame& frame) { return frame.duration.count() > 0; });
    if (!animated) {
        QString error;
        QImage image = wrapImage(document.frames.front().image, &error);
        if (image.isNull())
            return DecodeResult::failure(error);
        image.setColorSpace(metadata.sourceColorSpace);
        DecodeResult result = prepareDecodedImage(filePath, std::move(image), metadata, decoderName,
                                                  documentAnalysis.value());
        if (result.succeeded() && document.canvas_width > 0 && document.canvas_height > 0 &&
            document.canvas_width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
            document.canvas_height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            result.image.sourceSize = QSize(static_cast<int>(document.canvas_width),
                                            static_cast<int>(document.canvas_height));
        }
        return result;
    }

    if (document.canvas_width == 0 || document.canvas_height == 0 ||
        document.canvas_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        document.canvas_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return DecodeResult::failure(
            QStringLiteral("The animation canvas dimensions are invalid."));
    }
    const QSize canvasSize(static_cast<int>(document.canvas_width),
                           static_cast<int>(document.canvas_height));
    QImage canvas(canvasSize, QImage::Format_RGBA8888_Premultiplied);
    if (canvas.isNull()) {
        return DecodeResult::failure(
            QStringLiteral("The animation canvas could not be allocated."));
    }
    canvas.fill(Qt::transparent);

    DecodedImage resultImage;
    for (const snow::image::Frame& frame : document.frames) {
        if (cancellation.isCancelled()) {
            return DecodeResult::failure(QStringLiteral("Image decoding was cancelled."));
        }
        QString error;
        QImage source = wrapImage(frame.image, &error);
        if (source.isNull())
            return DecodeResult::failure(error);
        source = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        const QImage previous =
            frame.disposal == snow::image::FrameDisposal::previous ? canvas.copy() : QImage{};
        {
            QPainter painter(&canvas);
            painter.setCompositionMode(frame.blend == snow::image::FrameBlend::source
                                           ? QPainter::CompositionMode_Source
                                           : QPainter::CompositionMode_SourceOver);
            painter.drawImage(QPoint(static_cast<int>(frame.x), static_cast<int>(frame.y)), source);
        }
        DecodeResult prepared = prepareDecodedImage(filePath, canvas.copy(), metadata, decoderName,
                                                    documentAnalysis.value());
        if (!prepared.succeeded())
            return prepared;
        prepared.image.sourceSize = canvasSize;
        if (resultImage.filePath.isEmpty())
            resultImage = prepared.image;
        resultImage.animationFrames.push_back(
            {prepared.image.pixels, frameDurationMilliseconds(frame.duration)});

        if (frame.disposal == snow::image::FrameDisposal::background) {
            QPainter painter(&canvas);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(QRect(static_cast<int>(frame.x), static_cast<int>(frame.y),
                                   source.width(), source.height()),
                             Qt::transparent);
        } else if (frame.disposal == snow::image::FrameDisposal::previous) {
            canvas = previous;
        }
    }
    resultImage.loopCount =
        document.loop_count > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(document.loop_count);
    resultImage.pixels = resultImage.animationFrames.front().pixels;
    resultImage.analysis = documentAnalysis.value();
    return {std::move(resultImage), {}};
}

DecodeResult prepareMappedSnowDocument(const QString& filePath,
                                       const snow::image::Document& document,
                                       const QString& decoderName,
                                       const DecodeCancellation& cancellation) {
    const bool animated =
        document.frames.size() > 1 &&
        std::any_of(document.frames.begin(), document.frames.end(),
                    [](const snow::image::Frame& frame) { return frame.duration.count() > 0; });
    if (animated) {
        return prepareSnowDocument(filePath, document, decoderName, cancellation);
    }
    if (document.frames.empty()) {
        return DecodeResult::failure(QStringLiteral("The preview package has no display frame."));
    }
    const auto documentAnalysis = analyzeDocument(document, cancellation.token());
    if (!documentAnalysis)
        return DecodeResult::failure(statusText(documentAnalysis.error()));
    QString error;
    QImage pixels = wrapImage(document.frames.front().image, &error);
    if (pixels.isNull())
        return DecodeResult::failure(error);
    ColorMetadata metadata = colorMetadata(document.color);
    if (!metadata.sourceColorSpace.isValid())
        metadata.sourceColorSpace = QColorSpace(QColorSpace::SRgb);
    pixels.setColorSpace(metadata.sourceColorSpace);
    DecodedImage image;
    image.filePath = QFileInfo(filePath).absoluteFilePath();
    image.sourceSize =
        QSize(static_cast<int>(document.canvas_width), static_cast<int>(document.canvas_height));
    image.pixels = std::move(pixels);
    image.pixelEncoding = document.frames.front().image.format().sample_type ==
                                  snow::image::SampleType::floating_point
                              ? PixelEncoding::LinearScRgb16F
                              : PixelEncoding::Srgb8;
    image.pixelsPremultiplied = false;
    image.color = std::move(metadata);
    image.decoderName = decoderName;
    image.analysis = documentAnalysis.value();
    return {std::move(image), {}};
}

bool SnowImageDecoder::canDecode(const QString& filePath, const QByteArray& header) const {
    static const snow::image::Service service;
    const auto bytes =
        std::as_bytes(std::span(header.constData(), static_cast<std::size_t>(header.size())));
    const std::string hint = QFileInfo(filePath).fileName().toUtf8().toStdString();
    const snow::image::Input input = snow::image::memory_input(bytes, hint);
    if (service.detect(input))
        return true;
    return snow::image::format_from_extension(hint) != snow::image::Format::unknown;
}

DecodeResult SnowImageDecoder::decode(const QString& filePath,
                                      const DecodeCancellation& cancellation) const {
    if (cancellation.isCancelled()) {
        return DecodeResult::failure(QStringLiteral("Image decoding was cancelled."));
    }
    snow::image::Result<snow::image::Input> input = snow::image::file_input(nativePath(filePath));
    if (!input)
        return DecodeResult::failure(statusText(input.error()));
    snow::image::Service service;
    snow::image::Result<snow::image::DecodeSession> opened =
        service.open_decoder(input.value(), cancellation.token());
    if (!opened)
        return DecodeResult::failure(statusText(opened.error()));
    snow::image::DecodeSession session = std::move(opened).value();
    snow::image::DecodeOptions inspectOptions;
    inspectOptions.orientation = snow::image::OrientationPolicy::apply;
    inspectOptions.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
    snow::image::Result<snow::image::DocumentInfo> inspected =
        session.inspect(inspectOptions, cancellation.token());
    if (!inspected)
        return DecodeResult::failure(statusText(inspected.error()));

    if (residentEstimate(inspected.value()) > kMaximumResidentBytes &&
        hasStreamingCapability(service, session.format())) {
        snow::image::DecodeOptions nativeOptions;
        nativeOptions.orientation = snow::image::OrientationPolicy::apply;
        nativeOptions.raster_layout = snow::image::RasterLayoutPolicy::native;
        nativeOptions.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
        nativeOptions.limits.maximum_working_bytes = std::uint64_t{256} << 20U;
        snow::image::Result<snow::image::DocumentDescriptor> nativeDescriptor =
            session.inspect_raster(nativeOptions, cancellation.token());
        if (!nativeDescriptor)
            return DecodeResult::failure(statusText(nativeDescriptor.error()));
        if (supportsNativeTileStore(nativeDescriptor.value())) {
            return nativeTiledDecode(filePath, session, std::move(nativeDescriptor).value(),
                                     cancellation);
        }
        return tiledDecode(filePath, session, inspected.value(), cancellation);
    }
    if (session.format() == snow::image::Format::jpeg ||
        session.format() == snow::image::Format::webp) {
        snow::image::DecodeOptions nativeOptions;
        nativeOptions.orientation = snow::image::OrientationPolicy::apply;
        nativeOptions.raster_layout = snow::image::RasterLayoutPolicy::native;
        nativeOptions.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
        nativeOptions.limits.maximum_working_bytes = std::uint64_t{256} << 20U;
        snow::image::Result<snow::image::DocumentDescriptor> nativeDescriptor =
            session.inspect_raster(nativeOptions, cancellation.token());
        if (nativeDescriptor && supportsNativeTileStore(nativeDescriptor.value())) {
            return nativeTiledDecode(filePath, session, std::move(nativeDescriptor).value(),
                                     cancellation);
        }
    }
    if (hasStreamingCapability(service, session.format())) {
        snow::image::DecodeOptions packedOptions;
        packedOptions.orientation = snow::image::OrientationPolicy::apply;
        packedOptions.limits.maximum_owned_output_bytes = kMaximumResidentBytes;
        packedOptions.limits.maximum_working_bytes = std::uint64_t{256} << 20U;
        auto descriptor = session.inspect_raster(packedOptions, cancellation.token());
        if (descriptor && supportsPackedResidentStore(descriptor.value())) {
            return residentRasterDecode(filePath, session, std::move(descriptor).value(),
                                        cancellation);
        }
    }
    return residentDecode(filePath, session, cancellation);
}

} // namespace snow::image_viewer
