#pragma once

#include "codec_registry.h"

namespace snow::image::internal {

class IconCodec final : public Codec {
  public:
    explicit IconCodec(bool cursor) : cursor_(cursor) {}
    [[nodiscard]] Format format() const noexcept override {
        return cursor_ ? Format::cur : Format::ico;
    }
    [[nodiscard]] std::string_view name() const noexcept override {
        return cursor_ ? "snow CUR" : "snow ICO";
    }
    [[nodiscard]] CodecCapability capabilities() const noexcept override;
    [[nodiscard]] int probe(std::span<const std::byte> header,
                            std::string_view name_hint) const noexcept override;
    [[nodiscard]] Result<DocumentInfo> inspect(const Input& input, const DecodeOptions& options,
                                               std::stop_token stop) const override;
    [[nodiscard]] Result<Document> decode(const Input& input, const DecodeOptions& options,
                                          std::stop_token stop) const override;
    [[nodiscard]] Result<void> decode_to_sink(const Input& input, PixelSink& sink,
                                              const DecodeOptions& options,
                                              std::stop_token stop) const override;
    [[nodiscard]] Result<EncodedArtifactReceipt>
    encode_to_sink(const Document& document, const Output& output, const EncodeOptions& options,
                   std::stop_token stop) const override;

  private:
    bool cursor_ = false;
};

} // namespace snow::image::internal
