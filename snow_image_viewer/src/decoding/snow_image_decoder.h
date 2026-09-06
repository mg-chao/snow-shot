#pragma once

#include "decoding/image_decoder.h"

#include <snow/image/document.h>

namespace snow::image_viewer {

DecodeResult prepareSnowDocument(const QString& filePath, snow::image::Document document,
                                 const QString& decoderName,
                                 const DecodeCancellation& cancellation);
DecodeResult prepareMappedSnowDocument(const QString& filePath,
                                       const snow::image::Document& document,
                                       const QString& decoderName,
                                       const DecodeCancellation& cancellation);

class SnowImageDecoder final : public ImageDecoder {
  public:
    bool canDecode(const QString& filePath, const QByteArray& header) const override;
    DecodeResult decode(const QString& filePath,
                        const DecodeCancellation& cancellation) const override;
};

} // namespace snow::image_viewer
