#pragma once

#include "editing/raster_package.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace snow::image_viewer {

// Owns one verified raster package and presents the same lifetime and worker
// transport contract for file-backed and shared-memory assets.
class RasterAsset final {
  public:
    [[nodiscard]] static std::shared_ptr<RasterAsset>
    fromPackage(std::shared_ptr<MappedRasterPackage> package) {
        if (!package)
            return {};
        return std::shared_ptr<RasterAsset>(new RasterAsset(std::move(package)));
    }

    [[nodiscard]] static std::shared_ptr<RasterAsset>
    fileBacked(std::shared_ptr<MappedRasterPackage> package) {
        if (!package || package->isSharedMemory())
            return {};
        return fromPackage(std::move(package));
    }

    [[nodiscard]] static std::shared_ptr<RasterAsset>
    sharedMemory(std::shared_ptr<MappedRasterPackage> package) {
        if (!package || !package->isSharedMemory())
            return {};
        return fromPackage(std::move(package));
    }

    [[nodiscard]] const MappedRasterPackage& package() const noexcept {
        return *package_;
    }
    [[nodiscard]] std::shared_ptr<MappedRasterPackage> packageShared() const noexcept {
        return package_;
    }
    [[nodiscard]] const snow::image::RasterSource& source() const noexcept {
        return package_->source();
    }
    [[nodiscard]] const snow::image::Document* document() const noexcept {
        return package_->document();
    }
    [[nodiscard]] std::uint64_t byteSize() const noexcept {
        return package_->mappedBytes();
    }
    [[nodiscard]] snow::image::RasterAnalysis analysis() const noexcept {
        return package_->analysis();
    }
    [[nodiscard]] std::optional<snow::image::AlphaContent> verifiedAlphaContent() const noexcept {
        return package_->verifiedAlphaContent();
    }
    [[nodiscard]] bool isSharedMemory() const noexcept {
        return package_->isSharedMemory();
    }
    [[nodiscard]] const QString& path() const noexcept {
        return package_->path();
    }
    [[nodiscard]] const void* identity() const noexcept {
        return package_->identity();
    }

    // The descriptor contains only validated package identity and attachment
    // data.  Pixel bytes never cross the process boundary through JSON.
    [[nodiscard]] QJsonObject workerTransport() const {
        if (isSharedMemory()) {
            QJsonArray nonce;
            for (const std::byte value : package_->sharedMemoryNonce()) {
                nonce.append(static_cast<int>(std::to_integer<std::uint8_t>(value)));
            }
            return {{QStringLiteral("kind"), QStringLiteral("shared_memory")},
                    {QStringLiteral("key"), package_->sharedMemoryKey()},
                    {QStringLiteral("size"), QString::number(package_->sharedMemorySize())},
                    {QStringLiteral("nonce"), nonce}};
        }
        return {{QStringLiteral("kind"), QStringLiteral("verified_file")},
                {QStringLiteral("path"), package_->path()}};
    }

  private:
    explicit RasterAsset(std::shared_ptr<MappedRasterPackage> package)
        : package_(std::move(package)) {}

    std::shared_ptr<MappedRasterPackage> package_;
};

} // namespace snow::image_viewer
