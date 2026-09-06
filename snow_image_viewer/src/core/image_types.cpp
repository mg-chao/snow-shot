#include "core/image_types.h"

#include <QFileInfo>

#include <utility>

namespace snow::image_viewer {
namespace {

bool needsLinearWorkingSpace(const QColorSpace& colorSpace, DynamicRange dynamicRange) {
    if (dynamicRange == DynamicRange::High) {
        return true;
    }
    if (!colorSpace.isValid()) {
        return false;
    }
    return colorSpace.primaries() != QColorSpace::Primaries::SRgb ||
           colorSpace.transferFunction() == QColorSpace::TransferFunction::Linear;
}

QString primariesName(QColorSpace::Primaries primaries) {
    switch (primaries) {
    case QColorSpace::Primaries::SRgb:
        return QStringLiteral("sRGB / Rec.709");
    case QColorSpace::Primaries::AdobeRgb:
        return QStringLiteral("Adobe RGB");
    case QColorSpace::Primaries::DciP3D65:
        return QStringLiteral("Display P3");
    case QColorSpace::Primaries::ProPhotoRgb:
        return QStringLiteral("ProPhoto RGB");
    case QColorSpace::Primaries::Bt2020:
        return QStringLiteral("Rec.2020");
    case QColorSpace::Primaries::Custom:
    default:
        return QStringLiteral("Custom RGB");
    }
}

} // namespace

bool isHdrColorSpace(const QColorSpace& colorSpace) {
    if (!colorSpace.isValid()) {
        return false;
    }
    const auto transfer = colorSpace.transferFunction();
    return transfer == QColorSpace::TransferFunction::St2084 ||
           transfer == QColorSpace::TransferFunction::Hlg;
}

QString describeTransferFunction(const QColorSpace& colorSpace) {
    if (!colorSpace.isValid()) {
        return QStringLiteral("Unknown transfer");
    }
    switch (colorSpace.transferFunction()) {
    case QColorSpace::TransferFunction::Linear:
        return QStringLiteral("Linear");
    case QColorSpace::TransferFunction::Gamma:
        return QStringLiteral("Gamma");
    case QColorSpace::TransferFunction::SRgb:
        return QStringLiteral("sRGB");
    case QColorSpace::TransferFunction::ProPhotoRgb:
        return QStringLiteral("ProPhoto");
    case QColorSpace::TransferFunction::Bt2020:
        return QStringLiteral("BT.2020");
    case QColorSpace::TransferFunction::St2084:
        return QStringLiteral("PQ");
    case QColorSpace::TransferFunction::Hlg:
        return QStringLiteral("HLG");
    case QColorSpace::TransferFunction::Custom:
    default:
        return QStringLiteral("ICC");
    }
}

QString describeColorSpace(const QColorSpace& colorSpace) {
    if (!colorSpace.isValid()) {
        return QStringLiteral("Unprofiled");
    }
    if (!colorSpace.description().trimmed().isEmpty()) {
        return colorSpace.description().trimmed();
    }
    return primariesName(colorSpace.primaries()) + QStringLiteral(" / ") +
           describeTransferFunction(colorSpace);
}

DecodeResult prepareDecodedImage(const QString& filePath, QImage source, ColorMetadata metadata,
                                 const QString& decoderName, snow::image::RasterAnalysis analysis) {
    if (source.isNull() || !source.size().isValid()) {
        return DecodeResult::failure(QStringLiteral("The decoder returned an empty image."));
    }

    QColorSpace sourceSpace =
        metadata.sourceColorSpace.isValid() ? metadata.sourceColorSpace : source.colorSpace();
    if (!sourceSpace.isValid()) {
        sourceSpace = QColorSpace(QColorSpace::SRgb);
        metadata.usedFallbackProfile = true;
    }
    source.setColorSpace(sourceSpace);

    if (isHdrColorSpace(sourceSpace)) {
        metadata.dynamicRange = DynamicRange::High;
    }
    if (metadata.dynamicRange == DynamicRange::High && metadata.sourcePeakNits <= 0.0F) {
        metadata.sourcePeakNits = 1000.0F;
    }
    if (sourceSpace.transferFunction() == QColorSpace::TransferFunction::St2084) {
        metadata.linearScaleToScRgb = 10000.0F / 80.0F;
    } else if (sourceSpace.transferFunction() == QColorSpace::TransferFunction::Hlg) {
        metadata.linearScaleToScRgb = metadata.sourcePeakNits / 80.0F;
    }
    metadata.sourceColorSpace = sourceSpace;
    metadata.description = metadata.description.trimmed().isEmpty()
                               ? describeColorSpace(sourceSpace)
                               : metadata.description.trimmed();
    metadata.transferDescription = metadata.transferDescription.trimmed().isEmpty()
                                       ? describeTransferFunction(sourceSpace)
                                       : metadata.transferDescription.trimmed();

    DecodedImage decoded;
    decoded.filePath = QFileInfo(filePath).absoluteFilePath();
    decoded.sourceSize = source.size();
    decoded.color = metadata;
    decoded.decoderName = decoderName;
    decoded.analysis = analysis;

    if (needsLinearWorkingSpace(sourceSpace, metadata.dynamicRange)) {
        const QColorSpace workingSpace(QColorSpace::SRgbLinear);
        if (source.colorSpace() == workingSpace && source.format() == QImage::Format_RGBA16FPx4) {
            decoded.pixels = std::move(source);
        } else {
            decoded.pixels = source.convertedToColorSpace(workingSpace, QImage::Format_RGBA16FPx4,
                                                          Qt::AutoColor);
        }
        decoded.pixelEncoding = PixelEncoding::LinearScRgb16F;
    } else {
        const QColorSpace workingSpace(QColorSpace::SRgb);
        if (source.colorSpace() == workingSpace && source.format() == QImage::Format_RGBA8888) {
            decoded.pixels = std::move(source);
        } else {
            decoded.pixels =
                source.convertedToColorSpace(workingSpace, QImage::Format_RGBA8888, Qt::AutoColor);
        }
        decoded.pixelEncoding = PixelEncoding::Srgb8;
    }
    decoded.pixelsPremultiplied = false;

    if (decoded.pixels.isNull()) {
        return DecodeResult::failure(
            QStringLiteral("The image could not be converted for rendering."));
    }
    return {std::move(decoded), {}};
}

} // namespace snow::image_viewer
