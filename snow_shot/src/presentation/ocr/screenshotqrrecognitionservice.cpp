#include "snow_draw_engine_qt/snow_canvas_image.h"
#include "snow_shot/platform/applicationqos.h"
#include "snow_shot/runtime/runtimeactivitytracker.h"
#include "snow_shot/presentation/screenshotqrrecognitionservice.h"

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/wechat_qrcode.hpp>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSize>
#include <QThread>
#include <QTimer>

#if defined(Q_OS_WIN) && defined(_MSC_VER)
#include <concrt.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr qint64 kMaximumDetectorPixels = 1920LL * 1080LL;
constexpr int kMaximumDetectorEdge = 2560;
constexpr int kSessionIdleGraceMilliseconds = 1'000;

QString recognitionFailedMessage() {
    return QCoreApplication::translate("ScreenshotOcrController", "Barcode recognition failed");
}

QString recognitionModelsMissingMessage() {
    return QCoreApplication::translate("ScreenshotOcrController",
                                       "Barcode recognition components are missing or damaged");
}

QSize boundedDetectorSize(const QSize& sourceSize) {
    if (sourceSize.isEmpty()) {
        return {};
    }

    const double width = sourceSize.width();
    const double height = sourceSize.height();
    const double pixelScale =
        std::sqrt(static_cast<double>(kMaximumDetectorPixels) / (width * height));
    const double edgeScale = static_cast<double>(kMaximumDetectorEdge) / std::max(width, height);
    const double scale = std::min({1.0, pixelScale, edgeScale});
    return QSize(std::max(1, static_cast<int>(std::floor(width * scale))),
                 std::max(1, static_cast<int>(std::floor(height * scale))));
}

struct BarcodeDecoders {
    cv::Ptr<cv::wechat_qrcode::WeChatQRCode> qr;
    cv::Ptr<cv::barcode::BarcodeDetector> barcode;
    QString loadError;
};

// QFile resolves through Unicode-capable platform APIs, so model files can be
// read from install directories that OpenCV's narrow-char path handling cannot
// represent (for example non-ASCII Windows paths).
bool readModelFile(const QDir& modelsDirectory, const QString& name, std::vector<uchar>& output) {
    QFile file(modelsDirectory.filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open the WeChat QR model" << name << ":" << file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        qWarning() << "The WeChat QR model" << name << "is empty";
        return false;
    }
    output.assign(bytes.cbegin(), bytes.cend());
    return true;
}

BarcodeDecoders loadDecoders(const QString& modelsDirectoryPath) {
    BarcodeDecoders created;
    created.barcode = cv::makePtr<cv::barcode::BarcodeDetector>();
    const QDir modelsDirectory(
        modelsDirectoryPath.isEmpty()
            ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/qrcode"))
            : modelsDirectoryPath);
    std::vector<uchar> detectorPrototxt;
    std::vector<uchar> detectorCaffeModel;
    std::vector<uchar> superResolutionPrototxt;
    std::vector<uchar> superResolutionCaffeModel;
    const bool modelsReadable =
        readModelFile(modelsDirectory, QStringLiteral("detect.prototxt"), detectorPrototxt) &&
        readModelFile(modelsDirectory, QStringLiteral("detect.caffemodel"), detectorCaffeModel) &&
        readModelFile(modelsDirectory, QStringLiteral("sr.prototxt"), superResolutionPrototxt) &&
        readModelFile(modelsDirectory, QStringLiteral("sr.caffemodel"), superResolutionCaffeModel);
    if (modelsReadable) {
        try {
            created.qr = cv::makePtr<cv::wechat_qrcode::WeChatQRCode>(
                std::move(detectorPrototxt), std::move(detectorCaffeModel),
                std::move(superResolutionPrototxt), std::move(superResolutionCaffeModel));
        } catch (const std::exception& exception) {
            qWarning() << "Failed to load the WeChat QR models:" << exception.what();
            created.loadError = recognitionModelsMissingMessage();
        }
    } else {
        created.loadError = recognitionModelsMissingMessage();
    }
    return created;
}

ScreenshotQrRecognitionResult recognizeImage(QImage source, const std::atomic_bool& cancellation,
                                             const QString& modelsDirectoryPath,
                                             std::optional<BarcodeDecoders>& cachedDecoders,
                                             ScreenshotQrRecognitionMode mode) {
    try {
        const QSize originalSize = source.size();
        const QSize detectorSize = boundedDetectorSize(source.size());
        if (detectorSize.isEmpty() || cancellation.load(std::memory_order_relaxed)) {
            return {};
        }
        if (source.size() != detectorSize) {
            source = snowCanvasScaleImage(source, detectorSize, Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
        }
        if (cancellation.load(std::memory_order_relaxed)) {
            return {};
        }
        if (source.format() != QImage::Format_Grayscale8) {
            source = snowCanvasConvertImage(source, QImage::Format_Grayscale8);
        }
        if (source.isNull() || cancellation.load(std::memory_order_relaxed)) {
            return {};
        }

        // Cache only successful initialization, so restoring damaged/missing model files can
        // recover immediately. Every service owns its decoder cache on its serialized worker.
        if (!cachedDecoders.has_value() || cachedDecoders->qr.empty()) {
            auto loaded = loadDecoders(modelsDirectoryPath);
            // Loading cannot be interrupted inside OpenCV. Drop a retired job's fresh
            // models here and avoid running inference on a screenshot that has closed.
            if (cancellation.load(std::memory_order_acquire))
                return {};
            cachedDecoders = std::move(loaded);
        }
        const BarcodeDecoders& decoders = *cachedDecoders;
        if (decoders.qr.empty()) {
            return {{}, decoders.loadError, {}};
        }

        const cv::Mat view(source.height(), source.width(), CV_8UC1,
                           const_cast<uchar*>(source.constBits()),
                           static_cast<std::size_t>(source.bytesPerLine()));

        // Screenshots are usually upright: the DNN-based WeChat detector
        // handles QR codes first, and the traditional barcode detector only
        // runs when no QR code was found (EAN/UPC and other 1D symbologies).
        std::vector<cv::Mat> qrPoints;
        if (cancellation.load(std::memory_order_acquire))
            return {};
        const std::vector<std::string> qrTexts = decoders.qr->detectAndDecode(view, qrPoints);

        ScreenshotQrRecognitionResult result;
        for (std::size_t index = 0; index < qrTexts.size(); ++index) {
            const auto& value = qrTexts[index];
            if (value.empty())
                continue;
            const QString text =
                QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
            result.contents.push_back(text);
            if (index >= qrPoints.size() || qrPoints[index].total() != 8 ||
                qrPoints[index].type() != CV_32FC1)
                continue;
            const cv::Mat points = qrPoints[index].reshape(1, 4);
            QPolygonF corners;
            bool valid = true;
            for (int corner = 0; corner < 4; ++corner) {
                const double x = double(points.at<float>(corner, 0)) * originalSize.width() /
                                 detectorSize.width();
                const double y = double(points.at<float>(corner, 1)) * originalSize.height() /
                                 detectorSize.height();
                valid = valid && std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 &&
                        x <= originalSize.width() && y <= originalSize.height();
                corners.append(QPointF(x, y));
            }
            if (valid && !corners.boundingRect().isEmpty())
                result.detections.append({text, std::move(corners)});
        }
        if (mode == ScreenshotQrRecognitionMode::QrOnly || !result.contents.isEmpty() ||
            cancellation.load(std::memory_order_relaxed)) {
            return result;
        }

        // The barcode detector's region search misses perfectly synthetic or
        // edge-to-edge renderings, but the EAN/UPC decoder is reliable on its
        // own. Selections are already tight around the code, so decode the
        // whole image as one candidate region.
        const cv::Point2f bottomLeft(0.0F, static_cast<float>(view.rows));
        const cv::Point2f topRight(static_cast<float>(view.cols), 0.0F);
        const std::vector<cv::Point2f> wholeImage = {
            bottomLeft, {0.0F, 0.0F}, topRight, {topRight.x, bottomLeft.y}};
        std::vector<std::string> barcodeTexts;
        std::vector<std::string> barcodeTypes;
        decoders.barcode->decodeWithType(view, wholeImage, barcodeTexts, barcodeTypes);
        for (const std::string& value : barcodeTexts) {
            if (!value.empty()) {
                result.contents.push_back(
                    QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())));
            }
        }
        return result;
    } catch (const std::exception& exception) {
        qWarning() << "Barcode recognition failed:" << exception.what();
    } catch (...) {
        qWarning() << "Barcode recognition failed with an unknown error";
    }
    return {{}, recognitionFailedMessage(), {}};
}

#if defined(Q_OS_WIN) && defined(_MSC_VER)
class QrTaskScheduler final {
  public:
    QrTaskScheduler() {
        try {
            // OpenCV uses the current Concurrency Runtime scheduler on MSVC. Limit its
            // parallelism on this worker instead of changing OpenCV's process-wide settings.
            Concurrency::SchedulerPolicy policy;
            policy.SetConcurrencyLimits(1, 4);
            Concurrency::CurrentScheduler::Create(policy);
            m_attached = true;
        } catch (const std::exception& error) {
            qWarning() << "Could not configure the QR worker scheduler:" << error.what();
        }
    }

    ~QrTaskScheduler() {
        if (m_attached)
            Concurrency::CurrentScheduler::Detach();
    }

    Q_DISABLE_COPY_MOVE(QrTaskScheduler)

  private:
    bool m_attached = false;
};
#endif

class QrWorker final : public QObject {
  public:
    explicit QrWorker(QString modelsDirectory) : m_modelsDirectory(std::move(modelsDirectory)) {}

    ScreenshotQrRecognitionResult recognize(QImage image, const std::atomic_bool& cancellation,
                                            ScreenshotQrRecognitionMode mode) {
#if defined(Q_OS_WIN) && defined(_MSC_VER)
        // Construct and detach the scheduler on this worker's thread.
        if (!m_scheduler)
            m_scheduler.emplace();
#endif
        return recognizeImage(std::move(image), cancellation, m_modelsDirectory, m_decoders, mode);
    }

  private:
    const QString m_modelsDirectory;
#if defined(Q_OS_WIN) && defined(_MSC_VER)
    std::optional<QrTaskScheduler> m_scheduler;
#endif
    std::optional<BarcodeDecoders> m_decoders;
};
} // namespace

class ScreenshotQrRecognitionService::Impl final {
  public:
    Impl(ScreenshotQrRecognitionService* owner, QString modelsDirectory,
         int idleTimeoutMilliseconds)
        : m_owner(owner), m_modelsDirectory(std::move(modelsDirectory)),
          m_idleTimeoutMilliseconds(std::max(0, idleTimeoutMilliseconds)), m_idleTimer(owner) {
        m_idleTimer.setSingleShot(true);
        QObject::connect(&m_idleTimer, &QTimer::timeout, m_owner, [this] {
            if (m_runningRequest == nullptr)
                stopWorker();
        });
    }

    ~Impl() {
        shutdown();
    }

    RequestToken enqueue(RequestToken token, QImage image, QObject* receiver, Completion completion,
                         ScreenshotQrRecognitionMode mode, bool sessionBound) {
        auto request = std::make_shared<Request>();
        request->token = token;
        request->mode = mode;
        request->sessionBound = sessionBound;
        request->image = std::move(image);
        request->receiver = receiver;
        request->completion = std::move(completion);
        QPointer<ScreenshotQrRecognitionService> service(m_owner);
        request->receiverDestroyed =
            QObject::connect(receiver, &QObject::destroyed, m_owner, [service, token]() {
                if (service != nullptr) {
                    service->cancel(token);
                }
            });

        if (m_stopping) {
            QObject::disconnect(request->receiverDestroyed);
            return 0;
        }
        if (mode == ScreenshotQrRecognitionMode::QrOnly)
            m_automaticSessionActive = true;
        m_requests.insert(token, request);
        m_queue.push_back(request);
        startNext();
        return token;
    }

    void cancel(RequestToken token) {
        const auto request = m_requests.find(token);
        if (request == m_requests.end()) {
            return;
        }
        const RequestHandle job = request.value();
        job->cancellation.store(true, std::memory_order_release);
        m_requests.erase(request);
        QObject::disconnect(job->receiverDestroyed);
        if (job != m_runningRequest) {
            const auto queued = std::find(m_queue.begin(), m_queue.end(), job);
            if (queued != m_queue.end()) {
                m_queue.erase(queued);
            }
            startNext();
        }
    }

    void endAutomaticRecognitionSession() {
        m_automaticSessionActive = false;
        shortenIdleTimeout();
    }

    void endRecognitionSession(QObject* session) {
        const auto connection = m_sessions.take(session);
        if (connection) {
            QObject::disconnect(connection);
            shortenIdleTimeout();
        }
    }

  private:
    using RequestToken = ScreenshotQrRecognitionPort::RequestToken;
    using Completion = ScreenshotQrRecognitionPort::Completion;

    struct Request {
        snow_shot::runtime::RuntimeActivityLease activity =
            snow_shot::runtime::RuntimeActivityTracker::shared().acquire();
        RequestToken token = 0;
        ScreenshotQrRecognitionMode mode = ScreenshotQrRecognitionMode::QrAndBarcode;
        bool sessionBound = false;
        QImage image;
        QPointer<QObject> receiver;
        Completion completion;
        QMetaObject::Connection receiverDestroyed;
        std::atomic_bool cancellation = false;
        ScreenshotQrRecognitionResult result;
    };
    using RequestHandle = std::shared_ptr<Request>;

    void startNext() {
        if (m_stopping || m_runningRequest != nullptr) {
            return;
        }
        RequestHandle request;
        while (!m_queue.empty()) {
            request = std::move(m_queue.front());
            m_queue.pop_front();
            if (m_requests.contains(request->token) &&
                !request->cancellation.load(std::memory_order_acquire)) {
                break;
            }
            request.reset();
        }
        if (request == nullptr) {
            if (m_workerThread != nullptr)
                m_idleTimer.start(idleTimeout());
            return;
        }

        m_idleTimer.stop();
        if (m_workerThread == nullptr) {
            m_workerThread = new QThread(m_owner);
            m_workerThread->setObjectName(QStringLiteral("ScreenshotQrWorker"));
            m_worker = new QrWorker(m_modelsDirectory);
            m_worker->moveToThread(m_workerThread);
            QObject::connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
            snow_shot::platform::configureApplicationQoSThread(m_workerThread);
            m_workerThread->start();
        }
        if (request->sessionBound)
            trackSession(request->receiver.data());
        else if (request->mode == ScreenshotQrRecognitionMode::QrAndBarcode)
            m_usedWithoutSession = true;
        m_runningRequest = request;
        QMetaObject::invokeMethod(
            m_worker,
            [this, owner = m_owner, worker = m_worker, request] {
                if (!request->cancellation.load(std::memory_order_acquire)) {
                    request->result = worker->recognize(std::move(request->image),
                                                        request->cancellation, request->mode);
                }
                request->image = {};
                // shutdown() joins the worker before the owner can be destroyed. Completion
                // touches service state only on the owner's thread and is discarded on teardown.
                QMetaObject::invokeMethod(
                    owner, [this, request] { finish(request); }, Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    }

    void finish(const RequestHandle& request) {
        if (m_stopping || request != m_runningRequest) {
            return;
        }
        m_runningRequest.reset();

        startNext();
        deliver(request);
    }

    void deliver(const RequestHandle& request) {
        const auto found = m_requests.find(request->token);
        if (found == m_requests.end()) {
            return;
        }
        m_requests.erase(found);
        QObject::disconnect(request->receiverDestroyed);
        if (request->cancellation.load(std::memory_order_acquire) || request->receiver == nullptr ||
            !request->completion) {
            return;
        }
        QPointer<QObject> receiver = request->receiver;
        Completion completion = std::move(request->completion);
        ScreenshotQrRecognitionResult result = std::move(request->result);
        if (receiver != nullptr && completion) {
            completion(std::move(result));
        }
    }

    void shutdown() {
        if (m_stopping) {
            return;
        }
        m_stopping = true;
        for (auto request = m_requests.begin(); request != m_requests.end(); ++request) {
            request.value()->cancellation.store(true, std::memory_order_release);
            QObject::disconnect(request.value()->receiverDestroyed);
        }
        m_requests.clear();
        m_queue.clear();
        stopWorker();
        m_runningRequest.reset();
    }

    void stopWorker() {
        m_idleTimer.stop();
        m_automaticSessionActive = false;
        m_usedWithoutSession = false;
        for (const auto& connection : std::as_const(m_sessions))
            QObject::disconnect(connection);
        m_sessions.clear();
        QThread* const thread = m_workerThread;
        m_workerThread = nullptr;
        m_worker = nullptr;
        if (thread != nullptr) {
            thread->quit();
            thread->wait();
            delete thread;
        }
    }

    void trackSession(QObject* session) {
        if (session != nullptr && !m_sessions.contains(session)) {
            m_sessions.insert(
                session, QObject::connect(session, &QObject::destroyed, m_owner,
                                          [this, session] { endRecognitionSession(session); }));
        }
    }

    int idleTimeout() const {
        const bool keepWarm =
            m_automaticSessionActive || m_usedWithoutSession || !m_sessions.isEmpty();
        return keepWarm ? m_idleTimeoutMilliseconds
                        : std::min(m_idleTimeoutMilliseconds, kSessionIdleGraceMilliseconds);
    }

    void shortenIdleTimeout() {
        const int timeout = idleTimeout();
        if (m_idleTimer.remainingTime() > timeout)
            m_idleTimer.start(timeout);
    }

    ScreenshotQrRecognitionService* m_owner = nullptr;
    QString m_modelsDirectory;
    const int m_idleTimeoutMilliseconds;
    QHash<RequestToken, RequestHandle> m_requests;
    QHash<QObject*, QMetaObject::Connection> m_sessions;
    std::deque<RequestHandle> m_queue;
    QThread* m_workerThread = nullptr;
    QrWorker* m_worker = nullptr;
    QTimer m_idleTimer;
    RequestHandle m_runningRequest;
    bool m_automaticSessionActive = false;
    bool m_usedWithoutSession = false;
    bool m_stopping = false;
};

ScreenshotQrRecognitionService::ScreenshotQrRecognitionService(QObject* parent,
                                                               const QString& modelsDirectory,
                                                               int idleTimeoutMilliseconds)
    : ScreenshotQrRecognitionPort(parent),
      m_impl(std::make_unique<Impl>(this, modelsDirectory, idleTimeoutMilliseconds)) {}

ScreenshotQrRecognitionService::~ScreenshotQrRecognitionService() = default;

ScreenshotQrRecognitionPort::RequestToken
ScreenshotQrRecognitionService::recognize(QImage image, QObject* receiver, Completion completion,
                                          ScreenshotQrRecognitionMode mode) {
    return enqueue(std::move(image), receiver, std::move(completion), mode, false);
}

ScreenshotQrRecognitionPort::RequestToken
ScreenshotQrRecognitionService::recognizeForSession(QImage image, QObject* session,
                                                    Completion completion) {
    return enqueue(std::move(image), session, std::move(completion),
                   ScreenshotQrRecognitionMode::QrAndBarcode, true);
}

ScreenshotQrRecognitionPort::RequestToken
ScreenshotQrRecognitionService::enqueue(QImage image, QObject* receiver, Completion completion,
                                        ScreenshotQrRecognitionMode mode, bool sessionBound) {
    if (image.isNull() || receiver == nullptr || !completion || m_impl == nullptr) {
        return 0;
    }
    do {
        ++m_nextToken;
    } while (m_nextToken == 0);
    return m_impl->enqueue(m_nextToken, std::move(image), receiver, std::move(completion), mode,
                           sessionBound);
}

void ScreenshotQrRecognitionService::cancel(RequestToken token) {
    if (m_impl != nullptr && token != 0) {
        m_impl->cancel(token);
    }
}

void ScreenshotQrRecognitionService::endRecognitionSession(QObject* session) {
    m_impl->endRecognitionSession(session);
}

void ScreenshotQrRecognitionService::endAutomaticRecognitionSession() {
    m_impl->endAutomaticRecognitionSession();
}
