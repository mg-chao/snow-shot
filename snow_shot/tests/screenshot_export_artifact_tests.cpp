#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/presentation/screenshotclipboardpolicy.h"

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

struct ScreenshotClipboardPayloadTestAccess {
    static bool matchesFormat(const ScreenshotClipboardPayload& payload,
                              ScreenshotClipboardFormatMode expected) {
#if defined(Q_OS_WIN) || defined(_WIN32)
        if (payload.m_formatMode != expected || payload.m_nativeHandle == nullptr) {
            return false;
        }
        const auto handle = static_cast<HGLOBAL>(payload.m_nativeHandle);
        const auto* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(handle));
        if (header == nullptr) {
            return false;
        }
        const bool matches = expected == ScreenshotClipboardFormatMode::CompatibleDib
                                 ? header->biSize == sizeof(BITMAPINFOHEADER) &&
                                       header->biHeight > 0 && header->biCompression == BI_RGB
                                 : header->biSize == sizeof(BITMAPV5HEADER) &&
                                       header->biHeight < 0 &&
                                       header->biCompression == BI_BITFIELDS;
        GlobalUnlock(handle);
        return matches;
#else
        Q_UNUSED(expected);
        return payload.isValid();
#endif
    }
};

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

void clipboardRequestsPreserveScenarioFormat() {
    using Scenario = ScreenshotClipboardScenario;
    using Format = ScreenshotClipboardFormatMode;
    struct Case {
        Scenario scenario;
        ScreenshotResultStyle style;
        Format expected;
    };
    const Case cases[] = {
        {Scenario::OrdinarySelection, {}, Format::CompatibleDib},
        {Scenario::OrdinarySelection, {8, 0, QColor()}, Format::DibV5},
        {Scenario::ScrollingCapture, {}, Format::CompatibleDib},
        {Scenario::CurrentMonitor, {}, Format::CompatibleDib},
        {Scenario::Other, {}, Format::DibV5},
    };
    const QImage image = testImage();
    for (bool rowBacked : {false, true}) {
        std::atomic_int materializations = 0;
        ScreenshotExportArtifact artifact(
            rowBacked ? ScreenshotExportSource::fromProducer(
                            [&materializations, image](const ScreenshotExportCancellation&) {
                                ++materializations;
                                return image;
                            },
                            [image](std::function<bool()> cancellation) {
                                return rowSourceFor(image, std::move(cancellation));
                            })
                      : ScreenshotExportSource::fromImage(image));
        QObject receiver;
        int callbacks = 0;
        for (const auto& test : cases) {
            require(
                artifact.requestClipboard(
                    &receiver,
                    ScreenshotClipboardPolicy::formatForScenario(test.scenario, test.style),
                    [&callbacks, expected = test.expected](ScreenshotExportClipboardResult result) {
                        require(result.succeeded(), "artifact clipboard preparation failed");
                        require(ScreenshotClipboardPayloadTestAccess::matchesFormat(result.payload,
                                                                                    expected),
                                "artifact changed the requested clipboard format or header");
                        ++callbacks;
                    }),
                "artifact clipboard request was rejected");
        }
        processUntil([&callbacks]() { return callbacks == 5; });
        require(materializations == 0, "row-backed clipboard preparation materialized an image");
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
    processUntil([&callbacks]() { return callbacks == 2; });
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
        clipboardRequestsPreserveScenarioFormat();
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
