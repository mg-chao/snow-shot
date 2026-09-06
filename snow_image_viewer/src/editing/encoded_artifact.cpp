#include "editing/encoded_artifact.h"

#include <QIODevice>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <vector>

namespace snow::image_viewer {
namespace {

std::filesystem::path nativePath(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
}

void setError(QString* error, const QString& message) {
    if (error)
        *error = message;
}

} // namespace

EncodedArtifact::EncodedArtifact(snow::image::Format format, std::uint64_t byteSize, QString path,
                                 snow::image::Input input,
                                 snow::image::EncodedArtifactReceipt receipt,
                                 std::shared_ptr<const void> cleanupOwner)
    : cleanupOwner_(std::move(cleanupOwner)), format_(format), byteSize_(byteSize),
      path_(std::move(path)), input_(std::move(input)), receipt_(std::move(receipt)) {}

std::shared_ptr<const EncodedArtifact>
EncodedArtifact::adopt(snow::image::Format format, const QString& path,
                       snow::image::EncodedArtifactReceipt receipt,
                       std::shared_ptr<const void> cleanupOwner, QString* error) {
    if (format == snow::image::Format::unknown || !cleanupOwner || receipt.format != format ||
        !receipt.encoder_finalized_and_sink_flushed) {
        setError(error, QStringLiteral("The encoded artifact receipt is invalid."));
        return {};
    }
    auto input = snow::image::file_input(nativePath(path));
    if (!input) {
        setError(error, QString::fromStdString(input.error().message));
        return {};
    }
    auto size = input.value().source->size();
    if (!size || size.value() == 0) {
        setError(error, size ? QStringLiteral("The encoded artifact is empty.")
                             : QString::fromStdString(size.error().message));
        return {};
    }
    return std::shared_ptr<const EncodedArtifact>(
        new EncodedArtifact(format, size.value(), path, std::move(input).value(),
                            std::move(receipt), std::move(cleanupOwner)));
}

std::shared_ptr<const EncodedArtifact>
EncodedArtifact::adopt(snow::image::Format format, const QString& path,
                       std::shared_ptr<const void> cleanupOwner, QString* error) {
    snow::image::EncodedArtifactReceipt receipt;
    receipt.format = format;
    receipt.encoder_finalized_and_sink_flushed = true;
    return adopt(format, path, std::move(receipt), std::move(cleanupOwner), error);
}

bool EncodedArtifact::copyTo(QIODevice& destination, QString* error) const {
    if (!destination.isWritable()) {
        setError(error, QStringLiteral("The artifact destination is not writable."));
        return false;
    }
    constexpr std::size_t kChunkBytes = 1024U * 1024U;
    std::vector<std::byte> buffer;
    try {
        buffer.resize(kChunkBytes);
    } catch (const std::bad_alloc&) {
        setError(error, QStringLiteral("The artifact transfer buffer could not be allocated."));
        return false;
    }
    std::uint64_t offset = 0;
    while (offset < byteSize_) {
        const std::size_t requested =
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), byteSize_ - offset));
        auto read = input_.source->read_at(offset, std::span<std::byte>(buffer).first(requested));
        if (!read || read.value() == 0) {
            setError(error, read ? QStringLiteral("The encoded artifact ended unexpectedly.")
                                 : QString::fromStdString(read.error().message));
            return false;
        }
        std::size_t consumed = 0;
        while (consumed < read.value()) {
            const qint64 written =
                destination.write(reinterpret_cast<const char*>(buffer.data() + consumed),
                                  static_cast<qint64>(read.value() - consumed));
            if (written <= 0) {
                setError(error, destination.errorString().isEmpty()
                                    ? QStringLiteral("The encoded artifact could not be written.")
                                    : destination.errorString());
                return false;
            }
            consumed += static_cast<std::size_t>(written);
        }
        offset += read.value();
    }
    return true;
}

} // namespace snow::image_viewer
