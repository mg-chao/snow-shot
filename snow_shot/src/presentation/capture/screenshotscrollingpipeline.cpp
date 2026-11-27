#include "snow_shot/runtime/runtimeactivitytracker.h"
#include "snow_draw_engine_qt/snow_canvas_image.h"
#include "snow_shot/platform/applicationqos.h"
#include "screenshotscrollingpipeline.h"
#include "screenshotscrollingdiagnostics.h"
#include "latestframemailbox.h"
#include "../pinned/screenshotpintoperfinstrumentation.h"
#include "snow_stitch_images.h"
#include "../../image/snowimageopacity.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QColorSpace>

#ifdef Q_OS_MACOS
#include <pthread/qos.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

Q_LOGGING_CATEGORY(snowShotScrollingCaptureLog, "snowshot.capture.scrolling", QtWarningMsg)

namespace snow_shot::capture_detail {
namespace {
using AdaptiveScrollCadence = AdaptiveScrollingCaptureCadence;
using ScrollWorkerFrame = ScrollingPipelineFrame;
#if defined(SNOW_SHOT_SCROLLING_PERF_DETAIL)
static_assert(scrolling_perf::rustStageNames.size() == SNOW_STITCH_PERF_STAGE_COUNT);
class PerfResultReader final {
  public:
    explicit PerfResultReader(scrolling_perf::FrameTrace trace) : m_trace(std::move(trace)) {}
    ~PerfResultReader() {
        SnowStitchPerfSnapshot snapshot{};
        if (m_trace && snow_stitch_perf_read_thread(&snapshot, sizeof(snapshot)) != 0) {
            std::copy(std::begin(snapshot.elapsed_ns), std::end(snapshot.elapsed_ns),
                      m_trace->rustElapsed.begin());
            std::copy(std::begin(snapshot.calls), std::end(snapshot.calls),
                      m_trace->rustCalls.begin());
        }
    }

  private:
    scrolling_perf::FrameTrace m_trace;
};
#endif
struct ScrollPreviewLayout {
    int targetHeight = 0;
    int patchHeight = 0;
    int replacedRows = 0;
    bool replaced = false;
    bool valid = false;
};

struct ScrollCaptureResult {
    quint64 generation = 0;
    bool wakeConsumer = false;
    bool streamPressure = false;
    bool fatalError = false;
    QString errorMessage;
};

class OwnedScrollFrame {
  public:
    OwnedScrollFrame() = default;
    explicit OwnedScrollFrame(QImage imageValue) : image(std::move(imageValue)) {}
    ~OwnedScrollFrame() {
        discard("mailbox_cancelled");
    }
    OwnedScrollFrame(const OwnedScrollFrame&) = delete;
    OwnedScrollFrame& operator=(const OwnedScrollFrame&) = delete;
    OwnedScrollFrame(OwnedScrollFrame&& other) noexcept
        : trace(std::move(other.trace)), image(std::exchange(other.image, {})) {}
    OwnedScrollFrame& operator=(OwnedScrollFrame&& other) noexcept {
        if (this != &other) {
            discard("mailbox_cancelled");
            trace = std::move(other.trace);
            image = std::exchange(other.image, {});
        }
        return *this;
    }
    scrolling_perf::FrameTrace trace;

    QImage release() {
        return std::exchange(image, {});
    }

    void discard(const char* disposition) {
        Q_UNUSED(disposition);
        if (!image.isNull()) {
            SNOW_SCROLL_TRACE(trace, trace->disposition = disposition);
            image = {};
        }
    }

  private:
    QImage image;
};

using ScrollFrameMailbox = snow_shot::capture_detail::LatestFrameMailbox<OwnedScrollFrame, quint64>;

// Native cleanup never owns GUI resources captured by completion. The receiver owns this
// object, so cancellation during application teardown also releases those resources on the
// receiver's thread.
class ScrollingShutdownCompletion final : public QObject {
  public:
    ScrollingShutdownCompletion(QObject* receiver, std::function<void()> completed)
        : QObject(receiver),
          m_completed(snow_shot::runtime::trackRuntimeWork(std::move(completed))) {}
    void complete() {
        auto completed = std::exchange(m_completed, {});
        deleteLater();
        completed();
    }

  private:
    std::function<void()> m_completed;
};

class ScrollingShutdownTasks final {
  public:
    static ScrollingShutdownTasks& shared() {
        // Application teardown explicitly joins these tasks before its services and Qt
        // disappear. The admission registry itself remains valid through static teardown.
        static auto* tasks = new ScrollingShutdownTasks;
        return *tasks;
    }

    template <typename Work> void submit(Work work) {
        std::vector<std::thread> finished;
        const std::lock_guard lock(m_mutex);
        for (auto entry = m_threads.begin(); entry != m_threads.end();) {
            if (entry->finished->load()) {
                finished.push_back(std::move(entry->thread));
                entry = m_threads.erase(entry);
            } else {
                ++entry;
            }
        }
        auto done = std::make_shared<std::atomic_bool>(false);
        std::thread thread(
            [work = std::move(work), done, finished = std::move(finished)]() mutable {
                // Reap previous finished threads here, keeping every join off ordinary UI stop.
                for (auto& previous : finished)
                    previous.join();
                {
                    auto execute = std::move(work);
                    execute();
                }
                // The invocation and its runtime activity reservation are already destroyed.
                // A terminal join also waits for the cleanup thread's Qt/TLS destruction.
                done->store(true);
            });
        m_threads.push_back({std::move(thread), std::move(done)});
    }

    void wait() {
        for (;;) {
            std::vector<Entry> threads;
            {
                const std::lock_guard lock(m_mutex);
                threads = std::exchange(m_threads, {});
            }
            if (threads.empty())
                return;
            for (auto& entry : threads)
                entry.thread.join();
        }
    }

  private:
    struct Entry {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> finished;
    };
    std::mutex m_mutex;
    std::vector<Entry> m_threads;
};

void releaseScrollSourceImage(void* image) {
    delete static_cast<QImage*>(image);
}

void releaseStitchOwnedImage(void* image) {
    snow_stitch_owned_image_destroy(static_cast<SnowStitchOwnedImage*>(image));
}

const char* unmatchedReasonName(SnowStitchUnmatchedReason reason) {
    switch (reason) {
    case SNOW_STITCH_UNMATCHED_REASON_INSUFFICIENT_OVERLAP:
        return "insufficient-overlap";
    case SNOW_STITCH_UNMATCHED_REASON_LOW_INFORMATION:
        return "low-information";
    case SNOW_STITCH_UNMATCHED_REASON_AMBIGUOUS:
        return "ambiguous";
    case SNOW_STITCH_UNMATCHED_REASON_CONFLICTING_REFERENCES:
        return "conflicting-references";
    case SNOW_STITCH_UNMATCHED_REASON_VERIFICATION_FAILED:
        return "verification-failed";
    case SNOW_STITCH_UNMATCHED_REASON_NONE:
    default:
        return "none";
    }
}

class ScreenshotScrollingCaptureWorker final : public QObject {
  public:
    ~ScreenshotScrollingCaptureWorker() override {
        if (m_stitchSession != nullptr) {
            snow_stitch_session_destroy(m_stitchSession);
        }
    }

    void begin(quint64 generation, ScreenshotScrollingRecognitionMode mode) {
        m_generation = generation;
        if (m_mode != mode && m_stitchSession != nullptr) {
            snow_stitch_session_destroy(m_stitchSession);
            m_stitchSession = nullptr;
        }
        m_mode = mode;
        resetPreview();
        if (!ensureStitchSession()) {
            return;
        }
        static_cast<void>(snow_stitch_session_reset(m_stitchSession));
    }

    void reset(quint64 generation) {
        m_generation = generation;
        resetPreview();
        if (m_stitchSession != nullptr) {
            static_cast<void>(snow_stitch_session_reset(m_stitchSession));
        }
    }

    ScrollWorkerFrame process(quint64 generation, QImage sourceImage,
                              scrolling_perf::FrameTrace trace) {
        ScrollWorkerFrame result;
        result.generation = generation;
        result.trace = std::move(trace);
#if defined(SNOW_SHOT_SCROLLING_PERF_DETAIL)
        snow_stitch_perf_reset_thread();
        PerfResultReader perfReader(result.trace);
#endif
        if (generation != m_generation || sourceImage.isNull() || !ensureStitchSession()) {
            return result;
        }

        SnowStitchFrameOutcome outcome{};
        int pushed = 0;
        {
            SNOW_SCROLL_SCOPE(result.trace, Stitch);
            // The native frame remains immutable until the stitcher's last reference releases
            // this QImage. Mailbox replacements never copy pixels or reach the stitcher.
            auto input = std::make_unique<QImage>(std::move(sourceImage));
            const auto* pixels = input->constBits();
            const auto bytes = static_cast<std::size_t>(input->sizeInBytes());
            const auto width = static_cast<std::uint32_t>(input->width());
            const auto height = static_cast<std::uint32_t>(input->height());
            pushed = snow_stitch_session_push_external_rgba(m_stitchSession, width, height, pixels,
                                                            bytes, &releaseScrollSourceImage,
                                                            input.release(), &outcome);
        }
        if (pushed == 0) {
            qWarning("Scrolling screenshot stitching failed: %s", snow_stitch_last_error_message());
            result.fatalError = true;
            return result;
        }

        result.event = outcome.event;
        result.sourceSize =
            QSize(static_cast<int>(outcome.output_width), static_cast<int>(outcome.output_height));
        result.addedRows = static_cast<int>(outcome.added_rows);
        result.changed = outcome.event == SNOW_STITCH_FRAME_EVENT_INITIAL ||
                         outcome.event == SNOW_STITCH_FRAME_EVENT_EXTENDED_BOTTOM ||
                         outcome.event == SNOW_STITCH_FRAME_EVENT_EXTENDED_TOP ||
                         outcome.event == SNOW_STITCH_FRAME_EVENT_EXTENDED_LEFT ||
                         outcome.event == SNOW_STITCH_FRAME_EVENT_EXTENDED_RIGHT;
        if (outcome.event == SNOW_STITCH_FRAME_EVENT_UNMATCHED) {
            qCDebug(snowShotScrollingCaptureLog,
                    "Scrolling screenshot frame was not verified: reason=%s "
                    "score=%.3f second=%.3f content=%.3f "
                    "inliers=%.3f features=%u references=%u",
                    unmatchedReasonName(outcome.unmatched_reason),
                    static_cast<double>(outcome.metrics.score),
                    static_cast<double>(outcome.metrics.second_score),
                    static_cast<double>(outcome.metrics.content_coverage),
                    static_cast<double>(outcome.metrics.inlier_ratio),
                    outcome.metrics.feature_support, outcome.metrics.reference_count);
        }
        if (!result.changed) {
            return result;
        }

        const std::uint32_t outputExtent = m_mode == ScreenshotScrollingRecognitionMode::Horizontal
                                               ? outcome.output_width
                                               : outcome.output_height;
        if (outcome.output_width == 0 || outcome.output_height == 0 || outcome.delta_rows == 0 ||
            outcome.delta_rows > outputExtent ||
            outcome.delta_top > outputExtent - outcome.delta_rows) {
            qWarning("Failed to read scrolling screenshot output: %s",
                     snow_stitch_last_error_message());
            result.changed = false;
            result.fatalError = true;
            return result;
        }

        m_lastOutputSize = result.sourceSize;
        ScrollPreviewLayout preview;
        {
            SNOW_SCROLL_SCOPE(result.trace, PreviewLayout);
            preview = previewLayout(static_cast<int>(outcome.delta_rows), result.sourceSize,
                                    outcome.event, result.addedRows);
        }
        if (!preview.valid) {
            result.changed = false;
            result.fatalError = true;
            return result;
        }
        result.previewImage = renderPreviewPatch(static_cast<int>(outcome.delta_top),
                                                 static_cast<int>(outcome.delta_rows),
                                                 preview.patchHeight, result.trace);
        if (result.previewImage.isNull()) {
            qWarning("Failed to render scrolling screenshot preview: %s",
                     snow_stitch_last_error_message());
            result.changed = false;
            result.fatalError = true;
            return result;
        }
        m_emittedPreviewHeight = preview.targetHeight;
        result.replacedPreviewRows = preview.replacedRows;
        result.previewReplaced = preview.replaced;
        return result;
    }

    ScreenshotScrollingSnapshot trimmedSnapshot(int trimTop, int trimBottom) const {
        if (m_stitchSession == nullptr || m_lastOutputSize.isEmpty()) {
            return {};
        }
        // The stitch-session finalize is the scrolling screenshot's export
        // work; the pin trace records it while a scrolling pin sample waits.
        SNOW_SHOT_PIN_PERF_SCOPE("export.scrolling_trimmed_snapshot");
        const int sourceExtent = m_mode == ScreenshotScrollingRecognitionMode::Horizontal
                                     ? m_lastOutputSize.width()
                                     : m_lastOutputSize.height();
        const int top = std::clamp(trimTop, 0, sourceExtent - 1);
        const int bottom = std::clamp(trimBottom, top + 1, sourceExtent);
        SnowStitchSnapshot* snapshot = snow_stitch_session_snapshot_axis(
            m_stitchSession, static_cast<std::uint32_t>(top), static_cast<std::uint32_t>(bottom));
        if (snapshot == nullptr) {
            return {};
        }
        SnowStitchImageInfo info{};
        if (snow_stitch_snapshot_info(snapshot, &info) == 0 || info.width == 0 ||
            info.height == 0 ||
            info.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            info.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            snow_stitch_snapshot_destroy(snapshot);
            return {};
        }
        return ScreenshotScrollingSnapshot::adoptNative(
            snapshot, QSize(static_cast<int>(info.width), static_cast<int>(info.height)));
    }

    QImage viewportPreview(quint64 generation, int start, int end) const {
        if (generation != m_generation || m_stitchSession == nullptr ||
            m_lastOutputSize.isEmpty() || end <= start ||
            static_cast<qint64>(end) - start > std::numeric_limits<int>::max()) {
            return {};
        }
        const bool horizontal = m_mode == ScreenshotScrollingRecognitionMode::Horizontal;
        const int extent = horizontal ? m_lastOutputSize.width() : m_lastOutputSize.height();
        const QSize size = horizontal ? QSize(end - start, m_lastOutputSize.height())
                                      : QSize(m_lastOutputSize.width(), end - start);
        const int sourceStart = std::clamp(start, 0, extent);
        const int sourceEnd = std::clamp(end, 0, extent);
        QImage padded;
        if (sourceStart != start || sourceEnd != end) {
            padded = snowCanvasAllocateImage(size, QImage::Format_RGBA8888);
            if (padded.isNull())
                return {};
            padded.fill(Qt::black);
        }
        if (sourceStart == sourceEnd)
            return padded;
        const QSize sourceSize = horizontal ? QSize(sourceEnd - sourceStart, size.height())
                                            : QSize(size.width(), sourceEnd - sourceStart);
        SnowStitchOwnedImage* image = snow_stitch_session_materialize_axis(
            m_stitchSession, static_cast<std::uint32_t>(sourceStart),
            static_cast<std::uint32_t>(sourceEnd));
        if (image == nullptr) {
            return {};
        }
        SnowStitchImageInfo info{};
        if (snow_stitch_owned_image_info(image, &info) == 0 || info.rgba_bytes == nullptr ||
            info.width != static_cast<std::uint32_t>(sourceSize.width()) ||
            info.height != static_cast<std::uint32_t>(sourceSize.height()) ||
            info.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 4) ||
            info.stride_bytes != info.width * 4) {
            snow_stitch_owned_image_destroy(image);
            return {};
        }
        QImage result(info.rgba_bytes, sourceSize.width(), sourceSize.height(),
                      static_cast<int>(info.stride_bytes), QImage::Format_RGBA8888,
                      &releaseStitchOwnedImage, image);
        if (result.isNull()) {
            snow_stitch_owned_image_destroy(image);
            return {};
        }
        if (padded.isNull())
            return result;
        const int offset = sourceStart - start;
        for (int row = 0; row < result.height(); ++row) {
            auto* destination = horizontal
                                    ? padded.scanLine(row) + static_cast<qsizetype>(offset) * 4
                                    : padded.scanLine(row + offset);
            std::memcpy(destination, result.constScanLine(row),
                        static_cast<size_t>(result.width()) * 4);
        }
        return padded;
    }

  private:
    void resetPreview() {
        m_emittedPreviewHeight = 0;
        m_lastOutputSize = {};
    }

    ScrollPreviewLayout previewLayout(int deltaRows, const QSize& outputSize,
                                      SnowStitchFrameEvent event, int addedRows) const {
        if (deltaRows <= 0 || outputSize.isEmpty()) {
            return {};
        }
        constexpr int previewCrossExtent = 128;
        const int outputCrossExtent = m_mode == ScreenshotScrollingRecognitionMode::Horizontal
                                          ? outputSize.height()
                                          : outputSize.width();
        const int outputExtent = m_mode == ScreenshotScrollingRecognitionMode::Horizontal
                                     ? outputSize.width()
                                     : outputSize.height();
        const qreal scale =
            static_cast<qreal>(previewCrossExtent) / static_cast<qreal>(outputCrossExtent);
        const int targetHeight = std::max(1, qCeil(static_cast<qreal>(outputExtent) * scale));
        if (event == SNOW_STITCH_FRAME_EVENT_INITIAL || m_emittedPreviewHeight == 0) {
            return {targetHeight, targetHeight, 0, true, true};
        }

        const bool append = event == SNOW_STITCH_FRAME_EVENT_EXTENDED_BOTTOM ||
                            event == SNOW_STITCH_FRAME_EVENT_EXTENDED_RIGHT;
        const bool prepend = event == SNOW_STITCH_FRAME_EVENT_EXTENDED_TOP ||
                             event == SNOW_STITCH_FRAME_EVENT_EXTENDED_LEFT;
        if (!append && !prepend) {
            return {};
        }

        // Refresh the splice overlap and absorb all scale rounding into this
        // small edge patch so the retained preview tiles never drift in height.
        const int overlapSourceRows = std::max(0, deltaRows - std::max(0, addedRows));
        int replacedRows = 0;
        if (overlapSourceRows > 0) {
            replacedRows = std::clamp(qRound(static_cast<qreal>(overlapSourceRows) * scale), 1,
                                      m_emittedPreviewHeight);
        }
        const int patchHeight = targetHeight - (m_emittedPreviewHeight - replacedRows);
        if (patchHeight <= 0) {
            return {};
        }
        return {targetHeight, patchHeight, replacedRows, false, true};
    }

    QImage renderPreviewPatch(int top, int rows, int targetHeight,
                              scrolling_perf::FrameTrace trace) const {
        if (m_stitchSession == nullptr || top < 0 || rows <= 0 || targetHeight <= 0) {
            return {};
        }
        constexpr std::uint32_t previewCrossExtent = 128;
        const std::uint32_t targetWidth = m_mode == ScreenshotScrollingRecognitionMode::Horizontal
                                              ? static_cast<std::uint32_t>(targetHeight)
                                              : previewCrossExtent;
        const std::uint32_t targetImageHeight =
            m_mode == ScreenshotScrollingRecognitionMode::Horizontal
                ? previewCrossExtent
                : static_cast<std::uint32_t>(targetHeight);
        SnowStitchOwnedImage* image = nullptr;
        {
            SNOW_SCROLL_SCOPE(trace, PreviewRender);
            image = snow_stitch_session_render_scaled_axis(
                m_stitchSession, static_cast<std::uint32_t>(top), static_cast<std::uint32_t>(rows),
                targetWidth, targetImageHeight);
        }
        SNOW_SCROLL_SCOPE(trace, PreviewWrap);
        Q_UNUSED(trace);
        if (image == nullptr) {
            return {};
        }
        SnowStitchImageInfo info{};
        if (snow_stitch_owned_image_info(image, &info) == 0 || info.rgba_bytes == nullptr ||
            info.width != targetWidth || info.height != targetImageHeight ||
            info.stride_bytes != info.width * 4) {
            snow_stitch_owned_image_destroy(image);
            return {};
        }
        QImage preview(info.rgba_bytes, static_cast<int>(info.width), static_cast<int>(info.height),
                       static_cast<int>(info.stride_bytes), QImage::Format_RGBA8888,
                       &releaseStitchOwnedImage, image);
        if (preview.isNull()) {
            snow_stitch_owned_image_destroy(image);
        }
        return preview;
    }

    bool ensureStitchSession() {
        if (m_stitchSession != nullptr) {
            return true;
        }
        SnowStitchConfig config{};
        if (snow_stitch_config_default(&config) == 0) {
            qWarning("Failed to initialize scrolling stitch config: %s",
                     snow_stitch_last_error_message());
            return false;
        }
        config.axis = m_mode == ScreenshotScrollingRecognitionMode::Horizontal
                          ? SNOW_STITCH_AXIS_HORIZONTAL
                          : SNOW_STITCH_AXIS_VERTICAL;
        m_stitchSession = snow_stitch_session_create(&config);
        if (m_stitchSession == nullptr) {
            qWarning("Failed to create scrolling stitch session: %s",
                     snow_stitch_last_error_message());
        }
        return m_stitchSession != nullptr;
    }

    SnowStitchSession* m_stitchSession = nullptr;
    QSize m_lastOutputSize;
    quint64 m_generation = 0;
    int m_emittedPreviewHeight = 0;
    ScreenshotScrollingRecognitionMode m_mode = ScreenshotScrollingRecognitionMode::Vertical;
};

class ScreenshotScrollingCaptureProducer final : public QObject {
  public:
    using Callback = std::function<void(ScrollCaptureResult)>;
    ScreenshotScrollingCaptureProducer(std::shared_ptr<ScrollFrameMailbox> mailbox,
                                       Callback callback)
        : m_mailbox(std::move(mailbox)), m_callback(std::move(callback)) {}
    ~ScreenshotScrollingCaptureProducer() override {
        reset(m_generation);
    }

    void begin(quint64 generation, QSize viewport, ScrollingSourceFactory factory,
               AdaptiveScrollCadence::Config config) {
        reset(generation);
        m_viewport = viewport;
        m_cadence = AdaptiveScrollCadence(config);
        logScrollingEvent("scrolling.source_initializing", generation,
                          {{QStringLiteral("width"), viewport.width()},
                           {QStringLiteral("height"), viewport.height()}});
        m_diagnostics = {};
        m_sourceActivity = snow_shot::runtime::RuntimeActivityTracker::shared().acquire();
        m_source = factory();
        if (!m_source) {
            m_sourceActivity = {};
            m_callback({generation, false, false, true,
                        QStringLiteral("could not initialize scrolling frame source")});
            return;
        }
        logScrollingEvent("scrolling.source_ready", generation, m_diagnostics.fields());
        m_active.store(true);
        m_consumer = std::thread(snow_shot::runtime::trackRuntimeWork([this, generation]() {
            snow_shot::platform::applyApplicationQoSToCurrentThread();
            consume(generation);
        }));
    }
    void reset(quint64 generation) {
        m_active.store(false);
        if (m_source)
            m_source->stop();
        if (m_consumer.joinable())
            m_consumer.join();
        m_source.reset();
        m_sourceActivity = {};
        m_generation = generation;
    }
    void pause(quint64 generation) {
        if (m_generation == generation)
            reset(generation);
    }
    void recordStitch(quint64 generation, ScrollClock::duration duration) {
        if (!m_active.load() || generation != m_generation)
            return;
        m_cadence.recordStitch(duration);
        const auto stats = m_source->stats();
        if (stats.captureLatencyNs) {
            m_cadence.recordCapture(std::chrono::nanoseconds(stats.captureLatencyNs));
        }
        m_cadence.recordStreamPressure(stats.bufferedFrames, stats.droppedFrames);
        m_source->setTargetFps(static_cast<int>(std::lround(m_cadence.fps())));
    }
    void recordStreamPressure(quint64 generation) {
        if (!m_active.load() || generation != m_generation)
            return;
        const auto stats = m_source->stats();
        m_cadence.recordStreamPressure(std::max(2U, stats.bufferedFrames), stats.droppedFrames);
        m_source->setTargetFps(static_cast<int>(std::lround(m_cadence.fps())));
    }

  private:
    void consume(quint64 generation) {
        while (m_active.load()) {
            auto event = m_source->receive(100);
            if (m_diagnostics.reportDue(ScrollClock::now())) {
                logScrollingEvent("scrolling.capture_progress", generation, m_diagnostics.fields(),
                                  m_diagnostics.accepted == 0 ? QtWarningMsg : QtInfoMsg);
            }
            if (!m_active.load()) {
                SNOW_SCROLL_TRACE(event.frame.trace,
                                  event.frame.trace->disposition = "source_cancelled");
                break;
            }
            ScrollCaptureResult result;
            result.generation = generation;
            switch (event.kind) {
            case ScrollingSourceEvent::Kind::Frame:
                consumeFrame(generation, std::move(event.frame), result);
                break;
            case ScrollingSourceEvent::Kind::Dropped:
                ++m_diagnostics.droppedEvents;
                result.streamPressure = true;
                break;
            case ScrollingSourceEvent::Kind::Error:
            case ScrollingSourceEvent::Kind::Ended:
                result.fatalError = true;
                result.errorMessage = event.error.isEmpty()
                                          ? QStringLiteral("scrolling source ended unexpectedly")
                                          : event.error;
                break;
            case ScrollingSourceEvent::Kind::Timeout:
                ++m_diagnostics.timeouts;
                continue;
            }
            const bool fatal = result.fatalError;
            if (result.wakeConsumer || result.streamPressure || fatal)
                m_callback(std::move(result));
            if (fatal)
                break;
        }
        logScrollingEvent("scrolling.capture_summary", generation, m_diagnostics.fields());
    }
    void consumeFrame(quint64 generation, ScrollingSourceFrame source,
                      ScrollCaptureResult& result) {
        if (++m_diagnostics.received == 1) {
            logScrollingEvent(
                "scrolling.first_frame", generation,
                {{QStringLiteral("width"), source.image.width()},
                 {QStringLiteral("height"), source.image.height()},
                 {QStringLiteral("pixel_format"), static_cast<int>(source.image.format())},
                 {QStringLiteral("stride_bytes"), source.image.bytesPerLine()},
                 {QStringLiteral("duplicate"), source.duplicate}});
        }
        const auto trace = source.trace;
        SNOW_SCROLL_TRACE(trace, trace->record(scrolling_perf::Stage::SourceQueueWait,
                                               scrolling_perf::now() - trace->publishedAt));
#ifdef Q_OS_MACOS
        // The selected backing scale is fixed for the entire stitching session.
        // Never silently discard frames after a native display reconfiguration.
        if (!source.image.isNull() && source.image.size() != m_viewport) {
            result.fatalError = true;
            result.errorMessage =
                QStringLiteral("scrolling viewport changed after display reconfiguration");
            return;
        }
#endif
        const bool valid = !source.image.isNull() && source.image.size() == m_viewport &&
                           source.image.format() == QImage::Format_RGBA8888 &&
                           source.image.bytesPerLine() == m_viewport.width() * 4;
        SNOW_SCROLL_TRACE(trace, trace->queueDepth = m_mailbox->pendingDepth());
        if (!valid || source.duplicate) {
            if (!valid) {
                if (++m_diagnostics.invalid == 1) {
                    logScrollingEvent(
                        "scrolling.frame_rejected", generation,
                        {{QStringLiteral("width"), source.image.width()},
                         {QStringLiteral("height"), source.image.height()},
                         {QStringLiteral("expected_width"), m_viewport.width()},
                         {QStringLiteral("expected_height"), m_viewport.height()},
                         {QStringLiteral("pixel_format"), static_cast<int>(source.image.format())},
                         {QStringLiteral("stride_bytes"), source.image.bytesPerLine()}},
                        QtWarningMsg);
                }
            } else {
                ++m_diagnostics.duplicates;
            }
            SNOW_SCROLL_TRACE(trace, trace->disposition = !valid ? "invalid" : "duplicate");
            return;
        }
        ++m_diagnostics.accepted;
        OwnedScrollFrame owned(std::move(source.image));
        owned.trace = trace;
        SNOW_SCROLL_TRACE(trace, trace->publishedAt = scrolling_perf::now());
        SNOW_SCROLL_TRACE(trace, trace->disposition = "queued");
        {
            SNOW_SCROLL_SCOPE(trace, MailboxAdmission);
            auto publication = m_mailbox->publish(generation, std::move(owned));
            result.wakeConsumer = publication.wakeConsumer;
            if (publication.replaced.has_value()) {
                ++m_diagnostics.mailboxReplaced;
                publication.replaced->discard("mailbox_replaced");
                result.streamPressure = true;
            }
        }
    }
    std::shared_ptr<ScrollFrameMailbox> m_mailbox;
    Callback m_callback;
    std::unique_ptr<ScrollingFrameSource> m_source;
    snow_shot::runtime::RuntimeActivityLease m_sourceActivity;
    QSize m_viewport;
    ScrollingCaptureDiagnostics m_diagnostics;
    quint64 m_generation = 0;
    AdaptiveScrollCadence m_cadence;
    std::atomic_bool m_active = false;
    std::thread m_consumer;
};

ScreenshotScrollingStitchChange stitchChange(int event) {
    using Change = ScreenshotScrollingStitchChange;
    switch (event) {
    case SNOW_STITCH_FRAME_EVENT_INITIAL:
        return Change::Initial;
    case SNOW_STITCH_FRAME_EVENT_EXTENDED_BOTTOM:
        return Change::AppendedDown;
    case SNOW_STITCH_FRAME_EVENT_EXTENDED_TOP:
        return Change::PrependedUp;
    case SNOW_STITCH_FRAME_EVENT_EXTENDED_RIGHT:
        return Change::AppendedRight;
    case SNOW_STITCH_FRAME_EVENT_EXTENDED_LEFT:
        return Change::PrependedLeft;
    default:
        return Change::Replaced;
    }
}
} // namespace

struct ScreenshotScrollingPipeline::Impl {
    Impl(ScreenshotScrollingPipeline& ownerValue, FrameCallback frames, ErrorCallback errors)
        : owner(ownerValue), frameCallback(std::move(frames)), errorCallback(std::move(errors)) {
        const QPointer<ScreenshotScrollingPipeline> receiver(&owner);
        const QPointer<QObject> delivery(deliveryContext.get());
        producer = new ScreenshotScrollingCaptureProducer(
            mailbox, [receiver, delivery](ScrollCaptureResult result) {
                if (!delivery)
                    return;
                QMetaObject::invokeMethod(
                    delivery,
                    snow_shot::runtime::trackRuntimeWork(
                        [receiver, delivery, result = std::move(result)]() mutable {
                            if (delivery)
                                receiver->m_impl->handleCapture(std::move(result));
                        }),
                    Qt::QueuedConnection);
            });
        producer->moveToThread(&captureThread);
        QObject::connect(&captureThread, &QThread::finished, producer, &QObject::deleteLater);
        worker = new ScreenshotScrollingCaptureWorker;
        worker->moveToThread(&stitchThread);
        QObject::connect(&stitchThread, &QThread::finished, worker, &QObject::deleteLater);
        captureThread.setObjectName(QStringLiteral("snow-shot-scrolling-capture"));
        stitchThread.setObjectName(QStringLiteral("snow-shot-scrolling-stitch"));
        snow_shot::platform::configureApplicationQoSThread(&captureThread);
        captureThread.start();
#ifdef Q_OS_MACOS
        // CPU-intensive stitching must not inherit the application's interactive QoS.
        QObject::connect(
            &stitchThread, &QThread::started, &stitchThread,
            [] {
                const int code = pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0);
                if (code != 0) {
                    snow_shot::diagnostics::logEvent(
                        QStringLiteral("snow_shot.platform"),
                        QStringLiteral("scheduling.qos_failed"),
                        {{QStringLiteral("code"), code},
                         {QStringLiteral("thread"), QStringLiteral("snow-shot-scrolling-stitch")},
                         {QStringLiteral("qos"), QStringLiteral("default")}},
                        QtWarningMsg);
                }
            },
            Qt::DirectConnection);
#else
        snow_shot::platform::configureApplicationQoSThread(&stitchThread);
#endif
        stitchThread.start();
    }
    ~Impl() {
        QMetaObject::invokeMethod(producer, snow_shot::runtime::trackRuntimeWork([this]() {
                                      producer->reset(generation);
                                  }),
                                  Qt::BlockingQueuedConnection);
        captureThread.quit();
        captureThread.wait();
        // Export snapshots accepted before reset must be delivered even when
        // capture immediately destroys the pipeline. quit() from this thread
        // can discard queued work; enqueue it behind the accepted worker jobs.
        QMetaObject::invokeMethod(
            worker, snow_shot::runtime::trackRuntimeWork([this]() { stitchThread.quit(); }),
            Qt::QueuedConnection);
        stitchThread.wait();
    }
    void handleCapture(ScrollCaptureResult result) {
        if (!active || result.generation != generation)
            return;
        if (result.fatalError) {
            if (errorCallback)
                errorCallback(generation, std::move(result.errorMessage));
            return;
        }
        if (result.streamPressure) {
            QMetaObject::invokeMethod(
                producer, snow_shot::runtime::trackRuntimeWork([this, value = generation]() {
                    producer->recordStreamPressure(value);
                }),
                Qt::QueuedConnection);
        }
        if (result.wakeConsumer)
            schedule();
    }
    void schedule() {
        if (!active)
            return;
        auto next = mailbox->take();
        if (!next)
            return;
        busy = true;
        auto frame = std::make_shared<OwnedScrollFrame>(std::move(next->item));
        auto trace = frame->trace;
        SNOW_SCROLL_TRACE(trace, trace->dispatchedAt = scrolling_perf::now());
        SNOW_SCROLL_TRACE(trace, trace->record(scrolling_perf::Stage::MailboxWait,
                                               trace->dispatchedAt - trace->publishedAt));
        const QPointer<ScreenshotScrollingPipeline> receiver(&owner);
        const QPointer<QObject> delivery(deliveryContext.get());
        QMetaObject::invokeMethod(
            worker,
            snow_shot::runtime::trackRuntimeWork([receiver, delivery, target = worker,
                                                  value = next->generation, frame,
                                                  trace]() mutable {
                SNOW_SCROLL_TRACE(trace,
                                  trace->record(scrolling_perf::Stage::WorkerDispatch,
                                                scrolling_perf::now() - trace->dispatchedAt));
                const auto started = ScrollClock::now();
                auto result = target->process(value, frame->release(), trace);
                result.processingDuration = ScrollClock::now() - started;
                result.change = stitchChange(result.event);
                SNOW_SCROLL_TRACE(trace, trace->completedAt = scrolling_perf::now());
                if (!delivery)
                    return;
                QMetaObject::invokeMethod(
                    delivery,
                    snow_shot::runtime::trackRuntimeWork(
                        [receiver, delivery, result = std::move(result)]() mutable {
                            if (delivery)
                                receiver->m_impl->handleFrame(std::move(result));
                        }),
                    Qt::QueuedConnection);
            }),
            Qt::QueuedConnection);
    }
    void handleFrame(ScrollingPipelineFrame result) {
        const bool next = mailbox->finish(result.generation);
        if (result.generation != generation) {
            SNOW_SCROLL_TRACE(result.trace, result.trace->disposition = "stale_generation");
            return;
        }
        busy = false;
        // A dispatched frame already changed the stitcher. Publish its result even
        // while paused, so the preview and trim coordinates stay in sync.
        if (++processedFrames == 1 || result.fatalError) {
            logScrollingEvent("scrolling.stitch_result", generation,
                              {{QStringLiteral("count"), processedFrames},
                               {QStringLiteral("code"), result.event},
                               {QStringLiteral("changed"), result.changed},
                               {QStringLiteral("fatal"), result.fatalError},
                               {QStringLiteral("width"), result.sourceSize.width()},
                               {QStringLiteral("height"), result.sourceSize.height()}},
                              result.fatalError ? QtWarningMsg : QtInfoMsg);
        }
        auto trace = result.trace;
        Q_UNUSED(trace);
        SNOW_SCROLL_TRACE(trace, trace->record(scrolling_perf::Stage::ReturnQueueWait,
                                               scrolling_perf::now() - trace->completedAt));
        SNOW_SCROLL_TRACE(trace, trace->disposition = result.fatalError ? "error" : "processed");
        SNOW_SCROLL_TRACE(trace, trace->event = result.event);
        SNOW_SCROLL_TRACE(trace, trace->outputHeight = result.sourceSize.height());
        QMetaObject::invokeMethod(
            producer,
            snow_shot::runtime::trackRuntimeWork(
                [this, value = generation, duration = result.processingDuration]() {
                    producer->recordStitch(value, duration);
                }),
            Qt::QueuedConnection);
        if (next && !result.fatalError)
            schedule();
        if (frameCallback)
            frameCallback(std::move(result));
    }

    ScreenshotScrollingPipeline& owner;
    FrameCallback frameCallback;
    ErrorCallback errorCallback;
    std::unique_ptr<QObject> deliveryContext = std::make_unique<QObject>();
    std::shared_ptr<ScrollFrameMailbox> mailbox = std::make_shared<ScrollFrameMailbox>();
    QThread captureThread;
    QThread stitchThread;
    ScreenshotScrollingCaptureProducer* producer = nullptr;
    ScreenshotScrollingCaptureWorker* worker = nullptr;
    quint64 generation = 0;
    quint64 controlRevision = 0;
    bool active = false;
    bool busy = false;
    qint64 processedFrames = 0;
};

ScreenshotScrollingPipeline::ScreenshotScrollingPipeline(FrameCallback frames, ErrorCallback errors,
                                                         QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, std::move(frames), std::move(errors))) {
}
ScreenshotScrollingPipeline::~ScreenshotScrollingPipeline() {
    shutdown(nullptr, {});
}

void ScreenshotScrollingPipeline::shutdown(QObject* receiver, std::function<void()> completed) {
    if (!m_impl)
        return;
    m_impl->active = false;
    // Cancel UI/control delivery before detaching ownership. Queued worker operations retain
    // their targets until the cleanup thread joins both Qt threads; export snapshots instead
    // belong to their independent receivers and must drain before the stitch thread quits.
    m_impl->deliveryContext.reset();
    m_impl->frameCallback = {};
    m_impl->errorCallback = {};
    m_impl->mailbox->reset(m_impl->generation);
    // QThread objects are control handles with GUI affinity, separate from the workers they
    // run. Detach that affinity so the cleanup thread can adopt and destroy the handles after
    // wait() without posting their destruction back to the GUI event loop.
    m_impl->captureThread.moveToThread(nullptr);
    m_impl->stitchThread.moveToThread(nullptr);
    const QPointer<ScrollingShutdownCompletion> completion(
        receiver && completed ? new ScrollingShutdownCompletion(receiver, std::move(completed))
                              : nullptr);
    ScrollingShutdownTasks::shared().submit(
        snow_shot::runtime::trackRuntimeWork([impl = std::move(m_impl), completion]() mutable {
            impl->captureThread.moveToThread(QThread::currentThread());
            impl->stitchThread.moveToThread(QThread::currentThread());
            impl.reset();
            if (completion) {
                QMetaObject::invokeMethod(
                    completion,
                    [completion] {
                        if (completion)
                            completion->complete();
                    },
                    Qt::QueuedConnection);
            }
        }));
}

void ScreenshotScrollingPipeline::waitForShutdowns() {
    ScrollingShutdownTasks::shared().wait();
}

void ScreenshotScrollingPipeline::begin(quint64 generation, QSize viewport,
                                        ScreenshotScrollingRecognitionMode mode,
                                        ScrollingSourceFactory source,
                                        AdaptiveScrollingCaptureCadence::Config cadence) {
    reset(generation);
    QMetaObject::invokeMethod(
        m_impl->worker,
        snow_shot::runtime::trackRuntimeWork(
            [target = m_impl->worker, generation, mode]() { target->begin(generation, mode); }),
        Qt::QueuedConnection);
    m_impl->active = true;
    QMetaObject::invokeMethod(
        m_impl->producer,
        snow_shot::runtime::trackRuntimeWork(
            [target = m_impl->producer, generation, viewport, source = std::move(source),
             cadence]() { target->begin(generation, viewport, source, cadence); }),
        Qt::QueuedConnection);
}
void ScreenshotScrollingPipeline::reset(quint64 generation) {
    ++m_impl->controlRevision;
    m_impl->active = false;
    m_impl->generation = generation;
    m_impl->busy = false;
    m_impl->processedFrames = 0;
    m_impl->mailbox->reset(generation);
    QMetaObject::invokeMethod(
        m_impl->producer,
        snow_shot::runtime::trackRuntimeWork(
            [target = m_impl->producer, generation]() { target->reset(generation); }),
        Qt::QueuedConnection);
    QMetaObject::invokeMethod(
        m_impl->worker,
        snow_shot::runtime::trackRuntimeWork(
            [target = m_impl->worker, generation]() { target->reset(generation); }),
        Qt::QueuedConnection);
}
void ScreenshotScrollingPipeline::pause(quint64 generation, std::function<void()> acknowledged) {
    if (generation != m_impl->generation)
        return;
    const auto revision = ++m_impl->controlRevision;
    m_impl->active = false;
    m_impl->mailbox->reset(generation);
    const QPointer<ScreenshotScrollingPipeline> receiver(this);
    const QPointer<QObject> delivery(m_impl->deliveryContext.get());
    QMetaObject::invokeMethod(
        m_impl->producer,
        snow_shot::runtime::trackRuntimeWork([target = m_impl->producer, worker = m_impl->worker,
                                              receiver, delivery, generation, revision,
                                              acknowledged = std::move(acknowledged)]() mutable {
            target->pause(generation);
            if (!delivery || !acknowledged)
                return;
            // The source has stopped and joined. This worker barrier posts to the same GUI
            // receiver as stitch results, so their extent/trim updates precede acknowledgment.
            QMetaObject::invokeMethod(
                worker,
                snow_shot::runtime::trackRuntimeWork(
                    [receiver, delivery, generation, revision,
                     acknowledged = std::move(acknowledged)]() mutable {
                        if (!delivery)
                            return;
                        QMetaObject::invokeMethod(
                            delivery,
                            snow_shot::runtime::trackRuntimeWork(
                                [receiver, delivery, generation, revision,
                                 acknowledged = std::move(acknowledged)]() mutable {
                                    if (!delivery || receiver->m_impl->generation != generation ||
                                        receiver->m_impl->controlRevision != revision)
                                        return;
                                    // A consumer already copying a frame may publish after the
                                    // first reset. It is now joined, so this removes every
                                    // race-admitted frame.
                                    receiver->m_impl->mailbox->reset(generation);
                                    acknowledged();
                                }),
                            Qt::QueuedConnection);
                    }),
                Qt::QueuedConnection);
        }),
        Qt::QueuedConnection);
}
void ScreenshotScrollingPipeline::resume(quint64 generation, QSize viewport,
                                         ScrollingSourceFactory source,
                                         AdaptiveScrollingCaptureCadence::Config cadence) {
    const auto revision = ++m_impl->controlRevision;
    m_impl->active = false;
    m_impl->generation = generation;
    const QPointer<ScreenshotScrollingPipeline> receiver(this);
    const QPointer<QObject> delivery(m_impl->deliveryContext.get());
    // Producer ordering ensures the previous source has stopped. Worker ordering
    // then delivers all committed stitch results before reopening frame acceptance.
    QMetaObject::invokeMethod(
        m_impl->producer,
        snow_shot::runtime::trackRuntimeWork([receiver, delivery, worker = m_impl->worker,
                                              generation, viewport, source = std::move(source),
                                              cadence, revision]() mutable {
            if (!delivery)
                return;
            QMetaObject::invokeMethod(
                worker,
                snow_shot::runtime::trackRuntimeWork([receiver, delivery, generation, viewport,
                                                      source = std::move(source), cadence,
                                                      revision]() mutable {
                    if (!delivery)
                        return;
                    QMetaObject::invokeMethod(
                        delivery,
                        snow_shot::runtime::trackRuntimeWork([receiver, delivery, generation,
                                                              viewport, source = std::move(source),
                                                              cadence, revision]() mutable {
                            if (!delivery || receiver->m_impl->controlRevision != revision)
                                return;
                            auto* impl = receiver->m_impl.get();
                            impl->mailbox->reset(generation);
                            impl->active = true;
                            QMetaObject::invokeMethod(
                                impl->producer,
                                snow_shot::runtime::trackRuntimeWork(
                                    [target = impl->producer, generation, viewport,
                                     source = std::move(source), cadence]() {
                                        target->begin(generation, viewport, source, cadence);
                                    }),
                                Qt::QueuedConnection);
                        }),
                        Qt::QueuedConnection);
                }),
                Qt::QueuedConnection);
        }),
        Qt::QueuedConnection);
}

bool ScreenshotScrollingPipeline::idle() const {
    return !m_impl->busy && m_impl->mailbox->pendingDepth() == 0;
}
void ScreenshotScrollingPipeline::finishInput(std::function<void()> callback) {
    const QPointer<QObject> delivery(m_impl->deliveryContext.get());
    QMetaObject::invokeMethod(
        m_impl->producer,
        snow_shot::runtime::trackRuntimeWork([target = m_impl->producer, value = m_impl->generation,
                                              delivery, callback = std::move(callback)]() mutable {
            target->pause(value);
            if (delivery)
                QMetaObject::invokeMethod(delivery,
                                          snow_shot::runtime::trackRuntimeWork(
                                              [delivery, callback = std::move(callback)]() {
                                                  if (delivery)
                                                      callback();
                                              }),
                                          Qt::QueuedConnection);
        }),
        Qt::QueuedConnection);
}
bool ScreenshotScrollingPipeline::requestSnapshot(int top, int bottom, QObject* receiver,
                                                  SnapshotCallback callback) {
    const QPointer<QObject> guarded(receiver);
    return QMetaObject::invokeMethod(
        m_impl->worker,
        snow_shot::runtime::trackRuntimeWork([target = m_impl->worker, top, bottom, guarded,
                                              callback = std::move(callback)]() mutable {
            auto result = target->trimmedSnapshot(top, bottom);
            if (!guarded)
                return;
            QMetaObject::invokeMethod(
                guarded,
                snow_shot::runtime::trackRuntimeWork([guarded, callback = std::move(callback),
                                                      result = std::move(result)]() mutable {
                    if (guarded)
                        callback(std::move(result));
                }),
                Qt::QueuedConnection);
        }),
        Qt::QueuedConnection);
}

bool ScreenshotScrollingPipeline::requestViewportPreview(int start, int end, QObject* receiver,
                                                         ViewportPreviewCallback callback) {
    if (end <= start || static_cast<qint64>(end) - start > std::numeric_limits<int>::max() ||
        receiver == nullptr || !callback)
        return false;
    const QPointer<QObject> guarded(receiver);
    const QPointer<ScreenshotScrollingPipeline> pipeline(this);
    const QPointer<QObject> delivery(m_impl->deliveryContext.get());
    const auto generation = m_impl->generation;
    const auto revision = m_impl->controlRevision;
    return QMetaObject::invokeMethod(
        m_impl->worker,
        snow_shot::runtime::trackRuntimeWork([target = m_impl->worker, pipeline, delivery,
                                              generation, revision, start, end, guarded,
                                              callback = std::move(callback)]() mutable {
            if (!guarded || !delivery)
                return;
            QImage result = target->viewportPreview(generation, start, end);
            QMetaObject::invokeMethod(
                guarded,
                snow_shot::runtime::trackRuntimeWork([pipeline, delivery, generation, revision,
                                                      guarded, callback = std::move(callback),
                                                      result = std::move(result)]() mutable {
                    if (!delivery || !guarded)
                        return;
                    if (pipeline->m_impl->generation != generation ||
                        pipeline->m_impl->controlRevision != revision)
                        result = {};
                    callback(std::move(result));
                }),
                Qt::QueuedConnection);
        }),
        Qt::QueuedConnection);
}
} // namespace snow_shot::capture_detail

namespace {
void releaseSnapshotImage(void* image) {
    snow_stitch_owned_image_destroy(static_cast<SnowStitchOwnedImage*>(image));
}
} // namespace

ScreenshotScrollingSnapshot ScreenshotScrollingSnapshot::adoptNative(void* snapshot, QSize size) {
    ScreenshotScrollingSnapshot result;
    if (snapshot == nullptr || !size.isValid() || size.isEmpty()) {
        return result;
    }
    result.m_snapshot = std::shared_ptr<void>(snapshot, [](void* value) {
        snow_stitch_snapshot_destroy(static_cast<SnowStitchSnapshot*>(value));
    });
    result.m_size = size;
    return result;
}

bool ScreenshotScrollingSnapshot::isValid() const {
    return m_snapshot != nullptr && m_size.isValid() && !m_size.isEmpty();
}

ScreenshotImageRowSource
ScreenshotScrollingSnapshot::rowSource(std::function<bool()> cancellationRequested) const {
    ScreenshotImageRowSource source;
    if (!isValid()) {
        return source;
    }
    source.size = m_size;
    source.cancellationRequested = std::move(cancellationRequested);
    const std::shared_ptr<void> snapshot = m_snapshot;
    source.readRows = [snapshot](int firstRow, int rowCount, qsizetype destinationStride,
                                 uchar* destination, qsizetype destinationSize) {
        if (firstRow < 0 || rowCount <= 0 || destinationStride <= 0 || destination == nullptr ||
            destinationSize <= 0) {
            return false;
        }
        return snow_stitch_snapshot_copy_rows(
                   static_cast<const SnowStitchSnapshot*>(snapshot.get()),
                   static_cast<std::uint32_t>(firstRow), static_cast<std::uint32_t>(rowCount),
                   static_cast<std::size_t>(destinationStride), destination,
                   static_cast<std::size_t>(destinationSize)) != 0;
    };
    // The native capture lease is RGBA. Verify the actual selected pixels so
    // encoders can omit an opaque channel, including for a trimmed snapshot.
    using Alpha = ScreenshotImageAlphaContent;
    const auto opacity = std::make_shared<std::atomic<Alpha>>(Alpha::Unknown);
    source.classifyAlpha = [read = source.readRows, size = source.size,
                            opacity](const std::function<bool()>& cancel) {
        const auto cached = opacity->load(std::memory_order_relaxed);
        if (cached != Alpha::Unknown)
            return cached;
        if (cancel && cancel())
            return Alpha::Unknown;
        const qsizetype stride = qsizetype(size.width()) * 4;
        const auto batch =
            int(std::min<qsizetype>(size.height(), std::max<qsizetype>(1, 1024 * 1024 / stride)));
        QImage buffer(size.width(), batch, QImage::Format_RGBA8888);
        if (buffer.isNull())
            return Alpha::Unknown;
        auto alpha = Alpha::Opaque;
        for (int first = 0; first < size.height();) {
            const int count = std::min(batch, size.height() - first);
            if ((cancel && cancel()) || !read(first, count, stride, buffer.bits(), count * stride))
                return Alpha::Unknown;
            const QImage rows(buffer.constBits(), size.width(), count, stride,
                              QImage::Format_RGBA8888);
            alpha = snow_shot::image_codec::detail::imageAlphaContent(rows);
            if (alpha != Alpha::Opaque)
                break;
            first += count;
        }
        // Publish only verified results so an interrupted scan can be retried by another export.
        if (alpha != Alpha::Unknown)
            opacity->store(alpha, std::memory_order_relaxed);
        return alpha;
    };
    return source;
}

QImage ScreenshotScrollingSnapshot::materialize(std::function<bool()> cancellationRequested) const {
    if (!isValid()) {
        return {};
    }
    if (cancellationRequested) {
        if (cancellationRequested())
            return {};
        QImage result = snowCanvasAllocateImage(m_size, QImage::Format_RGBA8888);
        if (result.isNull())
            return {};
        const auto source = rowSource(cancellationRequested);
        const int batch =
            static_cast<int>(qMax(qsizetype{1}, qsizetype{1024 * 1024} / result.bytesPerLine()));
        for (int row = 0; row < m_size.height(); row += batch) {
            const int count = qMin(batch, m_size.height() - row);
            if (cancellationRequested() ||
                !source.readRows(row, count, result.bytesPerLine(), result.scanLine(row),
                                 result.bytesPerLine() * count))
                return {};
        }
        if (cancellationRequested())
            return {};
        result.setColorSpace(QColorSpace::SRgb);
        return result;
    }
    SnowStitchOwnedImage* image =
        snow_stitch_snapshot_materialize(static_cast<const SnowStitchSnapshot*>(m_snapshot.get()));
    if (image == nullptr) {
        return {};
    }
    SnowStitchImageInfo info{};
    if (snow_stitch_owned_image_info(image, &info) == 0 || info.rgba_bytes == nullptr ||
        info.width != static_cast<std::uint32_t>(m_size.width()) ||
        info.height != static_cast<std::uint32_t>(m_size.height()) ||
        info.stride_bytes != info.width * 4) {
        snow_stitch_owned_image_destroy(image);
        return {};
    }
    QImage result(info.rgba_bytes, m_size.width(), m_size.height(),
                  static_cast<int>(info.stride_bytes), QImage::Format_RGBA8888,
                  &releaseSnapshotImage, image);
    if (result.isNull()) {
        snow_stitch_owned_image_destroy(image);
    } else {
        result.setColorSpace(QColorSpace::SRgb);
    }
    return result;
}
