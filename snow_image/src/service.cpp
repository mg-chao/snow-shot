#include "snow/image/service.h"

#include "codec_registry.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace snow::image {
namespace {

bool supported_jpeg_sampling(ChromaSubsampling sampling) {
    return sampling == ChromaSubsampling::yuv444 || sampling == ChromaSubsampling::yuv422 ||
           sampling == ChromaSubsampling::yuv420;
}

bool compatible_descriptor(const DocumentDescriptor& expected, const DocumentDescriptor& actual) {
    if (expected.canvas_width != actual.canvas_width ||
        expected.canvas_height != actual.canvas_height ||
        expected.frames.size() != actual.frames.size())
        return false;
    for (std::size_t index = 0; index < expected.frames.size(); ++index) {
        const RasterFrameDescriptor& left = expected.frames[index];
        const RasterFrameDescriptor& right = actual.frames[index];
        if (left.width != right.width || left.height != right.height || left.x != right.x ||
            left.y != right.y || left.layout.planes.size() != 1 ||
            right.layout.planes.size() != 1 ||
            left.layout.planes.front() != right.layout.planes.front())
            return false;
    }
    return true;
}

Result<Image> encoder_frame_image(const RasterSource& source, std::uint32_t frame_index,
                                  const RasterFrameDescriptor& frame, std::stop_token stop) {
    if (stop.stop_requested())
        return internal::cancelled_status();
    const PlaneDescriptor& plane = frame.layout.planes.front();
    if (has_access(source.access(), RasterAccess::mapped_planes)) {
        Result<MappedPlane> mapped = source.map_plane(frame_index, 0);
        if (!mapped)
            return mapped.error();
        Result<SharedPixelBuffer> buffer =
            SharedPixelBuffer::adopt(std::move(mapped.value().owner), mapped.value().pixels);
        if (!buffer)
            return buffer.error();
        return Image::adopt(frame.width, frame.height, plane.format, mapped.value().row_stride,
                            std::move(buffer).value());
    }

    Result<MutableImage> allocated =
        MutableImage::allocate(frame.width, frame.height, plane.format, 64);
    if (!allocated)
        return allocated.error();
    MutableImage image = std::move(allocated).value();
    Result<void> read =
        source.read_rows(frame_index, 0, 0, frame.height, image.row_stride(), image.pixels(), stop);
    if (!read)
        return read.error();
    return std::move(image).freeze();
}

Result<void> validate_encoder_limits(const EncoderInfo& encoder,
                                     const DocumentDescriptor& descriptor) {
    if (descriptor.canvas_width > encoder.limits.maximum_width ||
        descriptor.canvas_height > encoder.limits.maximum_height ||
        descriptor.frames.size() > encoder.limits.maximum_frames) {
        return Status::error(ErrorCode::limit_exceeded,
                             "Image dimensions or frame count exceed the selected encoder limits.");
    }
    for (const RasterFrameDescriptor& frame : descriptor.frames) {
        if (frame.width > encoder.limits.maximum_width ||
            frame.height > encoder.limits.maximum_height) {
            return Status::error(ErrorCode::limit_exceeded,
                                 "A frame exceeds the selected encoder dimension limits.");
        }
    }
    return {};
}

Result<void> validate_encoder_limits(const EncoderInfo& encoder, const Document& document) {
    if (document.canvas_width > encoder.limits.maximum_width ||
        document.canvas_height > encoder.limits.maximum_height ||
        document.frames.size() > encoder.limits.maximum_frames) {
        return Status::error(ErrorCode::limit_exceeded,
                             "Image dimensions or frame count exceed the selected encoder limits.");
    }
    for (const Frame& frame : document.frames) {
        if (frame.image.width() > encoder.limits.maximum_width ||
            frame.image.height() > encoder.limits.maximum_height) {
            return Status::error(ErrorCode::limit_exceeded,
                                 "A frame exceeds the selected encoder dimension limits.");
        }
    }
    return {};
}

} // namespace

Result<EncodeOptions> normalize_encode_options(const EncoderInfo& encoder,
                                               const EncodeOptions& options) {
    if (encoder.format == Format::unknown) {
        return Status::error(ErrorCode::invalid_argument,
                             "Cannot normalize options for an unknown encoder.");
    }
    EncodeOptions normalized = options;
    normalized.format = encoder.format;
    const auto normalize_range = [](int value, const EncoderOptionRange& range) {
        return std::clamp(value, range.minimum, range.maximum);
    };
    normalized.quality = has_feature(encoder.features, EncoderFeature::quality)
                             ? normalize_range(options.quality, encoder.quality)
                             : 0;
    normalized.effort = has_feature(encoder.features, EncoderFeature::effort)
                            ? normalize_range(options.effort, encoder.effort)
                            : 0;
    normalized.lossless_effort =
        has_feature(encoder.features, EncoderFeature::lossless)
            ? normalize_range(options.lossless_effort, encoder.lossless_effort)
            : 0;
    normalized.compression_level =
        has_feature(encoder.features, EncoderFeature::compression_level)
            ? normalize_range(options.compression_level, encoder.compression_level)
            : 0;
    normalized.lossless =
        has_feature(encoder.features, EncoderFeature::lossless) && options.lossless;
    normalized.progressive =
        has_feature(encoder.features, EncoderFeature::progressive) && options.progressive;
    normalized.interlaced =
        has_feature(encoder.features, EncoderFeature::interlaced) && options.interlaced;
    normalized.preserve_metadata =
        has_feature(encoder.features, EncoderFeature::metadata) && options.preserve_metadata;
    if (encoder.format == Format::webp) {
        if (normalized.lossless) {
            normalized.quality = 0;
            normalized.effort = 0;
        } else {
            normalized.lossless_effort = 0;
        }
    }
    if (!has_feature(encoder.features, EncoderFeature::chroma_subsampling)) {
        normalized.chroma_subsampling.reset();
    } else if (normalized.chroma_subsampling &&
               !supported_jpeg_sampling(*normalized.chroma_subsampling)) {
        return Status::error(ErrorCode::invalid_argument,
                             "JPEG chroma subsampling must be Auto, 4:4:4, 4:2:2, or 4:2:0.");
    }
    return normalized;
}

Result<ChromaSubsampling> resolve_jpeg_chroma_subsampling(const EncodeOptions& normalized_options,
                                                          bool grayscale) {
    if (grayscale)
        return ChromaSubsampling::none;
    if (normalized_options.chroma_subsampling) {
        if (!supported_jpeg_sampling(*normalized_options.chroma_subsampling)) {
            return Status::error(ErrorCode::invalid_argument,
                                 "JPEG chroma subsampling must be Auto, 4:4:4, 4:2:2, or 4:2:0.");
        }
        return *normalized_options.chroma_subsampling;
    }
    if (normalized_options.quality >= 90)
        return ChromaSubsampling::yuv444;
    if (normalized_options.quality >= 80)
        return ChromaSubsampling::yuv422;
    return ChromaSubsampling::yuv420;
}

class Service::Impl final {
  public:
    internal::CodecRegistry registry;
};

class DecodeSession::Impl final {
  public:
    Impl(std::shared_ptr<const internal::Codec> selected_codec, Input selected_input)
        : codec(std::move(selected_codec)), input(std::move(selected_input)) {}

    std::shared_ptr<const internal::Codec> codec;
    Input input;
};

class EncoderSession::Impl final {
  public:
    Impl(std::shared_ptr<const internal::Codec> selected_codec,
         DocumentDescriptor selected_descriptor, Output selected_output,
         EncodeOptions selected_options)
        : codec(std::move(selected_codec)), descriptor(std::move(selected_descriptor)),
          output(std::move(selected_output)), options(selected_options) {
        document.format = options.format == Format::unknown ? descriptor.format : options.format;
        document.canvas_width = descriptor.canvas_width;
        document.canvas_height = descriptor.canvas_height;
        document.loop_count = descriptor.loop_count;
        document.metadata = descriptor.metadata;
        document.color = descriptor.color;
        document.frames.reserve(descriptor.frames.size());
    }

    std::shared_ptr<const internal::Codec> codec;
    DocumentDescriptor descriptor;
    Output output;
    EncodeOptions options;
    Document document;
    std::uint32_t next_frame = 0;
    bool finished = false;
};

Service::Service() : impl_(std::make_unique<Impl>()) {}
Service::~Service() = default;
Service::Service(Service&&) noexcept = default;
Service& Service::operator=(Service&&) noexcept = default;

std::span<const FormatCapability> Service::formats() const noexcept {
    return impl_->registry.formats();
}

std::span<const EncoderInfo> Service::encoders() const noexcept {
    return impl_->registry.encoders();
}

const EncoderInfo* Service::encoder_info(Format format) const noexcept {
    return impl_->registry.encoder_info(format);
}

Result<RasterEncodeRoute> Service::raster_encode_route(const DocumentDescriptor& descriptor,
                                                       const EncodeOptions& options) const {
    Result<void> descriptor_status = descriptor.validate();
    if (!descriptor_status)
        return descriptor_status.error();
    const Format requested = options.format == Format::unknown ? descriptor.format : options.format;
    std::shared_ptr<const internal::Codec> codec = impl_->registry.encoder(requested);
    const EncoderInfo* info = impl_->registry.encoder_info(requested);
    if (!codec || !info) {
        return Status::error(ErrorCode::codec_unavailable,
                             "No encoder is available for the requested format.");
    }
    Result<EncodeOptions> normalized = normalize_encode_options(*info, options);
    if (!normalized)
        return normalized.error();
    Result<void> limits = validate_encoder_limits(*info, descriptor);
    if (!limits)
        return limits.error();
    return codec->raster_encode_route(descriptor, normalized.value());
}

Result<Format> Service::detect(const Input& input, std::stop_token stop) const {
    try {
        Result<std::shared_ptr<const internal::Codec>> codec = impl_->registry.detect(input, stop);
        if (!codec) {
            return codec.error();
        }
        return codec.value()->format();
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Format detection ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Format detection failed unexpectedly.");
    }
}

Result<DecodeSession> Service::open_decoder(const Input& input, std::stop_token stop) const {
    try {
        Result<std::shared_ptr<const internal::Codec>> codec = impl_->registry.detect(input, stop);
        if (!codec) {
            return codec.error();
        }
        return DecodeSession(std::make_unique<DecodeSession::Impl>(codec.value(), input));
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Could not create a decode session.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Could not create a decode session.");
    }
}

Result<DocumentInfo> Service::inspect(const Input& input, const DecodeOptions& options,
                                      std::stop_token stop) const {
    Result<DecodeSession> session = open_decoder(input, stop);
    if (!session) {
        return session.error();
    }
    return session.value().inspect(options, stop);
}

Result<DocumentDescriptor> Service::inspect_raster(const Input& input, const DecodeOptions& options,
                                                   std::stop_token stop) const {
    Result<DecodeSession> session = open_decoder(input, stop);
    if (!session)
        return session.error();
    return session.value().inspect_raster(options, stop);
}

Result<Document> Service::decode(const Input& input, const DecodeOptions& options,
                                 std::stop_token stop) const {
    Result<DecodeSession> session = open_decoder(input, stop);
    if (!session) {
        return session.error();
    }
    return session.value().decode(options, stop);
}

Result<void> Service::decode_to_sink(const Input& input, PixelSink& sink,
                                     const DecodeOptions& options, std::stop_token stop) const {
    Result<DecodeSession> session = open_decoder(input, stop);
    if (!session) {
        return session.error();
    }
    return session.value().decode_to_sink(sink, options, stop);
}

Result<void> Service::decode_into(const Input& input, RasterWriter& writer,
                                  const DecodeOptions& options, std::stop_token stop) const {
    Result<DecodeSession> session = open_decoder(input, stop);
    if (!session)
        return session.error();
    return session.value().decode_into(writer, options, stop);
}

Result<std::shared_ptr<RasterStore>>
Service::decode_to_store(const Input& input, const std::filesystem::path& path,
                         const DecodeOptions& decode_options,
                         const RasterStoreOptions& store_options, std::stop_token stop) const {
    Result<DecodeSession> opened = open_decoder(input, stop);
    if (!opened)
        return opened.error();
    DecodeSession session = std::move(opened).value();
    Result<DocumentDescriptor> inspected = session.inspect_raster(decode_options, stop);
    if (!inspected)
        return inspected.error();
    Result<std::shared_ptr<RasterStore>> created =
        RasterStore::create(path, std::move(inspected).value(), store_options);
    if (!created)
        return created.error();
    std::shared_ptr<RasterStore> store = created.value();
    Result<void> decoded = session.decode_into(*store, decode_options, stop);
    if (!decoded) {
        store->abort();
        return decoded.error();
    }
    return store;
}

Result<EncoderSession> Service::create_encoder(const DocumentDescriptor& descriptor,
                                               const Output& output,
                                               const EncodeOptions& options) const {
    try {
        Result<void> descriptor_status = descriptor.validate();
        if (!descriptor_status)
            return descriptor_status.error();
        if (!output.sink)
            return Status::error(ErrorCode::invalid_argument, "Output has no byte sink.");
        const Format requested =
            options.format == Format::unknown ? descriptor.format : options.format;
        std::shared_ptr<const internal::Codec> codec = impl_->registry.encoder(requested);
        if (!codec)
            return Status::error(ErrorCode::codec_unavailable,
                                 "No encoder is available for the requested format.");
        const EncoderInfo* encoder_info = impl_->registry.encoder_info(requested);
        Result<EncodeOptions> normalized = normalize_encode_options(*encoder_info, options);
        if (!normalized)
            return normalized.error();
        Result<void> limits = validate_encoder_limits(*encoder_info, descriptor);
        if (!limits)
            return limits.error();
        return EncoderSession(std::make_unique<EncoderSession::Impl>(
            codec, descriptor, output, std::move(normalized).value()));
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Could not create an encoder session.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Could not create an encoder session.");
    }
}

Result<EncodeResult> Service::encode(const RasterSource& source, const Output& output,
                                     const EncodeOptions& options, std::stop_token stop) const {
    try {
        Result<void> descriptor_status = source.descriptor().validate();
        if (!descriptor_status)
            return descriptor_status.error();
        if (!output.sink)
            return Status::error(ErrorCode::invalid_argument, "Output has no byte sink.");
        const Format requested =
            options.format == Format::unknown ? source.descriptor().format : options.format;
        std::shared_ptr<const internal::Codec> codec = impl_->registry.encoder(requested);
        if (!codec)
            return Status::error(ErrorCode::codec_unavailable,
                                 "No encoder is available for the requested format.");
        const EncoderInfo* encoder_info = impl_->registry.encoder_info(requested);
        Result<EncodeOptions> normalized = normalize_encode_options(*encoder_info, options);
        if (!normalized)
            return normalized.error();
        Result<void> limits = validate_encoder_limits(*encoder_info, source.descriptor());
        if (!limits)
            return limits.error();
        return codec->encode(source, output, normalized.value(), stop);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Raster encoding ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Raster encoding failed unexpectedly.");
    }
}

Result<EncodeResult> Service::encode(const Document& document, const Output& output,
                                     const EncodeOptions& options, std::stop_token stop) const {
    try {
        if (!output.sink) {
            return Status::error(ErrorCode::invalid_argument, "Output has no byte sink.");
        }
        const Format requested =
            options.format == Format::unknown ? document.format : options.format;
        std::shared_ptr<const internal::Codec> codec = impl_->registry.encoder(requested);
        if (!codec) {
            return Status::error(ErrorCode::codec_unavailable,
                                 "No encoder is available for the requested format.");
        }
        const EncoderInfo* encoder_info = impl_->registry.encoder_info(requested);
        Result<EncodeOptions> normalized = normalize_encode_options(*encoder_info, options);
        if (!normalized)
            return normalized.error();
        Result<void> limits = validate_encoder_limits(*encoder_info, document);
        if (!limits)
            return limits.error();
        return codec->encode(document, output, normalized.value(), stop);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Image encoding ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Image encoding failed unexpectedly.");
    }
}

DecodeSession::DecodeSession() = default;
DecodeSession::DecodeSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
DecodeSession::~DecodeSession() = default;
DecodeSession::DecodeSession(DecodeSession&&) noexcept = default;
DecodeSession& DecodeSession::operator=(DecodeSession&&) noexcept = default;

Format DecodeSession::format() const noexcept {
    return impl_ ? impl_->codec->format() : Format::unknown;
}

std::string_view DecodeSession::codec_name() const noexcept {
    return impl_ ? impl_->codec->name() : std::string_view{};
}

Result<DocumentInfo> DecodeSession::inspect(const DecodeOptions& options,
                                            std::stop_token stop) const {
    if (!impl_) {
        return Status::error(ErrorCode::invalid_argument, "Decode session is empty.");
    }
    try {
        return impl_->codec->inspect(impl_->input, options, stop);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Image inspection ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Image inspection failed unexpectedly.");
    }
}

Result<Document> DecodeSession::decode(const DecodeOptions& options, std::stop_token stop) const {
    if (!impl_) {
        return Status::error(ErrorCode::invalid_argument, "Decode session is empty.");
    }
    try {
        return impl_->codec->decode(impl_->input, options, stop);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Image decoding ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Image decoding failed unexpectedly.");
    }
}

Result<void> DecodeSession::decode_to_sink(PixelSink& sink, const DecodeOptions& options,
                                           std::stop_token stop) const {
    if (!impl_) {
        return Status::error(ErrorCode::invalid_argument, "Decode session is empty.");
    }
    try {
        return impl_->codec->decode_to_sink(impl_->input, sink, options, stop);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Streamed decoding ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Streamed decoding failed unexpectedly.");
    }
}

Result<DocumentDescriptor> DecodeSession::inspect_raster(const DecodeOptions& options,
                                                         std::stop_token stop) const {
    if (!impl_)
        return Status::error(ErrorCode::invalid_argument, "Decode session is empty.");
    try {
        return impl_->codec->inspect_raster(impl_->input, options, stop);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Raster inspection ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Raster inspection failed unexpectedly.");
    }
}

Result<void> DecodeSession::decode_into(RasterWriter& writer, const DecodeOptions& options,
                                        std::stop_token stop) const {
    if (!impl_)
        return Status::error(ErrorCode::invalid_argument, "Decode session is empty.");
    try {
        Result<void> status = impl_->codec->decode_into(impl_->input, writer, options, stop);
        if (!status)
            writer.abort();
        return status;
    } catch (const std::bad_alloc&) {
        writer.abort();
        return Status::error(ErrorCode::out_of_memory, "Raster decoding ran out of memory.");
    } catch (...) {
        writer.abort();
        return Status::error(ErrorCode::internal_error, "Raster decoding failed unexpectedly.");
    }
}

EncoderSession::EncoderSession() = default;
EncoderSession::EncoderSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EncoderSession::~EncoderSession() = default;
EncoderSession::EncoderSession(EncoderSession&&) noexcept = default;
EncoderSession& EncoderSession::operator=(EncoderSession&&) noexcept = default;

const DocumentDescriptor& EncoderSession::descriptor() const noexcept {
    static const DocumentDescriptor empty;
    return impl_ ? impl_->descriptor : empty;
}

Format EncoderSession::format() const noexcept {
    return impl_ ? impl_->codec->format() : Format::unknown;
}

std::string_view EncoderSession::codec_name() const noexcept {
    return impl_ ? impl_->codec->name() : std::string_view{};
}

Result<void> EncoderSession::encode_frame(std::uint32_t frame_index, const RasterSource& source,
                                          std::stop_token stop) {
    if (!impl_ || impl_->finished)
        return Status::error(ErrorCode::invalid_argument,
                             "Encoder session is empty or already finished.");
    if (frame_index != impl_->next_frame || frame_index >= impl_->descriptor.frames.size() ||
        !compatible_descriptor(impl_->descriptor, source.descriptor()))
        return Status::error(ErrorCode::invalid_argument,
                             "Encoder frames must be supplied in descriptor order.");
    const RasterFrameDescriptor& descriptor_frame = impl_->descriptor.frames[frame_index];
    if (descriptor_frame.layout.planes.size() != 1 ||
        descriptor_frame.layout.planes.front().semantic != PlaneSemantic::packed)
        return Status::error(ErrorCode::unsupported_feature,
                             "The compatibility encoder accepts one packed plane per frame.");
    try {
        Result<Image> image = encoder_frame_image(source, frame_index, descriptor_frame, stop);
        if (!image)
            return image.error();
        Frame frame;
        frame.image = std::move(image).value();
        frame.x = descriptor_frame.x;
        frame.y = descriptor_frame.y;
        frame.duration = descriptor_frame.duration;
        frame.blend = descriptor_frame.blend;
        frame.disposal = descriptor_frame.disposal;
        frame.metadata = descriptor_frame.metadata;
        frame.color = descriptor_frame.color;
        frame.cursor_hotspot = descriptor_frame.cursor_hotspot;
        impl_->document.frames.push_back(std::move(frame));
        ++impl_->next_frame;
        return {};
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory,
                             "Could not retain a compatibility encoder frame.");
    }
}

Result<EncodeResult> EncoderSession::finish(std::stop_token stop) {
    if (!impl_ || impl_->finished)
        return Status::error(ErrorCode::invalid_argument,
                             "Encoder session is empty or already finished.");
    if (impl_->next_frame != impl_->descriptor.frames.size())
        return Status::error(ErrorCode::invalid_argument,
                             "Encoder session is missing one or more frames.");
    impl_->finished = true;
    try {
        return impl_->codec->encode(impl_->document, impl_->output, impl_->options, stop);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::out_of_memory, "Image encoding ran out of memory.");
    } catch (...) {
        return Status::error(ErrorCode::internal_error, "Image encoding failed unexpectedly.");
    }
}

} // namespace snow::image
