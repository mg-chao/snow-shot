#include "presentation/capture/screenshotscrollingpipeline.h"
#include "snow_shot/presentation/screenshotimagefileservice.h"
#include "snow_shot/presentation/screenshotscrollingthumbnailwidget.h"
#include "snowimageqtcodec.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

namespace {
using namespace snow_shot::capture_detail;

void require(bool condition, const char* message) {
    if (!condition)
        qFatal("%s", message);
}

// Release the next input after the previous thumbnail has actually been painted.
// This exercises the production mailbox without letting it discard corpus frames.
struct Input {
    std::mutex mutex;
    std::condition_variable wake;
    QImage next;
    bool stopped = false;

    void publish(const QImage& frame) {
        const std::lock_guard lock(mutex);
        next = frame;
        wake.notify_one();
    }
};

class Source final : public ScrollingFrameSource {
  public:
    explicit Source(std::shared_ptr<Input> input) : m_input(std::move(input)) {}

    ScrollingSourceEvent receive(int timeoutMilliseconds) override {
        std::unique_lock lock(m_input->mutex);
        m_input->wake.wait_for(lock, std::chrono::milliseconds(timeoutMilliseconds),
                               [&] { return m_input->stopped || !m_input->next.isNull(); });
        if (m_input->next.isNull())
            return {};
        ScrollingSourceEvent event;
        event.kind = ScrollingSourceEvent::Kind::Frame;
        event.frame.image = std::move(m_input->next);
        return event;
    }
    void setTargetFps(int) override {}
    ScrollingSourceStats stats() const override {
        return {};
    }
    void stop() override {
        const std::lock_guard lock(m_input->mutex);
        m_input->stopped = true;
        m_input->wake.notify_one();
    }

  private:
    std::shared_ptr<Input> m_input;
};

void compareRows(const QImage& actual, int first, const QImage& expected, int start, int count) {
    require(actual.width() == expected.width() && first >= 0 && start >= 0 && count >= 0 &&
                count <= actual.height() - first && count <= expected.height() - start,
            "Invalid image comparison range.");
    const auto bytes = static_cast<std::size_t>(actual.width()) * 4;
    for (int row = 0; row < count; ++row) {
        if (std::memcmp(actual.constScanLine(first + row), expected.constScanLine(start + row),
                        bytes) != 0)
            qFatal("Pixel mismatch at input row %d, stitched row %d.", first + row, start + row);
    }
}

struct Slice {
    QImage frame;
    int offset;
};

std::vector<Slice> loadSequence(const QDir& directory, const QImage& stitched) {
    QFile file(directory.filePath(QStringLiteral("steps.json")));
    require(file.open(QIODevice::ReadOnly), "Could not open steps.json.");
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    const auto slices = manifest.value(QStringLiteral("slices")).toArray();
    const auto files = directory.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    const auto fixed = manifest.value(QStringLiteral("fixed")).toObject();
    const int top = fixed.value(QStringLiteral("top")).toInt(-1);
    const int bottom = fixed.value(QStringLiteral("bottom")).toInt(-1);
    const int content = manifest.value(QStringLiteral("content_slice_height")).toInt();
    const int viewport = manifest.value(QStringLiteral("output_height")).toInt();
    require(!files.isEmpty() && files.size() == slices.size() &&
                files.size() == manifest.value(QStringLiteral("total_slices")).toInteger(-1) &&
                top >= 0 && bottom >= 0 && content > 0 &&
                qint64(top) + content + bottom == viewport &&
                stitched.width() == manifest.value(QStringLiteral("source_width")).toInt() &&
                stitched.height() == manifest.value(QStringLiteral("source_height")).toInt(),
            "Manifest, PNG list, or stitched dimensions disagree.");
    std::vector<Slice> result;
    for (qsizetype i = 0; i < files.size(); ++i) {
        const auto slice = slices.at(i).toObject();
        require(files[i] == slice.value(QStringLiteral("file")).toString(),
                "Unexpected frame order.");
        QImage frame =
            QImage(directory.filePath(files[i])).convertToFormat(QImage::Format_RGBA8888);
        const int offset = slice.value(QStringLiteral("offset_y")).toInt(-1);
        require(!frame.isNull() && frame.height() == viewport &&
                    qint64(offset) + viewport <= stitched.height() &&
                    (i == 0 ? offset == 0
                            : offset > result.back().offset &&
                                  offset - result.back().offset <= content &&
                                  offset - result.back().offset ==
                                      slice.value(QStringLiteral("step_from_previous")).toInt()) &&
                    qint64(top) + offset == slice.value(QStringLiteral("source_y")).toInteger(-1),
                "Invalid frame displacement or uncovered rows.");
        compareRows(frame, 0, stitched, 0, top);
        compareRows(frame, top, stitched, slice.value(QStringLiteral("source_y")).toInt(), content);
        compareRows(frame, frame.height() - bottom, stitched, stitched.height() - bottom, bottom);
        result.push_back({std::move(frame), offset});
    }
    require(qint64(result.back().offset) + result.back().frame.height() == stitched.height(),
            "Last frame does not cover the bottom of the export.");
    return result;
}

void savePng(const ScreenshotScrollingSnapshot& snapshot, const QString& path) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "Could not open the PNG output.");
    const auto options =
        ScreenshotImageFileService::encodeOptions(ScreenshotImageFileFormat::Png, {});
    QString error;
    if (!snow_shot::image_codec::encodeToDevice(snapshot.rowSource(), &file,
                                                snow::image::Format::png, options, &error))
        qFatal("%s", qPrintable(error));
    require(file.commit(), "Could not commit the PNG output.");
}

QJsonObject replay(const std::vector<Slice>& slices, const QImage& expected,
                   const QString& pngPath) {
    auto input = std::make_shared<Input>();
    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen);
    ScreenshotScrollingThumbnailWidget thumbnail(host);
    thumbnail.setMaximumPreviewExtent(640);
    QImage painted, output;
    QEventLoop loop;
    std::size_t index = 0;
    qint64 paintedAt = 0;
    qint64 exportedAt = 0;
    std::unique_ptr<ScreenshotScrollingPipeline> pipeline;
    pipeline = std::make_unique<ScreenshotScrollingPipeline>(
        [&](ScrollingPipelineFrame frame) {
            const int height = slices.front().frame.height() + slices.at(index).offset;
            require(frame.changed && !frame.fatalError &&
                        frame.sourceSize == QSize(expected.width(), height),
                    "Pipeline displacement differs from steps.json.");
            thumbnail.setStitchedImage(frame.previewImage, frame.sourceSize, frame.change,
                                       frame.addedRows, frame.previewReplaced,
                                       frame.replacedPreviewRows);
            if (painted.size() != thumbnail.size())
                painted = QImage(thumbnail.size(), QImage::Format_ARGB32_Premultiplied);
            painted.fill(Qt::transparent);
            thumbnail.render(&painted);
            if (++index < slices.size()) {
                input->publish(slices[index].frame);
                return;
            }
            paintedAt = scrolling_perf::now();
            const auto finish = [&](ScreenshotScrollingSnapshot snapshot) {
                if (pngPath.isEmpty())
                    output = snapshot.materialize([] { return false; });
                else
                    savePng(snapshot, pngPath);
                exportedAt = scrolling_perf::now();
                loop.quit();
            };
            require(pipeline->requestSnapshot(0, height, &loop, finish),
                    "Could not request the export snapshot.");
        },
        [](quint64, const QString& error) { qFatal("%s", qPrintable(error)); });
    QTimer::singleShot(std::chrono::minutes(2), &loop, [] { qFatal("Corpus replay timed out."); });
    const auto start = scrolling_perf::now();
    input->publish(slices.front().frame);
    pipeline->begin(1, slices.front().frame.size(), ScreenshotScrollingRecognitionMode::Vertical,
                    [input] { return std::make_unique<Source>(input); });
    loop.exec();
    if (!pngPath.isEmpty())
        output = QImage(pngPath).convertToFormat(QImage::Format_RGBA8888);
    require(index == slices.size() && output.size() == expected.size() &&
                output.format() == expected.format(),
            "Incomplete pipeline export.");
    compareRows(output, 0, expected, 0, expected.height());
    const auto preview = thumbnail.previewImageForTesting();
    require(!preview.isNull(), "No thumbnail was produced.");
    const auto hash = QCryptographicHash::hash(
        {reinterpret_cast<const char*>(preview.constBits()), preview.sizeInBytes()},
        QCryptographicHash::Sha256);
    return {{QStringLiteral("paint_ns"), paintedAt - start},
            {QStringLiteral("export_ns"), exportedAt - start},
            {QStringLiteral("preview_sha256"), QString::fromLatin1(hash.toHex())}};
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const bool png = argc == 5 && QString::fromLocal8Bit(argv[4]) == QStringLiteral("--png");
    require(argc == 4 || png,
            "Usage: benchmark FRAME_DIRECTORY CORPUS_STITCHED_PNG SAMPLES [--png]");
    require(QStringLiteral(SNOW_SCROLLING_BUILD_CONFIG) == QStringLiteral("Release"),
            "Requires a performance Release build.");
    const int samples = QString::fromLocal8Bit(argv[3]).toInt();
    require(samples > 0, "Invalid sample count.");
    QImageReader::setAllocationLimit(std::max(1024, QImageReader::allocationLimit()));
    const auto expected =
        QImage(QString::fromLocal8Bit(argv[2])).convertToFormat(QImage::Format_RGBA8888);
    require(!expected.isNull(), "Could not load the stitched reference.");
    const auto slices = loadSequence(QDir(QString::fromLocal8Bit(argv[1])), expected);
    QTemporaryDir temp;
    require(!png || temp.isValid(), "Could not create the PNG output directory.");
    const QString pngPath = png ? temp.filePath(QStringLiteral("stitched.png")) : QString();
    QJsonArray timings;
    for (int sample = 0; sample <= samples; ++sample) {
        const auto result = replay(slices, expected, pngPath);
        ScreenshotScrollingPipeline::waitForShutdowns();
        if (sample > 0)
            timings.append(result);
    }
    std::cout << QJsonDocument(
                     QJsonObject{{QStringLiteral("export"),
                                  png ? QStringLiteral("png") : QStringLiteral("raster")},
                                 {QStringLiteral("frames"), static_cast<qint64>(slices.size())},
                                 {QStringLiteral("samples"), timings}})
                     .toJson()
                     .constData();
}
