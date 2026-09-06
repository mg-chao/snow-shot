#include "editing/raster_package.h"

#include <QFile>
#include <QSharedMemory>

#include <filesystem>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace snow::image_viewer {
namespace {

constexpr std::uint32_t kNoFrame = std::numeric_limits<std::uint32_t>::max();

void setError(QString* error, const QString& message) {
    if (error)
        *error = message;
}

QString statusText(const snow::image::Status& status) {
    return QString::fromStdString(status.message);
}

bool validSharedMemoryKey(const QString& key) {
    constexpr qsizetype kPrefixLength = 13; // snow-edit-v1-
    if (!key.startsWith(QStringLiteral("snow-edit-v1-")) ||
        key.size() != kPrefixLength + 32 + 1 + 32)
        return false;
    const auto isHex = [](QChar value) {
        return (value >= QLatin1Char('0') && value <= QLatin1Char('9')) ||
               (value >= QLatin1Char('a') && value <= QLatin1Char('f')) ||
               (value >= QLatin1Char('A') && value <= QLatin1Char('F'));
    };
    if (key.at(kPrefixLength + 32) != QLatin1Char('-'))
        return false;
    for (qsizetype index = kPrefixLength; index < key.size(); ++index) {
        if (index == kPrefixLength + 32)
            continue;
        if (!isHex(key.at(index)))
            return false;
    }
    return true;
}

bool compatibleVerifiedAlpha(const snow::image::DocumentDescriptor& descriptor,
                             std::optional<snow::image::AlphaContent> alphaContent) {
    if (alphaContent != snow::image::AlphaContent::non_opaque)
        return true;
    return std::any_of(descriptor.frames.begin(), descriptor.frames.end(),
                       [](const snow::image::RasterFrameDescriptor& frame) {
                           return std::any_of(
                               frame.layout.planes.begin(), frame.layout.planes.end(),
                               [](const snow::image::PlaneDescriptor& plane) {
                                   return plane.semantic == snow::image::PlaneSemantic::alpha ||
                                          plane.format.alpha != snow::image::AlphaMode::none;
                               });
                       });
}

bool supportsCompatibilityDocument(const snow::image::DocumentDescriptor& descriptor) {
    return !descriptor.frames.empty() &&
           std::all_of(descriptor.frames.begin(), descriptor.frames.end(),
                       [](const snow::image::RasterFrameDescriptor& frame) {
                           return frame.layout.planes.size() == 1 &&
                                  frame.layout.planes.front().semantic ==
                                      snow::image::PlaneSemantic::packed;
                       });
}

std::filesystem::path nativePath(const QString& path) {
#if defined(Q_OS_WIN)
    return std::filesystem::path(path.toStdWString());
#else
    const QByteArray encoded = path.toUtf8();
    return std::filesystem::path(encoded.constData());
#endif
}

snow::image::Result<snow::image::Document> mappedDocument(const snow::image::RasterSource& store) {
    try {
        const snow::image::DocumentDescriptor& descriptor = store.descriptor();
        if (descriptor.frames.empty()) {
            return snow::image::Status::error(snow::image::ErrorCode::invalid_argument,
                                              "The raster package has no backing store.",
                                              "raster package");
        }
        snow::image::Document document;
        document.format = descriptor.format;
        document.canvas_width = descriptor.canvas_width;
        document.canvas_height = descriptor.canvas_height;
        document.loop_count = descriptor.loop_count;
        document.metadata = descriptor.metadata;
        document.color = descriptor.color;
        document.frames.reserve(descriptor.frames.size());
        for (std::uint32_t index = 0; index < descriptor.frames.size(); ++index) {
            const snow::image::RasterFrameDescriptor& source = descriptor.frames[index];
            if (source.layout.planes.size() != 1 ||
                source.layout.planes.front().semantic != snow::image::PlaneSemantic::packed) {
                return snow::image::Status::error(
                    snow::image::ErrorCode::unsupported_feature,
                    "The compatibility document view requires one packed plane per frame.",
                    "raster package");
            }
            snow::image::Result<snow::image::MappedPlane> mapped = store.map_plane(index, 0);
            if (!mapped)
                return mapped.error();
            snow::image::Result<snow::image::SharedPixelBuffer> buffer =
                snow::image::SharedPixelBuffer::adopt(mapped.value().owner, mapped.value().pixels);
            if (!buffer)
                return buffer.error();
            snow::image::Result<snow::image::Image> image = snow::image::Image::adopt(
                source.width, source.height, source.layout.planes.front().format,
                mapped.value().row_stride, std::move(buffer).value());
            if (!image)
                return image.error();
            snow::image::Frame frame;
            frame.image = std::move(image).value();
            frame.x = source.x;
            frame.y = source.y;
            frame.duration = source.duration;
            frame.blend = source.blend;
            frame.disposal = source.disposal;
            frame.metadata = source.metadata;
            frame.color = source.color;
            frame.cursor_hotspot = source.cursor_hotspot;
            document.frames.push_back(std::move(frame));
        }
        return document;
    } catch (const std::bad_alloc&) {
        return snow::image::Status::error(snow::image::ErrorCode::out_of_memory,
                                          "Could not allocate the mapped document view.",
                                          "raster package");
    }
}

snow::image::Status invalidSink(const char* message) {
    return snow::image::Status::error(snow::image::ErrorCode::invalid_argument, message,
                                      "raster package");
}

} // namespace

struct MappedRasterSink::Impl final {
    Impl(QString outputPath, std::optional<snow::image::AlphaContent> content)
        : path(std::move(outputPath)), alphaContent(content) {}

    QString path;
    std::optional<snow::image::AlphaContent> alphaContent;
    snow::image::DocumentInfo info;
    std::shared_ptr<snow::image::RasterStore> store;
    std::optional<snow::image::MutableMappedPlane> activeMapping;
    std::uint64_t fileBytes = 0;
    std::uint32_t activeFrame = kNoFrame;
    std::uint32_t completedFrames = 0;
    bool complete = false;
    bool packageTaken = false;
};

MappedRasterPackage::~MappedRasterPackage() = default;

void MappedRasterPackage::retainCleanupOwner(std::shared_ptr<const void> owner) {
    cleanupOwner_ = std::move(owner);
}

std::shared_ptr<MappedRasterPackage>
MappedRasterPackage::create(const QString& path, const snow::image::Document& document,
                            QString* error, std::shared_ptr<const void> cleanupOwner,
                            std::optional<snow::image::AlphaContent> alphaContent) {
    snow::image::Result<snow::image::DocumentDescriptor> described =
        snow::image::describe_document(document);
    if (!described) {
        setError(error, statusText(described.error()));
        return {};
    }
    snow::image::Result<std::shared_ptr<snow::image::RasterStore>> created =
        snow::image::RasterStore::create(nativePath(path), std::move(described).value());
    if (!created) {
        setError(error, statusText(created.error()));
        return {};
    }
    std::shared_ptr<snow::image::RasterStore> store = std::move(created).value();
    for (std::uint32_t index = 0; index < document.frames.size(); ++index) {
        const snow::image::Image& image = document.frames[index].image;
        snow::image::Result<void> copied =
            store->copy_plane(index, 0, image.row_stride(), image.pixels());
        if (!copied) {
            setError(error, statusText(copied.error()));
            store->abort();
            return {};
        }
    }
    if (alphaContent) {
        snow::image::Result<void> analysis =
            store->set_analysis(snow::image::RasterAnalysis{alphaContent});
        if (!analysis) {
            setError(error, statusText(analysis.error()));
            store->abort();
            return {};
        }
    }
    snow::image::Result<void> committed = store->commit();
    if (!committed) {
        setError(error, statusText(committed.error()));
        store->abort();
        return {};
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return fromStore(path, std::move(store), error, std::move(cleanupOwner));
}

std::shared_ptr<MappedRasterPackage>
MappedRasterPackage::open(const QString& path, QString* error,
                          std::shared_ptr<const void> cleanupOwner) {
    auto package = std::shared_ptr<MappedRasterPackage>(new MappedRasterPackage());
    if (!package->mapAndValidate(path, error, std::move(cleanupOwner)))
        return {};
    return package;
}

std::shared_ptr<MappedRasterPackage>
MappedRasterPackage::createShared(const QString& key, const snow::image::Document& document,
                                  std::optional<snow::image::AlphaContent> alphaContent,
                                  QString* error) {
    if (!validSharedMemoryKey(key)) {
        setError(error, QStringLiteral("The shared raster key is invalid."));
        return {};
    }
    snow::image::Result<snow::image::DocumentDescriptor> described =
        snow::image::describe_document(document);
    if (!described) {
        setError(error, statusText(described.error()));
        return {};
    }
    snow::image::RasterBufferStoreOptions options;
    options.row_alignment = 1;
    options.analysis.alpha_content = alphaContent;
    if (!options.analysis.alpha_content) {
        snow::image::AlphaContent content = snow::image::AlphaContent::opaque;
        for (const snow::image::Frame& frame : document.frames) {
            const auto classified = snow::image::classify_alpha(frame.image, {});
            if (!classified) {
                setError(error, statusText(classified.error()));
                return {};
            }
            if (classified.value() == snow::image::AlphaContent::non_opaque) {
                content = classified.value();
                break;
            }
        }
        options.analysis.alpha_content = content;
    }
    const auto required =
        snow::image::RasterBufferStore::required_bytes(described.value(), options);
    if (!required ||
        required.value() > static_cast<std::uint64_t>(std::numeric_limits<qsizetype>::max())) {
        setError(error, required ? QStringLiteral("The shared raster is too large.")
                                 : statusText(required.error()));
        return {};
    }
    auto memory = std::make_shared<QSharedMemory>(key);
    if (!memory->create(static_cast<qsizetype>(required.value()))) {
        setError(error, QStringLiteral("Could not create shared raster storage: %1")
                            .arg(memory->errorString()));
        return {};
    }
    if (static_cast<std::uint64_t>(memory->size()) < required.value()) {
        setError(error, QStringLiteral("The shared raster storage is smaller than requested."));
        return {};
    }
    auto* address = static_cast<std::byte*>(memory->data());
    if (!address || !memory->lock()) {
        setError(error, QStringLiteral("Could not map shared raster storage."));
        return {};
    }
    auto created = snow::image::RasterBufferStore::create(
        std::span<std::byte>(address, static_cast<std::size_t>(required.value())),
        std::move(described).value(), options);
    if (!created) {
        memory->unlock();
        setError(error, statusText(created.error()));
        return {};
    }
    std::shared_ptr<snow::image::RasterBufferStore> store = std::move(created).value();
    for (std::uint32_t index = 0; index < document.frames.size(); ++index) {
        const snow::image::Image& image = document.frames[index].image;
        const auto written =
            store->write_rows(index, 0, 0, image.height(), image.row_stride(), image.pixels());
        if (!written) {
            memory->unlock();
            setError(error, statusText(written.error()));
            store->abort();
            return {};
        }
    }
    const auto committed = store->commit();
    memory->unlock();
    if (!committed) {
        setError(error, statusText(committed.error()));
        store->abort();
        return {};
    }
    return fromBuffer(key, std::move(store), std::move(memory), error,
                      alphaContent.value_or(options.analysis.alpha_content.value()));
}

std::shared_ptr<MappedRasterPackage>
MappedRasterPackage::openShared(const QString& key, std::uint64_t expectedSize,
                                std::array<std::byte, 16> expectedNonce, QString* error) {
    if (!validSharedMemoryKey(key) || expectedSize == 0) {
        setError(error, QStringLiteral("The shared raster descriptor is invalid."));
        return {};
    }
    auto memory = std::make_shared<QSharedMemory>(key);
    if (!memory->attach(QSharedMemory::ReadOnly)) {
        setError(error, QStringLiteral("Could not attach shared raster storage: %1")
                            .arg(memory->errorString()));
        return {};
    }
    if (static_cast<std::uint64_t>(memory->size()) != expectedSize || !memory->data()) {
        setError(error, QStringLiteral("The shared raster size is unexpected."));
        return {};
    }
    const auto* address = static_cast<const std::byte*>(memory->constData());
    const auto opened = snow::image::RasterBufferStore::open(
        std::span<const std::byte>(address, static_cast<std::size_t>(memory->size())), {},
        expectedNonce, expectedSize);
    if (!opened) {
        setError(error, statusText(opened.error()));
        return {};
    }
    return fromBuffer(key, std::move(opened).value(), std::move(memory), error);
}

std::shared_ptr<MappedRasterPackage>
MappedRasterPackage::adoptStore(const QString& path,
                                std::shared_ptr<snow::image::RasterStore> store, QString* error,
                                std::shared_ptr<const void> cleanupOwner,
                                std::optional<snow::image::AlphaContent> verifiedAlphaContent) {
    if (!store || !store->complete()) {
        setError(error, QStringLiteral("The shared raster store is incomplete."));
        return {};
    }
    return fromStore(path, std::move(store), error, std::move(cleanupOwner), verifiedAlphaContent);
}

bool MappedRasterPackage::mapAndValidate(const QString& path, QString* error,
                                         std::shared_ptr<const void> cleanupOwner) {
    snow::image::Result<std::shared_ptr<snow::image::RasterStore>> opened =
        snow::image::RasterStore::open(nativePath(path));
    if (!opened) {
        setError(error, statusText(opened.error()));
        return false;
    }
    std::shared_ptr<MappedRasterPackage> loaded =
        fromStore(path, std::move(opened).value(), error, std::move(cleanupOwner));
    if (!loaded)
        return false;
    cleanupOwner_ = std::move(loaded->cleanupOwner_);
    path_ = std::move(loaded->path_);
    store_ = std::move(loaded->store_);
    bufferStore_ = std::move(loaded->bufferStore_);
    mappedBytes_ = loaded->mappedBytes_;
    document_ = std::move(loaded->document_);
    sharedMemoryKey_ = std::move(loaded->sharedMemoryKey_);
    sharedMemoryNonce_ = loaded->sharedMemoryNonce_;
    verifiedAlphaContent_ = loaded->verifiedAlphaContent_;
    return true;
}

std::shared_ptr<MappedRasterPackage>
MappedRasterPackage::fromStore(const QString& path, std::shared_ptr<snow::image::RasterStore> store,
                               QString* error, std::shared_ptr<const void> cleanupOwner,
                               std::optional<snow::image::AlphaContent> verifiedAlphaContent) {
    if (!store) {
        setError(error, QStringLiteral("The raster package has no backing store."));
        return {};
    }
    if (!compatibleVerifiedAlpha(store->descriptor(), verifiedAlphaContent)) {
        setError(error,
                 QStringLiteral("The raster package alpha metadata contradicts its descriptor."));
        return {};
    }
    if (verifiedAlphaContent && store->analysis().alpha_content &&
        *verifiedAlphaContent != *store->analysis().alpha_content) {
        setError(error, QStringLiteral(
                            "The raster package alpha metadata contradicts its stored analysis."));
        return {};
    }
    const bool supportsDocument = supportsCompatibilityDocument(store->descriptor());
    snow::image::Result<snow::image::Document> document = mappedDocument(*store);
    if (!document && supportsDocument) {
        setError(error, statusText(document.error()));
        return {};
    }
    auto package = std::shared_ptr<MappedRasterPackage>(new MappedRasterPackage());
    package->cleanupOwner_ = std::move(cleanupOwner);
    package->path_ = path;
    package->mappedBytes_ = store->file_bytes();
    package->store_ = std::move(store);
    if (document)
        package->document_ = std::move(document).value();
    package->verifiedAlphaContent_ =
        verifiedAlphaContent ? verifiedAlphaContent : package->store_->analysis().alpha_content;
    return package;
}

std::shared_ptr<MappedRasterPackage>
MappedRasterPackage::fromBuffer(const QString& key,
                                std::shared_ptr<snow::image::RasterBufferStore> store,
                                std::shared_ptr<QSharedMemory> sharedMemory, QString* error,
                                std::optional<snow::image::AlphaContent> verifiedAlphaContent) {
    if (!store || !store->complete() || !sharedMemory) {
        setError(error, QStringLiteral("The shared raster store is incomplete."));
        return {};
    }
    if (verifiedAlphaContent && store->analysis().alpha_content &&
        *verifiedAlphaContent != *store->analysis().alpha_content) {
        setError(error, QStringLiteral(
                            "The shared raster alpha metadata contradicts its stored analysis."));
        return {};
    }
    const std::uint64_t sharedMemoryBytes = static_cast<std::uint64_t>(sharedMemory->size());
    snow::image::Result<snow::image::Document> document = mappedDocument(*store);
    if (!document && supportsCompatibilityDocument(store->descriptor())) {
        setError(error, statusText(document.error()));
        return {};
    }
    auto package = std::shared_ptr<MappedRasterPackage>(new MappedRasterPackage());
    package->cleanupOwner_ = sharedMemory;
    package->sharedMemory_ = std::move(sharedMemory);
    package->sharedMemoryKey_ = key;
    package->sharedMemoryNonce_ = store->session_nonce();
    package->mappedBytes_ = sharedMemoryBytes;
    package->bufferStore_ = std::move(store);
    if (document)
        package->document_ = std::move(document).value();
    package->verifiedAlphaContent_ = verifiedAlphaContent
                                         ? verifiedAlphaContent
                                         : package->bufferStore_->analysis().alpha_content;
    return package;
}

MappedRasterSink::MappedRasterSink(QString path,
                                   std::optional<snow::image::AlphaContent> alphaContent)
    : impl_(std::make_unique<Impl>(std::move(path), alphaContent)) {}

MappedRasterSink::~MappedRasterSink() {
    if (impl_ && !impl_->packageTaken && impl_->store && !impl_->store->complete()) {
        discard();
    }
}

snow::image::Result<void> MappedRasterSink::begin(const snow::image::DocumentInfo& document) {
    if (impl_->store || impl_->complete || document.frames.empty()) {
        return invalidSink("The mapped raster sink has already begun or is empty.");
    }
    snow::image::Result<snow::image::DocumentDescriptor> descriptor =
        snow::image::describe_document(document);
    if (!descriptor)
        return descriptor.error();
    snow::image::Result<std::shared_ptr<snow::image::RasterStore>> created = [&]() {
        snow::image::RasterStoreOptions options;
        options.row_alignment = 1;
        options.analysis.alpha_content = impl_->alphaContent;
        return snow::image::RasterStore::create(nativePath(impl_->path),
                                                std::move(descriptor).value(), options);
    }();
    if (!created)
        return created.error();
    impl_->info = document;
    impl_->store = std::move(created).value();
    return {};
}

snow::image::Result<void> MappedRasterSink::begin_frame(std::uint32_t frameIndex,
                                                        const snow::image::FrameInfo& frame) {
    if (!impl_->store || impl_->activeFrame != kNoFrame || frameIndex != impl_->completedFrames ||
        frameIndex >= impl_->info.frames.size()) {
        return invalidSink("The mapped raster frame sequence is invalid.");
    }
    const snow::image::FrameInfo& expected = impl_->info.frames[frameIndex];
    if (frame.width != expected.width || frame.height != expected.height ||
        frame.native_format != expected.native_format) {
        return invalidSink("The mapped raster frame descriptor changed during decoding.");
    }
    impl_->activeFrame = frameIndex;
    return {};
}

std::span<std::byte> MappedRasterSink::frame_storage(std::uint32_t frameIndex,
                                                     std::size_t rowStride, std::size_t byteSize) {
    if (!impl_->store || impl_->activeMapping || frameIndex != impl_->activeFrame ||
        frameIndex >= impl_->info.frames.size()) {
        return {};
    }
    const snow::image::FrameInfo& frame = impl_->info.frames[frameIndex];
    const auto bytesPerPixel = frame.native_format.bytes_per_pixel();
    if (!bytesPerPixel ||
        frame.width > std::numeric_limits<std::size_t>::max() / bytesPerPixel.value()) {
        return {};
    }
    const std::size_t packedStride = static_cast<std::size_t>(frame.width) * bytesPerPixel.value();
    if (rowStride != packedStride ||
        frame.height > std::numeric_limits<std::size_t>::max() / packedStride ||
        byteSize != packedStride * frame.height) {
        return {};
    }
    auto mapped = impl_->store->map_plane_for_write(frameIndex, 0);
    if (!mapped || mapped.value().row_stride != rowStride ||
        mapped.value().pixels.size() != byteSize) {
        return {};
    }
    impl_->activeMapping = std::move(mapped).value();
    return impl_->activeMapping->pixels;
}

snow::image::Result<void> MappedRasterSink::write_rows(std::uint32_t firstRow,
                                                       std::uint32_t rowCount,
                                                       std::size_t rowStride,
                                                       std::span<const std::byte> pixels) {
    if (!impl_->store || impl_->activeFrame == kNoFrame || impl_->activeMapping) {
        return invalidSink("The mapped raster row write is outside an active frame.");
    }
    return impl_->store->write_rows(impl_->activeFrame, 0, firstRow, rowCount, rowStride, pixels);
}

snow::image::Result<void> MappedRasterSink::end_frame(std::uint32_t frameIndex) {
    if (!impl_->store || frameIndex != impl_->activeFrame) {
        return invalidSink("The mapped raster frame could not be finalized.");
    }
    if (impl_->activeMapping) {
        snow::image::Result<void> finished =
            impl_->store->finish_mapped_plane(frameIndex, 0, *impl_->activeMapping);
        if (!finished)
            return finished.error();
        impl_->activeMapping.reset();
    }
    impl_->activeFrame = kNoFrame;
    ++impl_->completedFrames;
    return {};
}

snow::image::Result<void> MappedRasterSink::end() {
    if (!impl_->store || impl_->activeFrame != kNoFrame || impl_->complete ||
        impl_->completedFrames != impl_->info.frames.size()) {
        return invalidSink("The mapped raster package is incomplete.");
    }
    snow::image::Result<void> committed = impl_->store->commit();
    if (!committed)
        return committed.error();
    impl_->fileBytes = impl_->store->file_bytes();
    QFile::setPermissions(impl_->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    impl_->complete = true;
    return {};
}

std::shared_ptr<MappedRasterPackage>
MappedRasterSink::takePackage(QString* error, std::shared_ptr<const void> cleanupOwner) {
    if (!impl_->complete || impl_->packageTaken) {
        setError(error, QStringLiteral("The mapped raster package is not complete."));
        return {};
    }
    auto package =
        MappedRasterPackage::fromStore(impl_->path, impl_->store, error, std::move(cleanupOwner));
    if (!package)
        return {};
    if (package->mappedBytes() != impl_->fileBytes) {
        setError(error, QStringLiteral("The reopened raster package size changed."));
        return {};
    }
    impl_->store.reset();
    impl_->packageTaken = true;
    return package;
}

void MappedRasterSink::discard() noexcept {
    if (!impl_ || impl_->packageTaken)
        return;
    impl_->activeMapping.reset();
    if (impl_->store) {
        if (!impl_->store->complete())
            impl_->store->abort();
        impl_->store.reset();
    }
    QFile::remove(impl_->path);
    impl_->activeFrame = kNoFrame;
    impl_->complete = false;
}

struct SharedRasterSink::Impl final {
    explicit Impl(QString sharedKey, std::optional<snow::image::AlphaContent> content)
        : key(std::move(sharedKey)), alphaContent(content) {}

    QString key;
    std::optional<snow::image::AlphaContent> alphaContent;
    snow::image::DocumentInfo info;
    std::shared_ptr<QSharedMemory> memory;
    std::shared_ptr<snow::image::RasterBufferStore> store;
    std::optional<snow::image::MutableMappedPlane> activeMapping;
    std::uint32_t activeFrame = kNoFrame;
    std::uint32_t completedFrames = 0;
    bool locked = false;
    bool complete = false;
    bool packageTaken = false;
};

SharedRasterSink::SharedRasterSink(QString key,
                                   std::optional<snow::image::AlphaContent> alphaContent)
    : impl_(std::make_unique<Impl>(std::move(key), alphaContent)) {}

SharedRasterSink::~SharedRasterSink() {
    if (impl_ && !impl_->packageTaken)
        discard();
}

snow::image::Result<void> SharedRasterSink::begin(const snow::image::DocumentInfo& document) {
    if (impl_->store || impl_->complete || document.frames.empty() ||
        !validSharedMemoryKey(impl_->key))
        return invalidSink("The shared raster sink has an invalid key or state.");
    snow::image::Result<snow::image::DocumentDescriptor> descriptor =
        snow::image::describe_document(document);
    if (!descriptor)
        return descriptor.error();
    snow::image::RasterBufferStoreOptions options;
    options.row_alignment = 1;
    options.analysis.alpha_content = impl_->alphaContent;
    const auto required =
        snow::image::RasterBufferStore::required_bytes(descriptor.value(), options);
    if (!required ||
        required.value() > static_cast<std::uint64_t>(std::numeric_limits<qsizetype>::max()))
        return required
                   ? snow::image::Status::error(snow::image::ErrorCode::limit_exceeded,
                                                "The shared raster is too large.", "raster package")
                   : required.error();
    impl_->memory = std::make_shared<QSharedMemory>(impl_->key);
    if (!impl_->memory->create(static_cast<qsizetype>(required.value())))
        return snow::image::Status::error(
            snow::image::ErrorCode::io_error,
            QStringLiteral("Could not create shared raster storage: %1")
                .arg(impl_->memory->errorString())
                .toStdString(),
            "raster package");
    if (static_cast<std::uint64_t>(impl_->memory->size()) < required.value())
        return snow::image::Status::error(snow::image::ErrorCode::io_error,
                                          "The shared raster storage is smaller than requested.",
                                          "raster package");
    if (!impl_->memory->lock())
        return snow::image::Status::error(snow::image::ErrorCode::io_error,
                                          "Could not lock shared raster storage.",
                                          "raster package");
    impl_->locked = true;
    auto created = snow::image::RasterBufferStore::create(
        std::span<std::byte>(static_cast<std::byte*>(impl_->memory->data()),
                             static_cast<std::size_t>(required.value())),
        std::move(descriptor).value(), options);
    if (!created)
        return created.error();
    impl_->info = document;
    impl_->store = std::move(created).value();
    return {};
}

snow::image::Result<void> SharedRasterSink::begin_frame(std::uint32_t frameIndex,
                                                        const snow::image::FrameInfo& frame) {
    if (!impl_->store || impl_->activeFrame != kNoFrame || frameIndex != impl_->completedFrames ||
        frameIndex >= impl_->info.frames.size())
        return invalidSink("The shared raster frame sequence is invalid.");
    const auto& expected = impl_->info.frames[frameIndex];
    if (frame.width != expected.width || frame.height != expected.height ||
        frame.native_format != expected.native_format)
        return invalidSink("The shared raster frame descriptor changed during decoding.");
    impl_->activeFrame = frameIndex;
    return {};
}

std::span<std::byte> SharedRasterSink::frame_storage(std::uint32_t frameIndex,
                                                     std::size_t rowStride, std::size_t byteSize) {
    if (!impl_->store || impl_->activeMapping || frameIndex != impl_->activeFrame ||
        frameIndex >= impl_->info.frames.size())
        return {};
    const auto& frame = impl_->info.frames[frameIndex];
    const auto bytesPerPixel = frame.native_format.bytes_per_pixel();
    if (!bytesPerPixel ||
        frame.width > std::numeric_limits<std::size_t>::max() / bytesPerPixel.value())
        return {};
    const std::size_t packedStride = static_cast<std::size_t>(frame.width) * bytesPerPixel.value();
    if (rowStride != packedStride ||
        frame.height > std::numeric_limits<std::size_t>::max() / packedStride ||
        byteSize != packedStride * frame.height)
        return {};
    auto mapped = impl_->store->map_plane_for_write(frameIndex, 0);
    if (!mapped || mapped.value().row_stride != rowStride ||
        mapped.value().pixels.size() != byteSize)
        return {};
    impl_->activeMapping = std::move(mapped).value();
    return impl_->activeMapping->pixels;
}

snow::image::Result<void> SharedRasterSink::write_rows(std::uint32_t firstRow,
                                                       std::uint32_t rowCount,
                                                       std::size_t rowStride,
                                                       std::span<const std::byte> pixels) {
    if (!impl_->store || impl_->activeFrame == kNoFrame || impl_->activeMapping)
        return invalidSink("The shared raster row write is outside an active frame.");
    return impl_->store->write_rows(impl_->activeFrame, 0, firstRow, rowCount, rowStride, pixels);
}

snow::image::Result<void> SharedRasterSink::end_frame(std::uint32_t frameIndex) {
    if (!impl_->store || frameIndex != impl_->activeFrame)
        return invalidSink("The shared raster frame could not be finalized.");
    if (impl_->activeMapping) {
        const auto finished =
            impl_->store->finish_mapped_plane(frameIndex, 0, *impl_->activeMapping);
        if (!finished)
            return finished.error();
        impl_->activeMapping.reset();
    }
    impl_->activeFrame = kNoFrame;
    ++impl_->completedFrames;
    return {};
}

snow::image::Result<void> SharedRasterSink::end() {
    if (!impl_->store || impl_->activeFrame != kNoFrame || impl_->complete ||
        impl_->completedFrames != impl_->info.frames.size())
        return invalidSink("The shared raster package is incomplete.");
    const auto committed = impl_->store->commit();
    if (impl_->locked) {
        impl_->memory->unlock();
        impl_->locked = false;
    }
    if (!committed)
        return committed.error();
    impl_->complete = true;
    return {};
}

std::shared_ptr<MappedRasterPackage> SharedRasterSink::takePackage(QString* error) {
    if (!impl_->complete || impl_->packageTaken || !impl_->store || !impl_->memory) {
        setError(error, QStringLiteral("The shared raster package is not complete."));
        return {};
    }
    auto package = MappedRasterPackage::fromBuffer(impl_->key, impl_->store, impl_->memory, error,
                                                   impl_->store->analysis().alpha_content);
    if (!package)
        return {};
    impl_->store.reset();
    impl_->memory.reset();
    impl_->packageTaken = true;
    return package;
}

void SharedRasterSink::discard() noexcept {
    if (!impl_ || impl_->packageTaken)
        return;
    impl_->activeMapping.reset();
    if (impl_->store) {
        if (!impl_->store->complete())
            impl_->store->abort();
        impl_->store.reset();
    }
    if (impl_->memory) {
        if (impl_->locked) {
            impl_->memory->unlock();
            impl_->locked = false;
        }
        if (impl_->memory->isAttached())
            impl_->memory->detach();
        impl_->memory.reset();
    }
    impl_->activeFrame = kNoFrame;
    impl_->complete = false;
}

} // namespace snow::image_viewer
