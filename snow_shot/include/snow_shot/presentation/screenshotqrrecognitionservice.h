#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTQRRECOGNITIONSERVICE_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTQRRECOGNITIONSERVICE_H

#include "snow_shot/app/edition.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QPolygonF>

#include <functional>
#include <memory>
#include <utility>

struct ScreenshotQrDetection {
    QString text;
    // Four ordered corners in the original input image's pixel coordinates.
    QPolygonF corners;
};

enum class ScreenshotQrRecognitionMode { QrAndBarcode, QrOnly };

struct ScreenshotQrRecognitionResult {
    QStringList contents;
    QString error;
    QList<ScreenshotQrDetection> detections;
};

class ScreenshotQrRecognitionPort : public QObject {
  public:
    explicit ScreenshotQrRecognitionPort(QObject* parent = nullptr) : QObject(parent) {}
    using RequestToken = quint64;
    using Completion = std::function<void(ScreenshotQrRecognitionResult)>;

    ~ScreenshotQrRecognitionPort() override = default;
    virtual RequestToken
    recognize(QImage image, QObject* receiver, Completion completion,
              ScreenshotQrRecognitionMode mode = ScreenshotQrRecognitionMode::QrAndBarcode) = 0;
    // Repeated requests can use warm decoders while the recognition session is alive.
    virtual RequestToken recognizeForSession(QImage image, QObject* session,
                                             Completion completion) {
        return recognize(std::move(image), session, std::move(completion));
    }
    virtual void cancel(RequestToken token) = 0;
    virtual void endRecognitionSession(QObject*) {}
    virtual void endAutomaticRecognitionSession() {}
};

#if SNOW_SHOT_ENABLE_QR_RECOGNITION
class ScreenshotQrRecognitionService final : public ScreenshotQrRecognitionPort {
    Q_OBJECT

  public:
    // Consecutive requests share one serialized worker and its decoder sessions. The worker
    // releases its models after a bounded idle grace period. A worker without a remaining
    // session or an unscoped request expires sooner.
    // An empty modelsDirectory resolves to <application dir>/assets/qrcode at runtime; tests
    // inject a custom directory to exercise non-ASCII installation paths.
    explicit ScreenshotQrRecognitionService(QObject* parent = nullptr,
                                            const QString& modelsDirectory = QString(),
                                            int idleTimeoutMilliseconds = 30'000);
    ~ScreenshotQrRecognitionService() override;

    RequestToken recognize(
        QImage image, QObject* receiver, Completion completion,
        ScreenshotQrRecognitionMode mode = ScreenshotQrRecognitionMode::QrAndBarcode) override;
    RequestToken recognizeForSession(QImage image, QObject* session,
                                     Completion completion) override;
    void cancel(RequestToken token) override;
    void endRecognitionSession(QObject* session) override;
    void endAutomaticRecognitionSession() override;

  private:
    RequestToken enqueue(QImage image, QObject* receiver, Completion completion,
                         ScreenshotQrRecognitionMode mode, bool sessionBound);

    class Impl;
    std::unique_ptr<Impl> m_impl;
    RequestToken m_nextToken = 0;
};

#endif // SNOW_SHOT_ENABLE_QR_RECOGNITION

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTQRRECOGNITIONSERVICE_H
