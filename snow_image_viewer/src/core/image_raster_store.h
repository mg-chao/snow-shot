#pragma once

#include <snow/image/raster.h>

#include <QString>

#include <memory>
#include <optional>
#include <utility>

namespace snow::image_viewer {

// Owns a committed raster store and the temporary file backing it. Sharing
// this object keeps the decoded source stable across render and edit users.
class ImageRasterStore final {
  public:
    ~ImageRasterStore();

    ImageRasterStore(const ImageRasterStore&) = delete;
    ImageRasterStore& operator=(const ImageRasterStore&) = delete;

    [[nodiscard]] static std::shared_ptr<ImageRasterStore>
    create(snow::image::DocumentDescriptor descriptor,
           const snow::image::RasterStoreOptions& options = {}, QString* error = nullptr);
    [[nodiscard]] static std::shared_ptr<ImageRasterStore>
    adopt(QString filePath, std::shared_ptr<snow::image::RasterStore> store);
    [[nodiscard]] static std::shared_ptr<ImageRasterStore>
    retain(QString filePath, std::shared_ptr<snow::image::RasterStore> store,
           std::shared_ptr<const void> cleanupOwner);

    [[nodiscard]] const QString& filePath() const noexcept {
        return filePath_;
    }
    [[nodiscard]] const std::shared_ptr<snow::image::RasterStore>& store() const noexcept {
        return store_;
    }
    [[nodiscard]] std::optional<snow::image::AlphaContent> verifiedAlphaContent() const noexcept {
        return analysis_.alpha_content;
    }
    void setVerifiedAlphaContent(snow::image::AlphaContent content) {
        analysis_.alpha_content = content;
        if (store_)
            (void)store_->set_analysis(analysis_);
    }
    [[nodiscard]] const snow::image::RasterAnalysis& analysis() const noexcept {
        return analysis_;
    }
    void setAnalysis(snow::image::RasterAnalysis analysis) {
        analysis_ = std::move(analysis);
        if (store_)
            (void)store_->set_analysis(analysis_);
    }
    [[nodiscard]] bool isValid() const noexcept {
        return !filePath_.isEmpty() && store_ != nullptr;
    }

  private:
    ImageRasterStore(QString filePath, std::shared_ptr<snow::image::RasterStore> store,
                     std::shared_ptr<const void> cleanupOwner = {});

    std::shared_ptr<const void> cleanupOwner_;
    QString filePath_;
    std::shared_ptr<snow::image::RasterStore> store_;
    snow::image::RasterAnalysis analysis_;
};

} // namespace snow::image_viewer
