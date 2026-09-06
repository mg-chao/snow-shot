#include "snow_shot/presentation/screenshotexportartifact.h"

#include "snowimageqtcodec.h"

#include <QBuffer>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

#include <snow/image/codec.h>
#include <snow/image/format.h>

#include <cstring>
#include <utility>
#include <vector>

namespace {
enum class RequestPhase { Empty, Pending, Ready, Failed };

template <typename Result, typename Callback>
void dispatchResult(QObject* receiver, Callback callback, Result result) {
    if (receiver == nullptr || !callback) {
        return;
    }
    const QPointer<QObject> guardedReceiver(receiver);
    if (QThread::currentThread() == receiver->thread()) {
        if (!guardedReceiver.isNull()) {
            callback(std::move(result));
        }
        return;
    }
    static_cast<void>(QMetaObject::invokeMethod(
        receiver,
        [guardedReceiver, callback = std::move(callback), result = std::move(result)]() mutable {
            if (!guardedReceiver.isNull()) {
                callback(std::move(result));
            }
        },
        Qt::QueuedConnection));
}

ScreenshotExportEncodingResult encodeCanonicalPng(const ScreenshotImageRowSource& source) {
    if (!source.isValid()) {
        return {{}, QStringLiteral("The screenshot row source is unavailable")};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {{}, QStringLiteral("The screenshot PNG buffer could not be opened")};
    }
    snow::image::EncodeOptions options;
    options.compression_level = 1;
    QString error;
    if (!snow_shot::image_codec::encodeToDevice(source, &buffer, snow::image::Format::png, options,
                                                &error)) {
        return {{},
                error.isEmpty() ? QStringLiteral("The screenshot could not be PNG encoded")
                                : error};
    }
    auto prepared = snow_shot::storage::PreparedPngImage::fromBytes(
        source.size, std::make_shared<const QByteArray>(std::move(bytes)));
    if (!prepared.has_value()) {
        return {{}, QStringLiteral("The encoded screenshot PNG is invalid")};
    }
    return {std::move(*prepared), {}};
}

ScreenshotExportEncodingResult encodeCanonicalPng(const QImage& image) {
    return encodeCanonicalPng(snow_shot::image_codec::srgbRowSource(image));
}
} // namespace

ScreenshotExportSource ScreenshotExportSource::fromImage(QImage image) {
    return fromProducer(
        [image = std::move(image)](const ScreenshotExportCancellation& cancellation) {
            return cancellation.isCancellationRequested() ? QImage{} : image;
        });
}

ScreenshotExportSource ScreenshotExportSource::fromImageLoader(ImageLoader loader) {
    ScreenshotExportSource source;
    source.m_imageLoader = std::move(loader);
    return source;
}

ScreenshotExportSource ScreenshotExportSource::fromProducer(ImageProducer producer,
                                                            RowSourceFactory rowSourceFactory) {
    ScreenshotExportSource source;
    source.m_imageProducer = std::move(producer);
    source.m_rowSourceFactory = std::move(rowSourceFactory);
    return source;
}

bool ScreenshotExportSource::isValid() const {
    return static_cast<bool>(m_imageLoader) || static_cast<bool>(m_imageProducer) ||
           static_cast<bool>(m_rowSourceFactory);
}

struct ScreenshotExportArtifact::Impl final {
    struct ImageSubscriber final {
        QPointer<QObject> receiver;
        ImageCallback callback;
    };
    struct EncodingSubscriber final {
        QPointer<QObject> receiver;
        EncodingCallback callback;
    };

    explicit Impl(ScreenshotExportSource value) : source(std::move(value)) {}

    ScreenshotExportSource source;
    mutable QMutex mutex;
    bool cancelled = false;
    RequestPhase imagePhase = RequestPhase::Empty;
    QImage image;
    QString imageError;
    std::vector<ImageSubscriber> imageSubscribers;
    RequestPhase encodingPhase = RequestPhase::Empty;
    snow_shot::storage::PreparedPngImage encoding;
    QString encodingError;
    std::vector<EncodingSubscriber> encodingSubscribers;
    ScreenshotExportJobHandle imageJob;
    ScreenshotExportJobHandle encodingJob;
    std::vector<ScreenshotExportJobHandle> outputJobs;
};

ScreenshotExportArtifact::ScreenshotExportArtifact(ScreenshotExportSource source, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(std::move(source))) {}

ScreenshotExportArtifact::~ScreenshotExportArtifact() {
    cancel();
}

bool ScreenshotExportArtifact::requestImage(QObject* receiver, ImageCallback callback) {
    if (receiver == nullptr || !callback || m_impl == nullptr) {
        return false;
    }
    ScreenshotExportImageResult ready;
    bool dispatchReady = false;
    bool start = false;
    {
        QMutexLocker lock(&m_impl->mutex);
        if (m_impl->cancelled || !m_impl->source.isValid()) {
            return false;
        }
        if (m_impl->imagePhase == RequestPhase::Ready) {
            ready.image = m_impl->image;
            dispatchReady = true;
        } else if (m_impl->imagePhase == RequestPhase::Failed) {
            ready.error = m_impl->imageError;
            dispatchReady = true;
        } else {
            m_impl->imageSubscribers.push_back({receiver, std::move(callback)});
            if (m_impl->imagePhase == RequestPhase::Empty) {
                m_impl->imagePhase = RequestPhase::Pending;
                start = true;
            }
        }
    }
    if (dispatchReady) {
        dispatchResult(receiver, std::move(callback), std::move(ready));
    } else if (start) {
        startImage();
    }
    return true;
}

void ScreenshotExportArtifact::startImage() {
    const QPointer<ScreenshotExportArtifact> guarded(this);
    if (m_impl->source.m_imageLoader) {
        const bool scheduled = m_impl->source.m_imageLoader(this, [guarded](QImage image) mutable {
            if (!guarded.isNull()) {
                guarded->completeImage({std::move(image), {}});
            }
        });
        if (!scheduled) {
            completeImage(
                {{}, QStringLiteral("The screenshot image request could not be started")});
        }
        return;
    }
    if (!m_impl->source.m_imageProducer) {
        completeImage({{}, QStringLiteral("The screenshot image source is unavailable")});
        return;
    }
    const auto producer = m_impl->source.m_imageProducer;
    m_impl->imageJob = ScreenshotExportCoordinator::shared().submit(
        this, ScreenshotExportCoordinator::Priority::Foreground,
        [producer](const ScreenshotExportCancellation& cancellation) {
            ScreenshotExportTaskResult result;
            result.image = producer(cancellation);
            if (result.image.isNull()) {
                return ScreenshotExportTaskResult::failure(
                    cancellation.isCancellationRequested() ? ScreenshotExportFailureStage::Cancelled
                                                           : ScreenshotExportFailureStage::Render,
                    cancellation.isCancellationRequested()
                        ? QStringLiteral("The screenshot image request was cancelled")
                        : QStringLiteral("The screenshot image could not be rendered"));
            }
            return result;
        },
        [guarded](ScreenshotExportTaskResult result) mutable {
            if (!guarded.isNull()) {
                guarded->completeImage(
                    {result.succeeded() ? std::move(result.image) : QImage{}, result.error});
            }
        });
    if (!m_impl->imageJob.isValid()) {
        completeImage({{}, QStringLiteral("The screenshot export queue is full")});
    }
}

void ScreenshotExportArtifact::completeImage(ScreenshotExportImageResult result) {
    std::vector<Impl::ImageSubscriber> subscribers;
    {
        QMutexLocker lock(&m_impl->mutex);
        if (m_impl->cancelled || m_impl->imagePhase != RequestPhase::Pending) {
            return;
        }
        if (result.succeeded()) {
            m_impl->imagePhase = RequestPhase::Ready;
            m_impl->image = result.image;
        } else {
            m_impl->imagePhase = RequestPhase::Failed;
            m_impl->imageError = result.error.isEmpty()
                                     ? QStringLiteral("The screenshot image is unavailable")
                                     : result.error;
            result.error = m_impl->imageError;
        }
        subscribers = std::move(m_impl->imageSubscribers);
        m_impl->imageSubscribers.clear();
    }
    for (auto& subscriber : subscribers) {
        if (!subscriber.receiver.isNull()) {
            ScreenshotExportImageResult delivered{result.image, result.error};
            dispatchResult(subscriber.receiver, std::move(subscriber.callback),
                           std::move(delivered));
        }
    }
}

bool ScreenshotExportArtifact::requestCanonicalPng(QObject* receiver, EncodingCallback callback) {
    if (receiver == nullptr || !callback || m_impl == nullptr) {
        return false;
    }
    ScreenshotExportEncodingResult ready;
    bool dispatchReady = false;
    bool start = false;
    {
        QMutexLocker lock(&m_impl->mutex);
        if (m_impl->cancelled || !m_impl->source.isValid()) {
            return false;
        }
        if (m_impl->encodingPhase == RequestPhase::Ready) {
            ready.image = m_impl->encoding;
            dispatchReady = true;
        } else if (m_impl->encodingPhase == RequestPhase::Failed) {
            ready.error = m_impl->encodingError;
            dispatchReady = true;
        } else {
            m_impl->encodingSubscribers.push_back({receiver, std::move(callback)});
            if (m_impl->encodingPhase == RequestPhase::Empty) {
                m_impl->encodingPhase = RequestPhase::Pending;
                start = true;
            }
        }
    }
    if (dispatchReady) {
        dispatchResult(receiver, std::move(callback), std::move(ready));
    } else if (start) {
        startCanonicalPng();
    }
    return true;
}

void ScreenshotExportArtifact::startCanonicalPng() {
    const QPointer<ScreenshotExportArtifact> guarded(this);
    if (m_impl->source.m_rowSourceFactory) {
        const auto factory = m_impl->source.m_rowSourceFactory;
        auto encoded = std::make_shared<ScreenshotExportEncodingResult>();
        m_impl->encodingJob = ScreenshotExportCoordinator::shared().submit(
            this, ScreenshotExportCoordinator::Priority::Background,
            [factory, encoded](const ScreenshotExportCancellation& cancellation) {
                const ScreenshotImageRowSource source =
                    factory([&cancellation]() { return cancellation.isCancellationRequested(); });
                *encoded = encodeCanonicalPng(source);
                if (!encoded->succeeded()) {
                    return ScreenshotExportTaskResult::failure(
                        cancellation.isCancellationRequested()
                            ? ScreenshotExportFailureStage::Cancelled
                            : ScreenshotExportFailureStage::Render,
                        encoded->error);
                }
                return ScreenshotExportTaskResult{};
            },
            [guarded, encoded](ScreenshotExportTaskResult result) mutable {
                if (guarded.isNull()) {
                    return;
                }
                guarded->completeCanonicalPng(
                    result.succeeded() ? std::move(*encoded)
                                       : ScreenshotExportEncodingResult{{}, result.error});
            });
        if (!m_impl->encodingJob.isValid()) {
            completeCanonicalPng({{}, QStringLiteral("The screenshot export queue is full")});
        }
        return;
    }
    if (!requestImage(this, [guarded](ScreenshotExportImageResult result) mutable {
            if (!guarded.isNull()) {
                if (result.succeeded()) {
                    guarded->startCanonicalPngFromImage(std::move(result.image));
                } else {
                    guarded->completeCanonicalPng({{}, result.error});
                }
            }
        })) {
        completeCanonicalPng(
            {{}, QStringLiteral("The screenshot image request could not be started")});
    }
}

void ScreenshotExportArtifact::startCanonicalPngFromImage(QImage image) {
    const QPointer<ScreenshotExportArtifact> guarded(this);
    auto encoded = std::make_shared<ScreenshotExportEncodingResult>();
    m_impl->encodingJob = ScreenshotExportCoordinator::shared().submit(
        this, ScreenshotExportCoordinator::Priority::Background,
        [image = std::move(image), encoded](const ScreenshotExportCancellation& cancellation) {
            if (cancellation.isCancellationRequested()) {
                return ScreenshotExportTaskResult::failure(
                    ScreenshotExportFailureStage::Cancelled,
                    QStringLiteral("The screenshot PNG encoding was cancelled"));
            }
            *encoded = encodeCanonicalPng(image);
            return encoded->succeeded() ? ScreenshotExportTaskResult{}
                                        : ScreenshotExportTaskResult::failure(
                                              ScreenshotExportFailureStage::Render, encoded->error);
        },
        [guarded, encoded](ScreenshotExportTaskResult result) mutable {
            if (!guarded.isNull()) {
                guarded->completeCanonicalPng(
                    result.succeeded() ? std::move(*encoded)
                                       : ScreenshotExportEncodingResult{{}, result.error});
            }
        });
    if (!m_impl->encodingJob.isValid()) {
        completeCanonicalPng({{}, QStringLiteral("The screenshot export queue is full")});
    }
}

void ScreenshotExportArtifact::completeCanonicalPng(ScreenshotExportEncodingResult result) {
    std::vector<Impl::EncodingSubscriber> subscribers;
    {
        QMutexLocker lock(&m_impl->mutex);
        if (m_impl->cancelled || m_impl->encodingPhase != RequestPhase::Pending) {
            return;
        }
        if (result.succeeded()) {
            m_impl->encodingPhase = RequestPhase::Ready;
            m_impl->encoding = result.image;
        } else {
            m_impl->encodingPhase = RequestPhase::Failed;
            m_impl->encodingError = result.error.isEmpty()
                                        ? QStringLiteral("The screenshot PNG is unavailable")
                                        : result.error;
            result.error = m_impl->encodingError;
        }
        subscribers = std::move(m_impl->encodingSubscribers);
        m_impl->encodingSubscribers.clear();
    }
    for (auto& subscriber : subscribers) {
        if (!subscriber.receiver.isNull()) {
            ScreenshotExportEncodingResult delivered{result.image, result.error};
            dispatchResult(subscriber.receiver, std::move(subscriber.callback),
                           std::move(delivered));
        }
    }
}

bool ScreenshotExportArtifact::requestClipboard(QObject* receiver, ClipboardCallback callback) {
    if (receiver == nullptr || !callback || isCancelled())
        return false;
    const QPointer<ScreenshotExportArtifact> guarded(this);
    const QPointer<QObject> target(receiver);
    return requestCanonicalPng(this, [guarded, target, callback = std::move(callback)](
                                         ScreenshotExportEncodingResult result) mutable {
        if (guarded.isNull() || guarded->isCancelled() || target.isNull())
            return;
        if (!result.succeeded()) {
            dispatchResult(target, std::move(callback),
                           ScreenshotExportClipboardResult{{}, result.error});
            return;
        }
        // Clipboard, PNG saving and history subscribe to the same immutable encoding.
        auto completion = std::make_shared<ClipboardCallback>(std::move(callback));
        const bool scheduled = guarded->prepareClipboard(
            target, result.image.bytes(),
            [completion](ScreenshotExportClipboardResult prepared) mutable {
                if (*completion) {
                    auto deliver = std::move(*completion);
                    deliver(std::move(prepared));
                }
            });
        if (!scheduled && *completion) {
            dispatchResult(target, std::move(*completion),
                           ScreenshotExportClipboardResult{
                               {}, QStringLiteral("The screenshot export queue is full")});
        }
    });
}

bool ScreenshotExportArtifact::prepareClipboard(QObject* receiver, QByteArray canonicalPng,
                                                ClipboardCallback callback) {
    if (receiver == nullptr || !callback || m_impl == nullptr || isCancelled()) {
        return false;
    }
    const QPointer<ScreenshotExportArtifact> guardedArtifact(this);
    const QPointer<QObject> guardedReceiver(receiver);
    auto scheduleImage = [this, guardedArtifact, guardedReceiver, receiver,
                          canonicalPng](QImage image, ClipboardCallback completion) mutable {
        auto payload = std::make_shared<ScreenshotClipboardPayload>();
        auto sharedCompletion = std::make_shared<ClipboardCallback>(std::move(completion));
        ScreenshotExportJobHandle job = ScreenshotExportCoordinator::shared().submit(
            receiver, ScreenshotExportCoordinator::Priority::Foreground,
            [image = std::move(image), canonicalPng,
             payload](const ScreenshotExportCancellation& cancellation) mutable {
                if (cancellation.isCancellationRequested()) {
                    return ScreenshotExportTaskResult::failure(
                        ScreenshotExportFailureStage::Cancelled,
                        QStringLiteral("The screenshot clipboard preparation was cancelled"));
                }
                *payload = ScreenshotClipboardService::prepareImage(image, canonicalPng);
                return payload->isValid()
                           ? ScreenshotExportTaskResult{}
                           : ScreenshotExportTaskResult::failure(
                                 ScreenshotExportFailureStage::Clipboard,
                                 QStringLiteral("The screenshot clipboard payload is invalid"));
            },
            [guardedArtifact, guardedReceiver, payload,
             sharedCompletion](ScreenshotExportTaskResult result) mutable {
                if (!guardedArtifact.isNull() && !guardedArtifact->isCancelled() &&
                    !guardedReceiver.isNull() && *sharedCompletion) {
                    dispatchResult(
                        guardedReceiver, std::move(*sharedCompletion),
                        ScreenshotExportClipboardResult{
                            result.succeeded() ? std::move(*payload) : ScreenshotClipboardPayload{},
                            result.error});
                }
            });
        if (!job.isValid()) {
            if (!guardedArtifact.isNull() && !guardedArtifact->isCancelled() &&
                !guardedReceiver.isNull() && *sharedCompletion) {
                dispatchResult(guardedReceiver, std::move(*sharedCompletion),
                               ScreenshotExportClipboardResult{
                                   {}, QStringLiteral("The screenshot export queue is full")});
            }
            return false;
        }
        QMutexLocker lock(&m_impl->mutex);
        m_impl->outputJobs.push_back(job);
        return true;
    };

    if (m_impl->source.m_rowSourceFactory) {
        const auto factory = m_impl->source.m_rowSourceFactory;
        auto payload = std::make_shared<ScreenshotClipboardPayload>();
        ScreenshotExportJobHandle job = ScreenshotExportCoordinator::shared().submit(
            receiver, ScreenshotExportCoordinator::Priority::Foreground,
            [factory, canonicalPng,
             payload](const ScreenshotExportCancellation& cancellation) mutable {
                const ScreenshotImageRowSource source =
                    factory([&cancellation]() { return cancellation.isCancellationRequested(); });
                *payload = ScreenshotClipboardService::prepare(source, canonicalPng);
                return payload->isValid()
                           ? ScreenshotExportTaskResult{}
                           : ScreenshotExportTaskResult::failure(
                                 cancellation.isCancellationRequested()
                                     ? ScreenshotExportFailureStage::Cancelled
                                     : ScreenshotExportFailureStage::Clipboard,
                                 QStringLiteral("The screenshot clipboard payload is invalid"));
            },
            [guardedArtifact, guardedReceiver, payload,
             callback = std::move(callback)](ScreenshotExportTaskResult result) mutable {
                if (!guardedArtifact.isNull() && !guardedArtifact->isCancelled() &&
                    !guardedReceiver.isNull()) {
                    dispatchResult(
                        guardedReceiver, std::move(callback),
                        ScreenshotExportClipboardResult{
                            result.succeeded() ? std::move(*payload) : ScreenshotClipboardPayload{},
                            result.error});
                }
            });
        if (!job.isValid()) {
            return false;
        }
        QMutexLocker lock(&m_impl->mutex);
        m_impl->outputJobs.push_back(job);
        return true;
    }

    return requestImage(
        this, [guardedArtifact, guardedReceiver, scheduleImage,
               callback = std::move(callback)](ScreenshotExportImageResult result) mutable {
            if (result.succeeded()) {
                static_cast<void>(scheduleImage(std::move(result.image), std::move(callback)));
            } else if (!guardedArtifact.isNull() && !guardedArtifact->isCancelled() &&
                       !guardedReceiver.isNull()) {
                dispatchResult(guardedReceiver, std::move(callback),
                               ScreenshotExportClipboardResult{{}, result.error});
            }
        });
}

bool ScreenshotExportArtifact::requestAutomaticSave(
    QObject* receiver, QStringList directories, ScreenshotImageFileFormat format,
    QString filenameFormat, ScreenshotExportCoordinator::Completion callback) {
    if (receiver == nullptr || !callback || isCancelled())
        return false;
    const QPointer<ScreenshotExportArtifact> guarded(this);
    const QPointer<QObject> target(receiver);
    const auto factory = m_impl->source.m_rowSourceFactory;
    auto schedule = [guarded, target, directories = std::move(directories), format,
                     filenameFormat = std::move(filenameFormat), factory,
                     callback = std::move(callback)](snow_shot::storage::PreparedPngImage png,
                                                     QImage image, QString error) mutable {
        if (guarded.isNull() || guarded->isCancelled() || target.isNull())
            return;
        if (!error.isEmpty()) {
            dispatchResult(target, std::move(callback),
                           ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::File,
                                                               std::move(error)));
            return;
        }
        auto completion =
            std::make_shared<ScreenshotExportCoordinator::Completion>(std::move(callback));
        auto job = ScreenshotExportCoordinator::shared().submit(
            target, ScreenshotExportCoordinator::Priority::Background,
            [directories, format, filenameFormat, factory, png = std::move(png),
             image = std::move(image)](const ScreenshotExportCancellation& cancellation) {
                if (cancellation.isCancellationRequested()) {
                    return ScreenshotExportTaskResult::failure(
                        ScreenshotExportFailureStage::Cancelled,
                        QStringLiteral("The screenshot save was cancelled"));
                }
                ScreenshotImageFileSaveResult saved;
                if (png.isValid()) {
                    saved = ScreenshotImageFileService::saveAutomatically(png, directories,
                                                                          filenameFormat);
                } else if (factory) {
                    auto rows = factory(
                        [&cancellation]() { return cancellation.isCancellationRequested(); });
                    saved = ScreenshotImageFileService::saveAutomatically(rows, directories, format,
                                                                          filenameFormat);
                } else {
                    saved = ScreenshotImageFileService::saveAutomatically(image, directories,
                                                                          format, filenameFormat);
                }
                if (!saved.succeeded()) {
                    return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::File,
                                                               saved.error);
                }
                ScreenshotExportTaskResult result;
                result.savedPath = saved.path;
                return result;
            },
            [guarded, target, completion](ScreenshotExportTaskResult result) mutable {
                if (!guarded.isNull() && !guarded->isCancelled() && !target.isNull()) {
                    dispatchResult(target, std::move(*completion), std::move(result));
                }
            });
        if (!job.isValid()) {
            dispatchResult(target, std::move(*completion),
                           ScreenshotExportTaskResult::failure(
                               ScreenshotExportFailureStage::Queue,
                               QStringLiteral("The screenshot export queue is full")));
            return;
        }
        QMutexLocker lock(&guarded->m_impl->mutex);
        guarded->m_impl->outputJobs.push_back(job);
    };
    if (format == ScreenshotImageFileFormat::Png) {
        return requestCanonicalPng(
            this, [schedule = std::move(schedule)](ScreenshotExportEncodingResult result) mutable {
                schedule(std::move(result.image), {}, std::move(result.error));
            });
    }
    if (factory) {
        schedule({}, {}, {});
        return true;
    }
    return requestImage(
        this, [schedule = std::move(schedule)](ScreenshotExportImageResult result) mutable {
            schedule({}, std::move(result.image), std::move(result.error));
        });
}

void ScreenshotExportArtifact::cancel() {
    if (m_impl == nullptr) {
        return;
    }
    ScreenshotExportJobHandle imageJob;
    ScreenshotExportJobHandle encodingJob;
    std::vector<ScreenshotExportJobHandle> outputJobs;
    {
        QMutexLocker lock(&m_impl->mutex);
        if (m_impl->cancelled) {
            return;
        }
        m_impl->cancelled = true;
        imageJob = m_impl->imageJob;
        encodingJob = m_impl->encodingJob;
        outputJobs = m_impl->outputJobs;
        m_impl->imageSubscribers.clear();
        m_impl->encodingSubscribers.clear();
    }
    imageJob.cancel();
    encodingJob.cancel();
    for (const auto& job : outputJobs) {
        job.cancel();
    }
}

bool ScreenshotExportArtifact::isValid() const {
    return m_impl != nullptr && m_impl->source.isValid();
}

bool ScreenshotExportArtifact::isCancelled() const {
    if (m_impl == nullptr) {
        return true;
    }
    QMutexLocker lock(&m_impl->mutex);
    return m_impl->cancelled;
}
