#include "screenshotsaveexportpipeline.h"

#include "snow_shot/presentation/screenshotexportcoordinator.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace pipeline = screenshot_save_export;
using Format = ScreenshotImageFileFormat;

namespace {
struct Scenario final {
    const char* name;
    QSize sourceSize;
    QSize outputSize;
    bool streaming = false;
};

struct RowCounters final {
    std::atomic<qint64> calls{0};
    std::atomic<qint64> rows{0};
    std::atomic<qint64> maximumBatch{0};

    void record(int count) {
        calls.fetch_add(1, std::memory_order_relaxed);
        rows.fetch_add(count, std::memory_order_relaxed);
        qint64 observed = maximumBatch.load(std::memory_order_relaxed);
        while (observed < count &&
               !maximumBatch.compare_exchange_weak(observed, count, std::memory_order_relaxed)) {
        }
    }
};

struct Sample final {
    qint64 provisionalNanoseconds = 0;
    qint64 resizeNanoseconds = 0;
    qint64 pngEncodeNanoseconds = 0;
    qint64 jpegEncodeNanoseconds = 0;
    qint64 decodeNanoseconds = 0;
    qint64 sourceReadCalls = 0;
    qint64 sourceRows = 0;
    qint64 encodeReadCalls = 0;
    qint64 encodeRows = 0;
    qint64 maximumRowBatch = 0;
    qint64 mappedBytes = 0;
    int decodeCount = 0;
    int additionalSourceMaterializations = 0;
};

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

void require(bool condition, const QString& message) {
    if (!condition)
        fail(message);
}

QImage makeImage(QSize size) {
    QImage image(size, QImage::Format_RGBA8888);
    require(!image.isNull(), QStringLiteral("Could not allocate benchmark source image"));
    for (int y = 0; y < size.height(); ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            row[x * 4] = uchar((x + y * 3) & 0xff);
            row[x * 4 + 1] = uchar((x * 5 + y) & 0xff);
            row[x * 4 + 2] = uchar((x * 3 + y * 7) & 0xff);
            row[x * 4 + 3] = 255;
        }
    }
    image.setColorSpace(QColorSpace::SRgb);
    return image;
}

ScreenshotImageRowSource streamingRows(QSize size,
                                       const ScreenshotExportCancellation& cancellation) {
    ScreenshotImageRowSource rows;
    rows.size = size;
    rows.cancellationRequested = [&cancellation] { return cancellation.isCancellationRequested(); };
    rows.readRows = [size, &cancellation](int first, int count, qsizetype stride, uchar* target,
                                          qsizetype capacity) {
        const qsizetype rowBytes = qsizetype(size.width()) * 4;
        if (cancellation.isCancellationRequested() || target == nullptr || first < 0 || count < 1 ||
            first > size.height() || count > size.height() - first || stride < rowBytes ||
            capacity < rowBytes || count - 1 > (capacity - rowBytes) / stride) {
            return false;
        }
        for (int offset = 0; offset < count; ++offset) {
            const int y = first + offset;
            uchar* row = target + qsizetype(offset) * stride;
            for (int x = 0; x < size.width(); ++x) {
                row[x * 4] = uchar((x + y * 3) & 0xff);
                row[x * 4 + 1] = uchar((x * 5 + y) & 0xff);
                row[x * 4 + 2] = uchar((x * 3 + y * 7) & 0xff);
                row[x * 4 + 3] = 255;
            }
        }
        return true;
    };
    return rows;
}

ScreenshotImageRowSource countedRows(ScreenshotImageRowSource rows, RowCounters& counters) {
    const auto readRows = rows.readRows;
    rows.readRows = [readRows, &counters](int first, int count, qsizetype stride, uchar* target,
                                          qsizetype capacity) {
        counters.record(count);
        return readRows(first, count, stride, target, capacity);
    };
    return rows;
}

qint64 elapsed(const std::function<void()>& operation) {
    QElapsedTimer timer;
    timer.start();
    operation();
    return timer.nsecsElapsed();
}

Sample runSample(const Scenario& scenario, const QImage& image,
                 const ScreenshotExportCancellation& cancellation) {
    RowCounters sourceCounters;
    RowCounters encodeCounters;
    ScreenshotImageRowSource rows = scenario.streaming
                                        ? streamingRows(scenario.sourceSize, cancellation)
                                        : snow_shot::image_codec::srgbRowSource(image);
    rows.cancellationRequested = [&cancellation] { return cancellation.isCancellationRequested(); };
    rows = countedRows(std::move(rows), sourceCounters);
    const uchar* canonicalBacking = rows.backingImage.constBits();

    QString error;
    pipeline::Source source;
    Sample sample;
    sample.provisionalNanoseconds =
        elapsed([&] { source = pipeline::prepare(rows, cancellation, &error); });
    require(source.rows.isValid() && !source.preview.isNull(),
            QStringLiteral("Source preparation failed: %1").arg(error));
    if (scenario.streaming) {
        require(source.rows.backingImage.isNull(),
                QStringLiteral("Streaming source was unexpectedly materialized"));
        sample.additionalSourceMaterializations = source.rows.backingImage.isNull() ? 0 : 1;
    } else {
        require(source.rows.backingImage.constBits() == image.constBits(),
                QStringLiteral("Image-backed source did not preserve its canonical backing"));
        sample.additionalSourceMaterializations =
            source.rows.backingImage.constBits() == canonicalBacking ? 0 : 1;
    }

    std::shared_ptr<pipeline::PreparedPixels> pixels;
    sample.resizeNanoseconds = elapsed([&] {
        pixels = pipeline::preparePixels(source, scenario.outputSize, cancellation, &error);
    });
    require(pixels != nullptr && pixels->rows.isValid(),
            QStringLiteral("Pixel preparation failed: %1").arg(error));
    sample.mappedBytes =
        scenario.outputSize == scenario.sourceSize
            ? 0
            : qint64(scenario.outputSize.width()) * scenario.outputSize.height() * 4;
    if (sample.mappedBytes == 0) {
        require(pixels->exactImage.constBits() == source.rows.backingImage.constBits(),
                QStringLiteral("Source-sized pixels did not reuse the canonical backing"));
    } else {
        require(!pixels->exactImage.isNull() &&
                    pixels->exactImage.sizeInBytes() == sample.mappedBytes,
                QStringLiteral("Resized pixels were not retained in one exact mapped raster"));
    }

    pixels->rows = countedRows(std::move(pixels->rows), encodeCounters);
    std::shared_ptr<pipeline::Encoded> png;
    sample.pngEncodeNanoseconds = elapsed([&] {
        png =
            pipeline::render(pixels, {scenario.outputSize, Format::Png, 100}, cancellation, &error);
    });
    require(png != nullptr && png->codecResult.roundTrip == snow::image::PixelRoundTrip::exact,
            QStringLiteral("Exact PNG encoding failed: %1").arg(error));
    require(!png->pixels->exactImage.isNull(),
            QStringLiteral("PNG benchmark unexpectedly requires an encoded-file decode"));

    std::shared_ptr<pipeline::Encoded> jpeg;
    sample.jpegEncodeNanoseconds = elapsed([&] {
        jpeg =
            pipeline::render(pixels, {scenario.outputSize, Format::Jpeg, 85}, cancellation, &error);
    });
    require(jpeg != nullptr &&
                jpeg->codecResult.roundTrip == snow::image::PixelRoundTrip::codec_artifact,
            QStringLiteral("JPEG artifact encoding failed: %1").arg(error));

    QImage decoded;
    sample.decodeNanoseconds = elapsed([&] {
        ++sample.decodeCount;
        decoded = pipeline::decode(*jpeg, cancellation, &error);
    });
    require(!decoded.isNull() && decoded.size() == scenario.outputSize,
            QStringLiteral("JPEG preview decode failed: %1").arg(error));
    require(sample.decodeCount == 1,
            QStringLiteral("Exact and codec-artifact previews used the wrong decode routes"));

    sample.sourceReadCalls = sourceCounters.calls.load(std::memory_order_relaxed);
    sample.sourceRows = sourceCounters.rows.load(std::memory_order_relaxed);
    sample.encodeReadCalls = encodeCounters.calls.load(std::memory_order_relaxed);
    sample.encodeRows = encodeCounters.rows.load(std::memory_order_relaxed);
    sample.maximumRowBatch = std::max(sourceCounters.maximumBatch.load(std::memory_order_relaxed),
                                      encodeCounters.maximumBatch.load(std::memory_order_relaxed));
    require(sample.additionalSourceMaterializations == 0,
            QStringLiteral("The save pipeline materialized an additional full source raster"));
    require(sample.maximumRowBatch < scenario.sourceSize.height() ||
                scenario.sourceSize.height() == 1,
            QStringLiteral("The benchmark observed an unbounded full-source row request"));
    return sample;
}

Sample runOnWorker(const Scenario& scenario, const QImage& image) {
    QObject receiver;
    auto sample = std::make_shared<Sample>();
    bool complete = false;
    QString failure;
    const auto job = ScreenshotExportCoordinator::shared().submit(
        &receiver, ScreenshotExportCoordinator::Priority::Foreground,
        [scenario, image, sample](const ScreenshotExportCancellation& cancellation) {
            *sample = runSample(scenario, image, cancellation);
            return ScreenshotExportTaskResult{};
        },
        [&](ScreenshotExportTaskResult result) {
            failure = std::move(result.error);
            complete = true;
        });
    require(job.isValid(), QStringLiteral("The export benchmark worker queue is full"));
    QElapsedTimer timeout;
    timeout.start();
    while (!complete && timeout.elapsed() < 600'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    require(complete, QStringLiteral("The export benchmark timed out"));
    require(failure.isEmpty(), failure);
    return *sample;
}

qint64 median(std::vector<qint64> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double milliseconds(qint64 nanoseconds) {
    return double(nanoseconds) / 1'000'000.0;
}

void report(const Scenario& scenario, const std::vector<Sample>& samples) {
    const auto values = [&](const auto& member) {
        std::vector<qint64> result;
        result.reserve(samples.size());
        for (const auto& sample : samples)
            result.push_back(member(sample));
        return result;
    };
    std::cout
        << std::left << std::setw(11) << scenario.name << ' ' << scenario.sourceSize.width() << 'x'
        << scenario.sourceSize.height() << " -> " << scenario.outputSize.width() << 'x'
        << scenario.outputSize.height() << std::fixed << std::setprecision(2) << " provisional_ms="
        << milliseconds(
               median(values([](const Sample& value) { return value.provisionalNanoseconds; })))
        << " resize_ms="
        << milliseconds(median(values([](const Sample& value) { return value.resizeNanoseconds; })))
        << " png_encode_ms=" << milliseconds(median(values([](const Sample& value) {
               return value.pngEncodeNanoseconds;
           })))
        << " jpeg_encode_ms=" << milliseconds(median(values([](const Sample& value) {
               return value.jpegEncodeNanoseconds;
           })))
        << " jpeg_decode_ms="
        << milliseconds(median(values([](const Sample& value) { return value.decodeNanoseconds; })))
        << " source_read_calls="
        << median(values([](const Sample& value) { return value.sourceReadCalls; }))
        << " source_rows=" << median(values([](const Sample& value) { return value.sourceRows; }))
        << " encode_read_calls="
        << median(values([](const Sample& value) { return value.encodeReadCalls; }))
        << " encode_rows=" << median(values([](const Sample& value) { return value.encodeRows; }))
        << " max_row_batch="
        << median(values([](const Sample& value) { return value.maximumRowBatch; }))
        << " decode_count="
        << median(values([](const Sample& value) { return qint64(value.decodeCount); }))
        << " mapped_mib="
        << double(median(values([](const Sample& value) { return value.mappedBytes; }))) /
               (1024.0 * 1024.0)
        << " extra_source_materializations=" << median(values([](const Sample& value) {
               return qint64(value.additionalSourceMaterializations);
           }))
        << '\n';
}
} // namespace

int main(int argc, char* argv[]) {
#if defined(_DEBUG)
    std::cerr << "This benchmark must be built in Release mode\n";
    return 2;
#endif
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption samplesOption(QStringLiteral("samples"),
                                     QStringLiteral("Samples per scenario (median is reported)."),
                                     QStringLiteral("count"), QStringLiteral("3"));
    parser.addOption(samplesOption);
    parser.process(application);
    bool validSamples = false;
    const int sampleCount = parser.value(samplesOption).toInt(&validSamples);
    if (!validSamples || sampleCount < 1 || sampleCount > 20) {
        std::cerr << "--samples must be between 1 and 20\n";
        return 2;
    }

    const std::vector<Scenario> scenarios{
        {"1080p", QSize(1920, 1080), QSize(1920, 1080), false},
        {"4k", QSize(3840, 2160), QSize(1920, 1080), false},
        {"8k", QSize(7680, 4320), QSize(3840, 2160), false},
        {"tall", QSize(1440, 12000), QSize(720, 6000), true},
    };

    try {
        std::cout << "Save-to-File pipeline medians, samples=" << sampleCount << '\n';
        for (const auto& scenario : scenarios) {
            const QImage image = scenario.streaming ? QImage{} : makeImage(scenario.sourceSize);
            std::vector<Sample> samples;
            samples.reserve(size_t(sampleCount));
            for (int sample = 0; sample < sampleCount; ++sample)
                samples.push_back(runOnWorker(scenario, image));
            report(scenario, samples);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
