#include "core/image_raster_store.h"

#include <QFile>
#include <QTemporaryFile>

#include <filesystem>
#include <utility>

namespace snow::image_viewer {
namespace {

std::filesystem::path nativePath(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
}

void setError(QString* error, const snow::image::Status& status) {
    if (error)
        *error = QString::fromStdString(status.message);
}

} // namespace

ImageRasterStore::ImageRasterStore(QString filePath,
                                   std::shared_ptr<snow::image::RasterStore> store,
                                   std::shared_ptr<const void> cleanupOwner)
    : cleanupOwner_(std::move(cleanupOwner)), filePath_(std::move(filePath)),
      store_(std::move(store)) {
    if (store_)
        analysis_ = store_->analysis();
}

ImageRasterStore::~ImageRasterStore() {
    if (store_ && !store_->complete())
        store_->abort();
    store_.reset();
    if (!cleanupOwner_ && !filePath_.isEmpty())
        QFile::remove(filePath_);
}

std::shared_ptr<ImageRasterStore>
ImageRasterStore::create(snow::image::DocumentDescriptor descriptor,
                         const snow::image::RasterStoreOptions& options, QString* error) {
    QTemporaryFile temporary;
    if (!temporary.open()) {
        if (error)
            *error = QStringLiteral("Could not create the temporary raster store.");
        return {};
    }
    const QString path = temporary.fileName();
    temporary.setAutoRemove(false);
    if (!temporary.remove()) {
        if (error)
            *error = QStringLiteral("Could not reserve the raster-store path.");
        return {};
    }

    auto created =
        snow::image::RasterStore::create(nativePath(path), std::move(descriptor), options);
    if (!created) {
        setError(error, created.error());
        QFile::remove(path);
        return {};
    }
    return adopt(path, std::move(created).value());
}

std::shared_ptr<ImageRasterStore>
ImageRasterStore::adopt(QString filePath, std::shared_ptr<snow::image::RasterStore> store) {
    if (filePath.isEmpty() || !store)
        return {};
    return std::shared_ptr<ImageRasterStore>(
        new ImageRasterStore(std::move(filePath), std::move(store)));
}

std::shared_ptr<ImageRasterStore>
ImageRasterStore::retain(QString filePath, std::shared_ptr<snow::image::RasterStore> store,
                         std::shared_ptr<const void> cleanupOwner) {
    if (filePath.isEmpty() || !store || !cleanupOwner)
        return {};
    return std::shared_ptr<ImageRasterStore>(
        new ImageRasterStore(std::move(filePath), std::move(store), std::move(cleanupOwner)));
}

} // namespace snow::image_viewer
