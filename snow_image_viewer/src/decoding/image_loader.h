#pragma once

#include "core/image_types.h"
#include "decoding/image_decoder.h"
#include "decoding/system_thumbnail.h"

#include <QCache>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QThreadPool>

#include <memory>
#include <optional>

namespace snow::image_viewer {

class ImageLoader final : public QObject {
    Q_OBJECT

  public:
    explicit ImageLoader(QObject* parent = nullptr);

    void load(const QString& filePath, int thumbnailExtent = kSystemThumbnailMaximumExtent);
    void prefetchThumbnails(const QStringList& filePaths);
    bool isLoading() const;
    static DecodeResult decodeSynchronously(const QString& filePath,
                                            const DecodeCancellation* cancellation = nullptr);

  signals:
    void loadingChanged(bool loading);
    void thumbnailLoaded(const snow::image_viewer::ImageThumbnail& thumbnail);
    void imageLoaded(const snow::image_viewer::DecodedImage& image);
    void loadFailed(const QString& filePath, const QString& error);

  private:
    struct LoadRequest {
        QString filePath;
        int thumbnailExtent = kSystemThumbnailMaximumExtent;
        QString thumbnailCacheKey;
        quint64 generation = 0;
        std::shared_ptr<DecodeCancellation> cancellation;
    };

    struct ThumbnailTask {
        QString filePath;
        int extent = kSystemThumbnailMaximumExtent;
        QString cacheKey;
        std::shared_ptr<DecodeCancellation> cancellation;
        bool displayWhenReady = false;
    };

    void startThumbnailLoad(const LoadRequest& request);
    void queuePrimaryThumbnailTask(ThumbnailTask task);
    void startPrimaryThumbnailTask(ThumbnailTask task);
    void startNextPrimaryThumbnailTask();
    void startPrefetchThumbnailTask(ThumbnailTask task);
    void startNextPrefetchThumbnailTask();
    void cancelPrefetchThumbnailTasksExcept(const QString& cacheKey);
    bool hasLiveThumbnailTask(const std::optional<ThumbnailTask>& task,
                              const QString& cacheKey) const;
    std::optional<ThumbnailTask> takeQueuedPrefetchTask(const QString& cacheKey);
    void cacheThumbnail(const QString& cacheKey, const ImageThumbnail& thumbnail);
    void emitCurrentThumbnail(const QString& cacheKey, const ImageThumbnail& thumbnail);
    void startPendingDecode();

    quint64 generation_ = 0;
    bool loading_ = false;
    bool decodeInFlight_ = false;
    QThreadPool decodePool_;
    QThreadPool thumbnailPool_;
    QThreadPool prefetchPool_;
    QCache<QString, ImageThumbnail> thumbnailCache_{48 * 1024};
    std::optional<LoadRequest> pendingRequest_;
    std::shared_ptr<DecodeCancellation> activeCancellation_;
    QString currentThumbnailCacheKey_;
    int thumbnailExtent_ = kSystemThumbnailMaximumExtent;
    std::optional<ThumbnailTask> activePrimaryThumbnailTask_;
    std::optional<ThumbnailTask> queuedPrimaryThumbnailTask_;
    std::optional<ThumbnailTask> activePrefetchThumbnailTask_;
    QList<ThumbnailTask> queuedPrefetchThumbnailTasks_;
};

} // namespace snow::image_viewer
