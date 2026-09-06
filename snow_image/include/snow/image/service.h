#pragma once

#include "snow/image/codec.h"
#include "snow/image/export.h"
#include "snow/image/raster.h"

#include <memory>
#include <span>
#include <stop_token>
#include <string_view>

namespace snow::image {

class DecodeSession;
class EncoderSession;

class SNOW_IMAGE_API Service final {
  public:
    Service();
    ~Service();
    Service(Service&&) noexcept;
    Service& operator=(Service&&) noexcept;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    [[nodiscard]] std::span<const FormatCapability> formats() const noexcept;
    [[nodiscard]] std::span<const EncoderInfo> encoders() const noexcept;
    [[nodiscard]] const EncoderInfo* encoder_info(Format format) const noexcept;
    [[nodiscard]] Result<RasterEncodeRoute>
    raster_encode_route(const DocumentDescriptor& descriptor, const EncodeOptions& options) const;
    [[nodiscard]] Result<Format> detect(const Input& input, std::stop_token stop = {}) const;
    [[nodiscard]] Result<DecodeSession> open_decoder(const Input& input,
                                                     std::stop_token stop = {}) const;
    [[nodiscard]] Result<DocumentInfo>
    inspect(const Input& input, const DecodeOptions& options = {}, std::stop_token stop = {}) const;
    [[nodiscard]] Result<DocumentDescriptor> inspect_raster(const Input& input,
                                                            const DecodeOptions& options = {},
                                                            std::stop_token stop = {}) const;
    [[nodiscard]] Result<Document> decode(const Input& input, const DecodeOptions& options = {},
                                          std::stop_token stop = {}) const;
    [[nodiscard]] Result<void> decode_to_sink(const Input& input, PixelSink& sink,
                                              const DecodeOptions& options = {},
                                              std::stop_token stop = {}) const;
    [[nodiscard]] Result<void> decode_into(const Input& input, RasterWriter& writer,
                                           const DecodeOptions& options = {},
                                           std::stop_token stop = {}) const;
    [[nodiscard]] Result<std::shared_ptr<RasterStore>>
    decode_to_store(const Input& input, const std::filesystem::path& path,
                    const DecodeOptions& decode_options = {},
                    const RasterStoreOptions& store_options = {}, std::stop_token stop = {}) const;
    [[nodiscard]] Result<EncoderSession> create_encoder(const DocumentDescriptor& descriptor,
                                                        const Output& output,
                                                        const EncodeOptions& options) const;
    [[nodiscard]] Result<EncodeResult> encode(const RasterSource& source, const Output& output,
                                              const EncodeOptions& options,
                                              std::stop_token stop = {}) const;
    [[nodiscard]] Result<EncodeResult> encode(const Document& document, const Output& output,
                                              const EncodeOptions& options,
                                              std::stop_token stop = {}) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class SNOW_IMAGE_API DecodeSession final {
  public:
    DecodeSession();
    ~DecodeSession();
    DecodeSession(DecodeSession&&) noexcept;
    DecodeSession& operator=(DecodeSession&&) noexcept;
    DecodeSession(const DecodeSession&) = delete;
    DecodeSession& operator=(const DecodeSession&) = delete;

    [[nodiscard]] Format format() const noexcept;
    [[nodiscard]] std::string_view codec_name() const noexcept;
    [[nodiscard]] Result<DocumentInfo> inspect(const DecodeOptions& options = {},
                                               std::stop_token stop = {}) const;
    [[nodiscard]] Result<DocumentDescriptor> inspect_raster(const DecodeOptions& options = {},
                                                            std::stop_token stop = {}) const;
    [[nodiscard]] Result<Document> decode(const DecodeOptions& options = {},
                                          std::stop_token stop = {}) const;
    [[nodiscard]] Result<void> decode_to_sink(PixelSink& sink, const DecodeOptions& options = {},
                                              std::stop_token stop = {}) const;
    [[nodiscard]] Result<void> decode_into(RasterWriter& writer, const DecodeOptions& options = {},
                                           std::stop_token stop = {}) const;

  private:
    class Impl;
    explicit DecodeSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class Service;
};

class SNOW_IMAGE_API EncoderSession final {
  public:
    EncoderSession();
    ~EncoderSession();
    EncoderSession(EncoderSession&&) noexcept;
    EncoderSession& operator=(EncoderSession&&) noexcept;
    EncoderSession(const EncoderSession&) = delete;
    EncoderSession& operator=(const EncoderSession&) = delete;

    [[nodiscard]] const DocumentDescriptor& descriptor() const noexcept;
    [[nodiscard]] Format format() const noexcept;
    [[nodiscard]] std::string_view codec_name() const noexcept;
    [[nodiscard]] Result<void> encode_frame(std::uint32_t frame_index, const RasterSource& source,
                                            std::stop_token stop = {});
    [[nodiscard]] Result<EncodeResult> finish(std::stop_token stop = {});

  private:
    class Impl;
    explicit EncoderSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class Service;
};

} // namespace snow::image
