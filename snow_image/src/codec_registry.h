#pragma once

#include "snow/image/service.h"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace snow::image::internal {

class Codec {
  public:
    virtual ~Codec() = default;
    [[nodiscard]] virtual Format format() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual CodecCapability capabilities() const noexcept = 0;
    [[nodiscard]] virtual int probe(std::span<const std::byte> header,
                                    std::string_view name_hint) const noexcept = 0;
    [[nodiscard]] virtual Result<DocumentInfo>
    inspect(const Input& input, const DecodeOptions& options, std::stop_token stop) const = 0;
    [[nodiscard]] virtual Result<DocumentDescriptor>
    inspect_raster(const Input& input, const DecodeOptions& options, std::stop_token stop) const;
    [[nodiscard]] virtual Result<Document> decode(const Input& input, const DecodeOptions& options,
                                                  std::stop_token stop) const = 0;
    [[nodiscard]] virtual Result<void> decode_to_sink(const Input& input, PixelSink& sink,
                                                      const DecodeOptions& options,
                                                      std::stop_token stop) const;
    [[nodiscard]] virtual Result<void> decode_into(const Input& input, RasterWriter& writer,
                                                   const DecodeOptions& options,
                                                   std::stop_token stop) const;
    [[nodiscard]] Result<EncodeResult> encode(const Document& document, const Output& output,
                                              const EncodeOptions& options,
                                              std::stop_token stop) const;
    [[nodiscard]] Result<EncodeResult> encode(const RasterSource& source, const Output& output,
                                              const EncodeOptions& options,
                                              std::stop_token stop) const;
    [[nodiscard]] virtual RasterEncodeRoute
    raster_encode_route(const DocumentDescriptor& descriptor,
                        const EncodeOptions& normalized_options) const noexcept;
    [[nodiscard]] virtual Result<EncodedArtifactReceipt>
    encode_to_sink(const Document& document, const Output& output, const EncodeOptions& options,
                   std::stop_token stop) const = 0;
    [[nodiscard]] virtual Result<EncodedArtifactReceipt>
    encode_raster_to_sink(const RasterSource& source, const Output& output,
                          const EncodeOptions& options, std::stop_token stop) const;
};

class CodecRegistry final {
  public:
    CodecRegistry();
    [[nodiscard]] std::span<const FormatCapability> formats() const noexcept {
        return formats_;
    }
    [[nodiscard]] std::span<const EncoderInfo> encoders() const noexcept {
        return encoders_;
    }
    [[nodiscard]] const EncoderInfo* encoder_info(Format format) const noexcept;
    [[nodiscard]] Result<std::shared_ptr<const Codec>> detect(const Input& input,
                                                              std::stop_token stop) const;
    [[nodiscard]] std::shared_ptr<const Codec> encoder(Format format) const noexcept;

  private:
    std::vector<std::shared_ptr<const Codec>> codecs_;
    std::vector<FormatCapability> formats_;
    std::vector<EncoderInfo> encoders_;
};

Status cancelled_status();
Result<void> validate_dimensions(std::uint32_t width, std::uint32_t height,
                                 const DecodeLimits& limits);

EncodedArtifactReceipt receipt_for_descriptor(const DocumentDescriptor& descriptor, Format format);
EncodedArtifactReceipt receipt_for_document(const Document& document, Format format);

} // namespace snow::image::internal
