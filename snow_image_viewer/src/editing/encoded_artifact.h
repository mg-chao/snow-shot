#pragma once

#include <snow/image/codec.h>
#include <snow/image/io.h>

#include <QString>

#include <cstdint>
#include <memory>

class QIODevice;

namespace snow::image_viewer {

class EncodedArtifact final {
  public:
    [[nodiscard]] snow::image::Format format() const noexcept {
        return format_;
    }
    [[nodiscard]] std::uint64_t byteSize() const noexcept {
        return byteSize_;
    }
    [[nodiscard]] const QString& path() const noexcept {
        return path_;
    }
    [[nodiscard]] const snow::image::Input& input() const noexcept {
        return input_;
    }
    [[nodiscard]] const snow::image::EncodedArtifactReceipt& receipt() const noexcept {
        return receipt_;
    }

    [[nodiscard]] bool copyTo(QIODevice& destination, QString* error = nullptr) const;

    static std::shared_ptr<const EncodedArtifact> adopt(snow::image::Format format,
                                                        const QString& path,
                                                        snow::image::EncodedArtifactReceipt receipt,
                                                        std::shared_ptr<const void> cleanupOwner,
                                                        QString* error = nullptr);
    static std::shared_ptr<const EncodedArtifact> adopt(snow::image::Format format,
                                                        const QString& path,
                                                        std::shared_ptr<const void> cleanupOwner,
                                                        QString* error = nullptr);

  private:
    EncodedArtifact(snow::image::Format format, std::uint64_t byteSize, QString path,
                    snow::image::Input input, snow::image::EncodedArtifactReceipt receipt,
                    std::shared_ptr<const void> cleanupOwner);

    // Declared first so it is destroyed last, after the file-backed input closes.
    std::shared_ptr<const void> cleanupOwner_;
    snow::image::Format format_ = snow::image::Format::unknown;
    std::uint64_t byteSize_ = 0;
    QString path_;
    snow::image::Input input_;
    snow::image::EncodedArtifactReceipt receipt_;
};

} // namespace snow::image_viewer
