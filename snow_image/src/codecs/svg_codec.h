#pragma once

#include "codec_registry.h"

namespace snow::image::internal {

class SvgCodec final : public Codec {
  public:
    explicit SvgCodec(bool gzip) : gzip_(gzip) {}
    [[nodiscard]] Format format() const noexcept override {
        return gzip_ ? Format::svgz : Format::svg;
    }
    [[nodiscard]] std::string_view name() const noexcept override {
        return gzip_ ? "lunasvg / gzip" : "lunasvg";
    }
    [[nodiscard]] CodecCapability capabilities() const noexcept override;
    [[nodiscard]] int probe(std::span<const std::byte> header,
                            std::string_view name_hint) const noexcept override;
    [[nodiscard]] Result<DocumentInfo> inspect(const Input& input, const DecodeOptions& options,
                                               std::stop_token stop) const override;
    [[nodiscard]] Result<Document> decode(const Input& input, const DecodeOptions& options,
                                          std::stop_token stop) const override;
    [[nodiscard]] Result<EncodedArtifactReceipt>
    encode_to_sink(const Document& document, const Output& output, const EncodeOptions& options,
                   std::stop_token stop) const override;

  private:
    bool gzip_ = false;
};

} // namespace snow::image::internal
