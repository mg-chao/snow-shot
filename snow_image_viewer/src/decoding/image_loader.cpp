#include "decoding/image_loader.h"

#include "decoding/snow_image_decoder.h"
#include "decoding/system_thumbnail.h"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent>

#include <algorithm>
#include <limits>
#include <memory>

namespace snow::image_viewer {
namespace {

QString thumbnailCacheKey(const QString& filePath, int thumbnailExtent) {
    const QFileInfo info(filePath);
    return QStringLiteral("%1\n%2\n%3\n%4")
        .arg(info.absoluteFilePath())
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(thumbnailExtent);
}

int thumbnailCacheCost(const ImageThumbnail& thumbnail) {
    const qsizetype bytes = thumbnail.pixels.sizeInBytes();
    const qsizetype kibibytes = std::max<qsizetype>(1, (bytes + 1023) / 1024);
    return kibibytes > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max()
                                                       : static_cast<int>(kibibytes);
}

DecodeResult cancellationResult() {
    return DecodeResult::failure(QStringLiteral("Image decoding was cancelled."));
}

} // namespace

ImageLoader::ImageLoader(QObject* parent) : QObject(parent) {
    qRegisterMetaType<DecodedImage>();
    qRegisterMetaType<ImageThumbnail>();
    qRegisterMetaType<DecodeResult>();
    decodePool_.setMaxThreadCount(1);
    decodePool_.setExpiryTimeout(-1);
    thumbnailPool_.setMaxThreadCount(1);
    thumbnailPool_.setExpiryTimeout(-1);
    prefetchPool_.setMaxThreadCount(1);
    prefetchPool_.setExpiryTimeout(-1);
}

void ImageLoader::load(const QString& filePath, int thumbnailExtent) {
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (activeCancellation_) {
        activeCancellation_->cancel();
    }
    if (pendingRequest_ && pendingRequest_->cancellation) {
        pendingRequest_->cancellation->cancel();
    }

    const LoadRequest request{absolutePath, thumbnailExtent,
                              thumbnailCacheKey(absolutePath, thumbnailExtent), ++generation_,
                              std::make_shared<DecodeCancellation>()};
    pendingRequest_ = request;
    currentThumbnailCacheKey_ = request.thumbnailCacheKey;
    thumbnailExtent_ = thumbnailExtent;
    if (!loading_) {
        loading_ = true;
        emit loadingChanged(true);
    }

    cancelPrefetchThumbnailTasksExcept(request.thumbnailCacheKey);
    startThumbnailLoad(request);
    // The newest request always gets the next available decode slot.
    startPendingDecode();
}

void ImageLoader::startThumbnailLoad(const LoadRequest& request) {
    if (const ImageThumbnail* cachedThumbnail = thumbnailCache_.object(request.thumbnailCacheKey)) {
        emitCurrentThumbnail(request.thumbnailCacheKey, *cachedThumbnail);
        return;
    }

    if (activePrimaryThumbnailTask_ &&
        hasLiveThumbnailTask(activePrimaryThumbnailTask_, request.thumbnailCacheKey)) {
        activePrimaryThumbnailTask_->displayWhenReady = true;
        return;
    }
    if (queuedPrimaryThumbnailTask_ &&
        hasLiveThumbnailTask(queuedPrimaryThumbnailTask_, request.thumbnailCacheKey)) {
        queuedPrimaryThumbnailTask_->displayWhenReady = true;
        return;
    }
    if (activePrefetchThumbnailTask_ &&
        hasLiveThumbnailTask(activePrefetchThumbnailTask_, request.thumbnailCacheKey)) {
        activePrefetchThumbnailTask_->displayWhenReady = true;
        return;
    }
    if (std::optional<ThumbnailTask> task = takeQueuedPrefetchTask(request.thumbnailCacheKey)) {
        task->displayWhenReady = true;
        queuePrimaryThumbnailTask(std::move(*task));
        return;
    }

    queuePrimaryThumbnailTask(ThumbnailTask{request.filePath, request.thumbnailExtent,
                                            request.thumbnailCacheKey,
                                            std::make_shared<DecodeCancellation>(), true});
}

void ImageLoader::prefetchThumbnails(const QStringList& filePaths) {
    if (loading_ || filePaths.isEmpty()) {
        return;
    }

    QSet<QString> requestedKeys;
    QList<ThumbnailTask> tasks;
    for (const QString& filePath : filePaths) {
        if (filePath.isEmpty()) {
            continue;
        }

        const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
        const QString cacheKey = thumbnailCacheKey(absolutePath, thumbnailExtent_);
        if (requestedKeys.contains(cacheKey)) {
            continue;
        }
        requestedKeys.insert(cacheKey);
        if (cacheKey == currentThumbnailCacheKey_ || thumbnailCache_.contains(cacheKey) ||
            hasLiveThumbnailTask(activePrimaryThumbnailTask_, cacheKey) ||
            hasLiveThumbnailTask(queuedPrimaryThumbnailTask_, cacheKey) ||
            hasLiveThumbnailTask(activePrefetchThumbnailTask_, cacheKey)) {
            continue;
        }
        tasks.append(ThumbnailTask{absolutePath, thumbnailExtent_, cacheKey,
                                   std::make_shared<DecodeCancellation>(), false});
    }

    queuedPrefetchThumbnailTasks_.clear();
    if (activePrefetchThumbnailTask_ && !activePrefetchThumbnailTask_->displayWhenReady &&
        !requestedKeys.contains(activePrefetchThumbnailTask_->cacheKey)) {
        activePrefetchThumbnailTask_->cancellation->cancel();
    }
    queuedPrefetchThumbnailTasks_.append(tasks);
    startNextPrefetchThumbnailTask();
}

void ImageLoader::queuePrimaryThumbnailTask(ThumbnailTask task) {
    if (activePrimaryThumbnailTask_) {
        if (activePrimaryThumbnailTask_->cacheKey == task.cacheKey &&
            !activePrimaryThumbnailTask_->cancellation->isCancelled()) {
            activePrimaryThumbnailTask_->displayWhenReady |= task.displayWhenReady;
            return;
        }
        activePrimaryThumbnailTask_->cancellation->cancel();
        queuedPrimaryThumbnailTask_ = std::move(task);
        return;
    }

    startPrimaryThumbnailTask(std::move(task));
}

// QFutureWatcher is owned by this QObject and also schedules deletion when its future finishes.
// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
void ImageLoader::startPrimaryThumbnailTask(ThumbnailTask task) {
    activePrimaryThumbnailTask_ = std::move(task);
    const ThumbnailTask& activeTask = *activePrimaryThumbnailTask_;
    auto* thumbnailWatcher = new QFutureWatcher<ImageThumbnail>(this);
    connect(thumbnailWatcher, &QFutureWatcher<ImageThumbnail>::finished, this,
            [this, thumbnailWatcher, cacheKey = activeTask.cacheKey,
             cancellation = activeTask.cancellation]() {
                const ImageThumbnail thumbnail = thumbnailWatcher->result();
                thumbnailWatcher->deleteLater();
                if (!activePrimaryThumbnailTask_ ||
                    activePrimaryThumbnailTask_->cancellation != cancellation) {
                    return;
                }
                const bool displayWhenReady = activePrimaryThumbnailTask_->displayWhenReady;
                activePrimaryThumbnailTask_.reset();
                if (!cancellation->isCancelled() && thumbnail.isValid()) {
                    cacheThumbnail(cacheKey, thumbnail);
                    if (displayWhenReady) {
                        emitCurrentThumbnail(cacheKey, thumbnail);
                    }
                }
                startNextPrimaryThumbnailTask();
            });
    thumbnailWatcher->setFuture(QtConcurrent::run(
        &thumbnailPool_, [filePath = activeTask.filePath, extent = activeTask.extent,
                          cancellation = activeTask.cancellation]() {
            return loadSystemThumbnail(filePath, extent, cancellation.get());
        }));
}
// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

void ImageLoader::startNextPrimaryThumbnailTask() {
    if (activePrimaryThumbnailTask_ || !queuedPrimaryThumbnailTask_) {
        return;
    }
    ThumbnailTask task = std::move(*queuedPrimaryThumbnailTask_);
    queuedPrimaryThumbnailTask_.reset();
    startPrimaryThumbnailTask(std::move(task));
}

// QFutureWatcher is owned by this QObject and also schedules deletion when its future finishes.
// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
void ImageLoader::startPrefetchThumbnailTask(ThumbnailTask task) {
    activePrefetchThumbnailTask_ = std::move(task);
    const ThumbnailTask& activeTask = *activePrefetchThumbnailTask_;
    auto* thumbnailWatcher = new QFutureWatcher<ImageThumbnail>(this);
    connect(thumbnailWatcher, &QFutureWatcher<ImageThumbnail>::finished, this,
            [this, thumbnailWatcher, cacheKey = activeTask.cacheKey,
             cancellation = activeTask.cancellation]() {
                const ImageThumbnail thumbnail = thumbnailWatcher->result();
                thumbnailWatcher->deleteLater();
                if (!activePrefetchThumbnailTask_ ||
                    activePrefetchThumbnailTask_->cancellation != cancellation) {
                    return;
                }
                const bool displayWhenReady = activePrefetchThumbnailTask_->displayWhenReady;
                activePrefetchThumbnailTask_.reset();
                if (!cancellation->isCancelled() && thumbnail.isValid()) {
                    cacheThumbnail(cacheKey, thumbnail);
                    if (displayWhenReady) {
                        emitCurrentThumbnail(cacheKey, thumbnail);
                    }
                }
                startNextPrefetchThumbnailTask();
            });
    thumbnailWatcher->setFuture(QtConcurrent::run(
        &prefetchPool_, [filePath = activeTask.filePath, extent = activeTask.extent,
                         cancellation = activeTask.cancellation]() {
            return loadSystemThumbnail(filePath, extent, cancellation.get());
        }));
}
// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

void ImageLoader::startNextPrefetchThumbnailTask() {
    if (activePrefetchThumbnailTask_ || queuedPrefetchThumbnailTasks_.isEmpty()) {
        return;
    }
    startPrefetchThumbnailTask(queuedPrefetchThumbnailTasks_.takeFirst());
}

void ImageLoader::cancelPrefetchThumbnailTasksExcept(const QString& cacheKey) {
    queuedPrefetchThumbnailTasks_.clear();
    if (activePrefetchThumbnailTask_ && activePrefetchThumbnailTask_->cacheKey != cacheKey) {
        activePrefetchThumbnailTask_->cancellation->cancel();
    }
}

bool ImageLoader::hasLiveThumbnailTask(const std::optional<ThumbnailTask>& task,
                                       const QString& cacheKey) const {
    return task && task->cacheKey == cacheKey && task->cancellation &&
           !task->cancellation->isCancelled();
}

std::optional<ImageLoader::ThumbnailTask>
ImageLoader::takeQueuedPrefetchTask(const QString& cacheKey) {
    for (qsizetype index = 0; index < queuedPrefetchThumbnailTasks_.size(); ++index) {
        if (queuedPrefetchThumbnailTasks_.at(index).cacheKey == cacheKey) {
            return queuedPrefetchThumbnailTasks_.takeAt(index);
        }
    }
    return std::nullopt;
}

void ImageLoader::cacheThumbnail(const QString& cacheKey, const ImageThumbnail& thumbnail) {
    thumbnailCache_.insert(cacheKey, new ImageThumbnail(thumbnail), thumbnailCacheCost(thumbnail));
}

void ImageLoader::emitCurrentThumbnail(const QString& cacheKey, const ImageThumbnail& thumbnail) {
    if (loading_ && cacheKey == currentThumbnailCacheKey_ && thumbnail.isValid()) {
        emit thumbnailLoaded(thumbnail);
    }
}

void ImageLoader::startPendingDecode() {
    if (decodeInFlight_ || !pendingRequest_.has_value()) {
        return;
    }

    const LoadRequest request = *pendingRequest_;
    decodeInFlight_ = true;
    activeCancellation_ = request.cancellation;

    auto* watcher = new QFutureWatcher<DecodeResult>(this);
    connect(watcher, &QFutureWatcher<DecodeResult>::finished, this, [this, watcher, request]() {
        const DecodeResult result = watcher->result();
        watcher->deleteLater();
        decodeInFlight_ = false;
        if (activeCancellation_ == request.cancellation) {
            activeCancellation_.reset();
        }
        if (request.generation != generation_) {
            startPendingDecode();
            return;
        }

        pendingRequest_.reset();
        loading_ = false;
        emit loadingChanged(false);
        if (result.succeeded()) {
            emit imageLoaded(result.image);
        } else {
            emit loadFailed(request.filePath, result.error);
        }
    });
    watcher->setFuture(QtConcurrent::run(&decodePool_, [request]() {
        return ImageLoader::decodeSynchronously(request.filePath, request.cancellation.get());
    }));
}

bool ImageLoader::isLoading() const {
    return loading_;
}

DecodeResult ImageLoader::decodeSynchronously(const QString& filePath,
                                              const DecodeCancellation* cancellation) {
    DecodeCancellation uncancelled;
    const DecodeCancellation& requestCancellation = cancellation ? *cancellation : uncancelled;
    if (requestCancellation.isCancelled()) {
        return cancellationResult();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return DecodeResult::failure(file.errorString());
    }
    const QByteArray header = file.read(256);
    file.close();
    if (requestCancellation.isCancelled()) {
        return cancellationResult();
    }

    SnowImageDecoder decoder;
    if (decoder.canDecode(filePath, header)) {
        const DecodeResult result = decoder.decode(filePath, requestCancellation);
        return requestCancellation.isCancelled() ? cancellationResult() : result;
    }
    return DecodeResult::failure(QStringLiteral("This image format is not supported."));
}

} // namespace snow::image_viewer
