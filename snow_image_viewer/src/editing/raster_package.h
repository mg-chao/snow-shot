#pragma once

#include <snow/image/codec.h>
#include <snow/image/raster.h>

#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

class QSharedMemory;

namespace snow::image_viewer {

class MappedRasterPackage final {
  public:
    ~MappedRasterPackage();

    MappedRasterPackage(const MappedRasterPackage&) = delete;
    MappedRasterPackage& operator=(const MappedRasterPackage&) = delete;

    [[nodiscard]] static std::shared_ptr<MappedRasterPackage>
    create(const QString& path, const snow::image::Document& document, QString* error = nullptr,
           std::shared_ptr<const void> cleanupOwner = {},
           std::optional<snow::image::AlphaContent> alphaContent = {});
    [[nodiscard]] static std::shared_ptr<MappedRasterPackage>
    open(const QString& path, QString* error = nullptr,
         std::shared_ptr<const void> cleanupOwner = {});
    [[nodiscard]] static std::shared_ptr<MappedRasterPackage>
    createShared(const QString& key, const snow::image::Document& document,
                 std::optional<snow::image::AlphaContent> alphaContent = {},
                 QString* error = nullptr);
    [[nodiscard]] static std::shared_ptr<MappedRasterPackage>
    openShared(const QString& key, std::uint64_t expectedSize,
               std::array<std::byte, 16> expectedNonce, QString* error = nullptr);
    [[nodiscard]] static std::shared_ptr<MappedRasterPackage>
    adoptStore(const QString& path, std::shared_ptr<snow::image::RasterStore> store,
               QString* error = nullptr, std::shared_ptr<const void> cleanupOwner = {},
               std::optional<snow::image::AlphaContent> verifiedAlphaContent = {});

    [[nodiscard]] const snow::image::Document* document() const noexcept {
        return document_ ? &*document_ : nullptr;
    }
    [[nodiscard]] const snow::image::RasterSource& source() const noexcept {
        return bufferStore_ ? static_cast<const snow::image::RasterSource&>(*bufferStore_)
                            : static_cast<const snow::image::RasterSource&>(*store_);
    }
    [[nodiscard]] const std::shared_ptr<snow::image::RasterStore>& store() const noexcept {
        return store_;
    }
    [[nodiscard]] const std::shared_ptr<snow::image::RasterBufferStore>&
    bufferStore() const noexcept {
        return bufferStore_;
    }
    [[nodiscard]] const QString& path() const noexcept {
        return path_;
    }
    [[nodiscard]] std::uint64_t mappedBytes() const noexcept {
        return mappedBytes_;
    }
    [[nodiscard]] bool isSharedMemory() const noexcept {
        return bufferStore_ != nullptr;
    }
    [[nodiscard]] const QString& sharedMemoryKey() const noexcept {
        return sharedMemoryKey_;
    }
    [[nodiscard]] std::uint64_t sharedMemorySize() const noexcept {
        return mappedBytes_;
    }
    [[nodiscard]] std::array<std::byte, 16> sharedMemoryNonce() const noexcept {
        return sharedMemoryNonce_;
    }
    [[nodiscard]] const void* identity() const noexcept {
        return bufferStore_ ? static_cast<const void*>(bufferStore_.get())
                            : static_cast<const void*>(store_.get());
    }
    [[nodiscard]] std::optional<snow::image::AlphaContent> verifiedAlphaContent() const noexcept {
        return verifiedAlphaContent_;
    }
    [[nodiscard]] snow::image::RasterAnalysis analysis() const noexcept {
        return snow::image::RasterAnalysis{verifiedAlphaContent_};
    }
    void retainCleanupOwner(std::shared_ptr<const void> owner);

  private:
    MappedRasterPackage() = default;
    [[nodiscard]] static std::shared_ptr<MappedRasterPackage>
    fromStore(const QString& path, std::shared_ptr<snow::image::RasterStore> store, QString* error,
              std::shared_ptr<const void> cleanupOwner,
              std::optional<snow::image::AlphaContent> verifiedAlphaContent = {});
    [[nodiscard]] static std::shared_ptr<MappedRasterPackage>
    fromBuffer(const QString& key, std::shared_ptr<snow::image::RasterBufferStore> store,
               std::shared_ptr<QSharedMemory> sharedMemory, QString* error,
               std::optional<snow::image::AlphaContent> verifiedAlphaContent = {});
    bool mapAndValidate(const QString& path, QString* error,
                        std::shared_ptr<const void> cleanupOwner);

    // Declared first so temporary-directory cleanup runs after the store and all
    // mapped image owners have released their operating-system handles.
    std::shared_ptr<const void> cleanupOwner_;
    std::shared_ptr<QSharedMemory> sharedMemory_;
    QString path_;
    std::shared_ptr<snow::image::RasterStore> store_;
    std::shared_ptr<snow::image::RasterBufferStore> bufferStore_;
    std::uint64_t mappedBytes_ = 0;
    std::optional<snow::image::Document> document_;
    std::optional<snow::image::AlphaContent> verifiedAlphaContent_;
    QString sharedMemoryKey_;
    std::array<std::byte, 16> sharedMemoryNonce_{};

    friend class MappedRasterSink;
    friend class SharedRasterSink;
};

class MappedRasterSink final : public snow::image::PixelSink {
  public:
    explicit MappedRasterSink(QString path,
                              std::optional<snow::image::AlphaContent> alphaContent = {});
    ~MappedRasterSink() override;

    MappedRasterSink(const MappedRasterSink&) = delete;
    MappedRasterSink& operator=(const MappedRasterSink&) = delete;

    [[nodiscard]] snow::image::Result<void>
    begin(const snow::image::DocumentInfo& document) override;
    [[nodiscard]] snow::image::Result<void>
    begin_frame(std::uint32_t frameIndex, const snow::image::FrameInfo& frame) override;
    [[nodiscard]] std::span<std::byte>
    frame_storage(std::uint32_t frameIndex, std::size_t rowStride, std::size_t byteSize) override;
    [[nodiscard]] snow::image::Result<void> write_rows(std::uint32_t firstRow,
                                                       std::uint32_t rowCount,
                                                       std::size_t rowStride,
                                                       std::span<const std::byte> pixels) override;
    [[nodiscard]] snow::image::Result<void> end_frame(std::uint32_t frameIndex) override;
    [[nodiscard]] snow::image::Result<void> end() override;

    [[nodiscard]] std::shared_ptr<MappedRasterPackage>
    takePackage(QString* error = nullptr, std::shared_ptr<const void> cleanupOwner = {});
    void discard() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// PixelSink implementation used by generated CPU/GPU rasters.  It seals a
// QSharedMemory-backed RasterBufferStore before handing the package to the
// controller; the worker can then attach read-only using the package metadata.
class SharedRasterSink final : public snow::image::PixelSink {
  public:
    explicit SharedRasterSink(QString key,
                              std::optional<snow::image::AlphaContent> alphaContent = {});
    ~SharedRasterSink() override;

    SharedRasterSink(const SharedRasterSink&) = delete;
    SharedRasterSink& operator=(const SharedRasterSink&) = delete;

    [[nodiscard]] snow::image::Result<void>
    begin(const snow::image::DocumentInfo& document) override;
    [[nodiscard]] snow::image::Result<void>
    begin_frame(std::uint32_t frameIndex, const snow::image::FrameInfo& frame) override;
    [[nodiscard]] std::span<std::byte>
    frame_storage(std::uint32_t frameIndex, std::size_t rowStride, std::size_t byteSize) override;
    [[nodiscard]] snow::image::Result<void> write_rows(std::uint32_t firstRow,
                                                       std::uint32_t rowCount,
                                                       std::size_t rowStride,
                                                       std::span<const std::byte> pixels) override;
    [[nodiscard]] snow::image::Result<void> end_frame(std::uint32_t frameIndex) override;
    [[nodiscard]] snow::image::Result<void> end() override;

    [[nodiscard]] std::shared_ptr<MappedRasterPackage> takePackage(QString* error = nullptr);
    void discard() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace snow::image_viewer
