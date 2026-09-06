#include "snow_shot/presentation/screenshotexportartifact.h"
#include <QTemporaryDir>
#include <QFile>

#include "snow_draw_engine_qt/snow_canvas_runtime.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QThread>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include <windows.h>
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void processUntil(const std::function<bool()>& predicate) {
    for (int iteration = 0; iteration < 1000 && !predicate(); ++iteration) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    require(predicate(), "timed out waiting for export artifact completion");
}

QImage testImage() {
    QImage image(QSize(37, 19), QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor((x * 9) % 256, (y * 17) % 256, (x + y) % 256, 255));
        }
    }
    return image;
}

ScreenshotImageRowSource rowSourceFor(const QImage& source, std::function<bool()> cancellation) {
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    const qsizetype rowBytes = static_cast<qsizetype>(image.width()) * 4;
    ScreenshotImageRowSource rows;
    rows.size = image.size();
    rows.cancellationRequested = std::move(cancellation);
    rows.readRows = [image, rowBytes](int firstRow, int rowCount, qsizetype stride,
                                      uchar* destination, qsizetype destinationSize) {
        if (firstRow < 0 || rowCount <= 0 || rowCount > image.height() - firstRow ||
            stride < rowBytes || destination == nullptr ||
            destinationSize < stride * (rowCount - 1) + rowBytes) {
            return false;
        }
        for (int row = 0; row < rowCount; ++row) {
            std::memcpy(destination + static_cast<qsizetype>(row) * stride,
                        image.constScanLine(firstRow + row), static_cast<std::size_t>(rowBytes));
        }
        return true;
    };
    return rows;
}

void clipboardAndSaveShareCanonicalEncoding() {
    const QImage image = testImage();
    for (bool rowBacked : {false, true}) {
        std::atomic_int materializations = 0;
        std::atomic_int rowFactories = 0;
        ScreenshotExportSource::RowSourceFactory factory;
        if (rowBacked) {
            factory = [&rowFactories, image](std::function<bool()> cancellation) {
                ++rowFactories;
                return rowSourceFor(image, std::move(cancellation));
            };
        }
        ScreenshotExportArtifact artifact(ScreenshotExportSource::fromProducer(
            [&materializations, image](const ScreenshotExportCancellation&) {
                ++materializations;
                return image;
            },
            factory));
        QTemporaryDir directory;
        require(directory.isValid(), "temporary save directory unavailable");
        QObject receiver;
        int callbacks = 0;
        QByteArray canonical;
        QByteArray clipboard;
        QString path;
        require(artifact.requestCanonicalPng(&receiver,
                                             [&](ScreenshotExportEncodingResult result) {
                                                 require(result.succeeded(),
                                                         "canonical PNG failed");
                                                 canonical = result.image.bytes();
                                                 ++callbacks;
                                             }),
                "canonical request rejected");
        require(artifact.requestClipboard(&receiver,
                                          [&](ScreenshotExportClipboardResult result) {
                                              require(result.succeeded(),
                                                      "clipboard preparation failed");
                                              clipboard = result.payload.pngBytes();
                                              ++callbacks;
                                          }),
                "clipboard request rejected");
        require(artifact.requestAutomaticSave(
                    &receiver, {directory.path()}, ScreenshotImageFileFormat::Png,
                    QStringLiteral("shared"),
                    [&](ScreenshotExportTaskResult result) {
                        require(result.succeeded(), "automatic PNG save failed");
                        path = result.savedPath;
                        ++callbacks;
                    }),
                "save request rejected");
        processUntil([&] { return callbacks == 3; });
        require(canonical.constData() == clipboard.constData(),
                "clipboard did not reuse the canonical PNG buffer");
        QFile saved(path);
        require(saved.open(QIODevice::ReadOnly) && saved.readAll() == canonical,
                "saved PNG differs from clipboard/history encoding");
        require(materializations == (rowBacked ? 0 : 1),
                "image source was materialized more than once");
        // One source for encoding and one for the raw DIB fallback. Saving reads no pixels.
        require(rowFactories == (rowBacked ? 2 : 0), "PNG was redundantly encoded for an output");
        require(artifact.requestClipboard(&receiver,
                                          [&](ScreenshotExportClipboardResult result) {
                                              require(result.succeeded() &&
                                                          result.payload.pngBytes().constData() ==
                                                              canonical.constData(),
                                                      "cached clipboard request re-encoded PNG");
                                              ++callbacks;
                                          }),
                "cached clipboard request rejected");
        processUntil([&] { return callbacks == 4; });
        require(rowFactories == (rowBacked ? 3 : 0),
                "cached PNG request encoded or materialized an image again");
    }
}

void nonPngSaveReadsPixelsWithoutEncodingPng() {
    const QImage image = testImage();
    for (bool rowBacked : {false, true}) {
        std::atomic_int imageCalls = 0;
        std::atomic_int rowCalls = 0;
        ScreenshotExportSource::RowSourceFactory factory;
        if (rowBacked) {
            factory = [&rowCalls, image](std::function<bool()> cancellation) {
                ++rowCalls;
                return rowSourceFor(image, std::move(cancellation));
            };
        }
        ScreenshotExportArtifact artifact(ScreenshotExportSource::fromProducer(
            [&imageCalls, image](const ScreenshotExportCancellation&) {
                ++imageCalls;
                return image;
            },
            factory));
        QTemporaryDir directory;
        QObject receiver;
        int callbacks = 0;
        require(artifact.requestAutomaticSave(
                    &receiver, {directory.path()}, ScreenshotImageFileFormat::Webp,
                    QStringLiteral("pixels"),
                    [&](ScreenshotExportTaskResult result) {
                        require(result.succeeded() && QFile::exists(result.savedPath),
                                "non-PNG save failed");
                        ++callbacks;
                    }),
                "non-PNG save request rejected");
        processUntil([&] { return callbacks == 1; });
        require(rowCalls == (rowBacked ? 1 : 0) && imageCalls == (rowBacked ? 0 : 1),
                "non-PNG save performed an extra encoding or materialization");
        require(artifact.requestAutomaticSave(
                    &receiver, {directory.path()}, ScreenshotImageFileFormat::Png,
                    QStringLiteral("bad/name"),
                    [&](ScreenshotExportTaskResult result) {
                        require(!result.succeeded() && !result.error.isEmpty(),
                                "invalid save filename did not report failure");
                        ++callbacks;
                    }),
                "invalid save request must complete with an error");
        processUntil([&] { return callbacks == 2; });
    }
}

void imageRequestsShareOneAsyncLoad() {
    int loadCount = 0;
    std::function<void(QImage)> finishLoad;
    ScreenshotExportArtifact artifact(ScreenshotExportSource::fromImageLoader(
        [&loadCount, &finishLoad](QObject*, std::function<void(QImage)> callback) {
            ++loadCount;
            finishLoad = std::move(callback);
            return true;
        }));
    QObject firstReceiver;
    QObject secondReceiver;
    int callbacks = 0;
    require(artifact.requestImage(&firstReceiver,
                                  [&callbacks](ScreenshotExportImageResult result) {
                                      require(result.succeeded(),
                                              "first shared image request failed");
                                      ++callbacks;
                                  }) &&
                artifact.requestImage(&secondReceiver,
                                      [&callbacks](ScreenshotExportImageResult result) {
                                          require(result.succeeded(),
                                                  "second shared image request failed");
                                          ++callbacks;
                                      }),
            "shared image requests were rejected");
    require(loadCount == 1 && finishLoad, "concurrent image requests started duplicate loads");
    finishLoad(testImage());
    require(callbacks == 2, "shared image result did not fan out to both subscribers");
}

void canonicalEncodingCoalescesAndPreservesBufferIdentity() {
    const QImage image = testImage();
    std::atomic_int rowFactoryCount = 0;
    std::atomic_int imageProducerCount = 0;
    ScreenshotExportArtifact artifact(ScreenshotExportSource::fromProducer(
        [&imageProducerCount, image](const ScreenshotExportCancellation&) {
            ++imageProducerCount;
            return image;
        },
        [&rowFactoryCount, image](std::function<bool()> cancellation) {
            ++rowFactoryCount;
            return rowSourceFor(image, std::move(cancellation));
        }));
    QObject receiver;
    int callbacks = 0;
    const QByteArray* firstBuffer = nullptr;
    const QByteArray* secondBuffer = nullptr;
    require(artifact.requestCanonicalPng(
                &receiver,
                [&callbacks, &firstBuffer](ScreenshotExportEncodingResult result) {
                    require(result.succeeded(), "first canonical encoding failed");
                    firstBuffer = result.image.sharedBytes().get();
                    ++callbacks;
                }) &&
                artifact.requestCanonicalPng(
                    &receiver,
                    [&callbacks, &secondBuffer](ScreenshotExportEncodingResult result) {
                        require(result.succeeded(), "second canonical encoding failed");
                        secondBuffer = result.image.sharedBytes().get();
                        ++callbacks;
                    }),
            "canonical encoding requests were rejected");
    processUntil([&callbacks]() { return callbacks == 2; });
    require(rowFactoryCount == 1, "exact canonical encoding requests were not coalesced");
    require(imageProducerCount == 0,
            "row-backed canonical encoding materialized the image unnecessarily");
    require(firstBuffer != nullptr && firstBuffer == secondBuffer,
            "canonical encoding subscribers did not receive the same immutable buffer");
}

void encodingFailureFansOutOnce() {
    std::atomic_int rowFactoryCount = 0;
    ScreenshotExportArtifact artifact(ScreenshotExportSource::fromProducer(
        {}, [&rowFactoryCount](std::function<bool()> cancellation) {
            ++rowFactoryCount;
            ScreenshotImageRowSource rows;
            rows.size = QSize(16, 8);
            rows.cancellationRequested = std::move(cancellation);
            rows.readRows = [](int, int, qsizetype, uchar*, qsizetype) { return false; };
            return rows;
        }));
    QObject receiver;
    int callbacks = 0;
    const auto completion = [&callbacks](ScreenshotExportEncodingResult result) {
        require(!result.succeeded() && !result.error.isEmpty(),
                "failed encoding was reported as successful");
        ++callbacks;
    };
    require(artifact.requestCanonicalPng(&receiver, completion) &&
                artifact.requestCanonicalPng(&receiver, completion),
            "failed canonical encoding requests were rejected prematurely");
    require(artifact.requestClipboard(&receiver,
                                      [&](ScreenshotExportClipboardResult result) {
                                          require(
                                              !result.succeeded(),
                                              "failed PNG encoding produced a clipboard payload");
                                          ++callbacks;
                                      }),
            "clipboard failure request rejected");
    processUntil([&callbacks]() { return callbacks == 3; });
    require(rowFactoryCount == 1, "encoding failure was recomputed for each subscriber");
}

void cancellationSuppressesPendingCallbacks() {
    std::function<void(QImage)> finishLoad;
    ScreenshotExportArtifact artifact(ScreenshotExportSource::fromImageLoader(
        [&finishLoad](QObject*, std::function<void(QImage)> callback) {
            finishLoad = std::move(callback);
            return true;
        }));
    QObject receiver;
    int callbacks = 0;
    require(artifact.requestImage(&receiver,
                                  [&callbacks](ScreenshotExportImageResult) { ++callbacks; }),
            "cancellation image request was rejected");
    artifact.cancel();
    require(static_cast<bool>(finishLoad),
            "cancellation fixture did not retain its loader callback");
    finishLoad(testImage());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    require(callbacks == 0 && artifact.isCancelled(),
            "cancelled artifact delivered a pending callback");
}

void pinnedViewportSourceRendersExpectedPixels() {
    SnowCanvasRuntime runtime;
    const QByteArray session = runtime.serializeDocumentSession();
    require(!session.isEmpty(), "runtime session could not be serialized");
    QImage background(QSize(64, 48), QImage::Format_ARGB32_Premultiplied);
    background.fill(QColor(12, 24, 36, 255));
    ScreenshotPinnedViewportExportSource source{
        session, std::move(background), QRectF(0.0, 0.0, 64.0, 48.0), QSize(64, 48), {},
    };
    ScreenshotExportArtifact artifact(
        ScreenshotExportSource::fromPinnedViewport(std::move(source)));
    QObject receiver;
    QImage rendered;
    require(artifact.requestImage(&receiver,
                                  [&rendered](ScreenshotExportImageResult result) {
                                      require(result.succeeded(),
                                              "pinned viewport artifact render failed");
                                      rendered = std::move(result.image);
                                  }),
            "pinned viewport artifact render was not scheduled");
    processUntil([&rendered]() { return !rendered.isNull(); });
    require(rendered.size() == QSize(64, 48) &&
                rendered.pixelColor(rendered.width() / 2, rendered.height() / 2) ==
                    QColor(12, 24, 36, 255),
            "pinned viewport artifact did not return the rendered pixels");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        clipboardAndSaveShareCanonicalEncoding();
        nonPngSaveReadsPixelsWithoutEncodingPng();
        imageRequestsShareOneAsyncLoad();
        canonicalEncodingCoalescesAndPreservesBufferIdentity();
        encodingFailureFansOutOnce();
        cancellationSuppressesPendingCallbacks();
        pinnedViewportSourceRendersExpectedPixels();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
