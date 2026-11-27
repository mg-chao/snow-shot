#include "snow_shot/runtime/runtimeactivitytracker.h"
#include "snow_shot/diagnostics/diagnostics.h"
#include "recordingtrimsession.h"
#include "recordingrenderdialog.h"
#include "recordingtrimtoolbar.h"
#include "snow_shot/presentation/screenrecordingfolder.h"
#include "snow_shot/presentation/screenshotimagefileservice.h"
#include "snow_shot/presentation/screenrecordingareawindow.h"
#include "snow_shot/presentation/screenrecordingtoolbarwindow.h"
#include "snow_shot/platform/applicationqos.h"
#include "snow_shot/storage/settingsadapters.h"
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>
#include <climits>
#include <thread>
#include <utility>

namespace {
QString trimText(const char* text) {
    return QCoreApplication::translate("RecordingTrimSession", text);
}
QString nativeError() {
    return QString::fromUtf8(snow_recording_last_error_message());
}
template <class Read> QString nativeText(Read read) {
    const size_t size = read(nullptr, 0);
    if (size == 0 || size > 1024 * 1024)
        return {};
    QByteArray data(static_cast<qsizetype>(size), '\0');
    read(data.data(), size);
    return QString::fromUtf8(data.constData());
}
void copyRecording(const QString& path) {
    auto* data = new QMimeData;
    data->setUrls({QUrl::fromLocalFile(path)});
    QApplication::clipboard()->setMimeData(data);
}
QString nextOutput(const QString& original,
                   const snow_shot::storage::ScreenshotExportContext& exportContext) {
    const QFileInfo file(original);
    const QString baseName = snow_shot::storage::expandScreenshotExportVariables(
        snow_shot::storage::RecordingSettings().videoFilenameFormat(), QDateTime::currentDateTime(),
        exportContext);
    return snow_shot::presentation::recording::screenRecordingOutputPath({file.absolutePath()},
                                                                         baseName, file.suffix());
}
} // namespace

RecordingTrimSession::RecordingTrimSession(ScreenRecordingAreaWindow* area,
                                           ScreenRecordingToolbarWindow* toolbar, QObject* parent)
    : QObject(parent), m_area(area), m_toolbar(toolbar) {
    m_panel = new RecordingTrimToolbar;
    m_toolbar->palette()->setRecordingTrimPanel(m_panel, true);
    m_panel->commitControlScale(adqt::widgets::controlScaleContextFor(m_toolbar->palette()));
    m_panel->replayRequested = [this] { seek(static_cast<int>(m_first), true); };
    m_panel->playbackToggleRequested = [this] { togglePlayback(); };
    m_panel->seekRequested = [this](int frame) { seek(frame, false); };
    m_panel->rangeChanged = [this](int first, int end, int preview) {
        m_first = static_cast<quint64>(first);
        m_end = static_cast<quint64>(end);
        seek(preview, false);
    };
    m_panel->speedChanged = [this](quint32 percent) { setSpeed(percent); };
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this] { poll(); });
}

RecordingTrimSession::~RecordingTrimSession() {
    if (m_saveDialog) {
        disconnect(m_saveDialog, nullptr, this, nullptr);
        m_saveDialog->reject();
    }
    if (m_dialog)
        delete m_dialog;
    // A controller can be destroyed during acquisition or an export. Transfer the
    // complete dependency chain before releasing any native resource.
    auto clip = m_clip;
    auto task = m_export;
    std::thread(snow_shot::runtime::trackRuntimeWork([open = std::move(m_openFuture),
                                                      publish = std::move(m_publishFuture),
                                                      cancellation = std::move(m_cancelFuture),
                                                      request = std::move(m_request),
                                                      cached = std::move(m_cachedClip), clip,
                                                      task]() mutable {
        snow_shot::platform::applyApplicationQoSToCurrentThread();
        if (open.valid())
            clip = open.get().clip;
        if (publish.valid())
            static_cast<void>(publish.get());
        if (cancellation.valid())
            cancellation.get();
        if (task)
            snow_recording_clip_export_destroy(task);
        if (clip)
            snow_recording_clip_destroy(clip);
        request.reset();
        cached.reset();
    })).detach();
}

void RecordingTrimSession::open(const QString& sourcePath, const QString& outputPath,
                                const SnowRecordingClipOptions& options, bool deferred,
                                bool saveWhenReady,
                                snow_shot::storage::ScreenshotExportContext exportContext) {
    m_area->setTrimming(true);
    m_mediaActivity = snow_shot::runtime::RuntimeActivityTracker::shared().acquire();
    m_sourcePath = sourcePath;
    m_outputPath = outputPath;
    m_exportContext = std::move(exportContext);
    m_saveWhenReady = saveWhenReady;
    m_deferred = deferred;
    m_options = options;
    m_operation = QUuid::createUuid().toString(QUuid::Id128);
    report(QStringLiteral("recording.trim_open_started"));
    m_openFuture = std::async(
        std::launch::async, snow_shot::runtime::trackRuntimeWork([sourcePath, options, deferred] {
            snow_shot::platform::applyApplicationQoSToCurrentThread();
            OpenResult result;
            result.clip = snow_recording_clip_open(sourcePath.toUtf8().constData(),
                                                   deferred ? nullptr : &options);
            if (!result.clip)
                result.error = nativeError();
            return result;
        }));
    m_timer.start();
}

bool RecordingTrimSession::ready() const {
    return m_clip && !m_busy;
}
QString RecordingTrimSession::phase() const {
    switch (m_phase) {
    case Phase::Preparing:
        return QStringLiteral("preparing");
    case Phase::Trimming:
        return QStringLiteral("trimming");
    case Phase::Exporting:
        return QStringLiteral("exporting");
    }
    Q_UNREACHABLE();
}
void RecordingTrimSession::setBusy(bool busy) {
    m_busy = busy;
    if (m_toolbar && m_panel)
        m_toolbar->palette()->setRecordingTrimPanel(m_panel, busy);
}
void RecordingTrimSession::seek(int frame, bool play) {
    if (!ready() || m_detached || m_previewFailed)
        return;
    const auto revision =
        snow_recording_clip_seek(m_clip, static_cast<quint64>(frame), m_end, play ? 1 : 0);
    if (!revision)
        return;
    m_revision = revision;
    m_position = snow_recording_clip_boundary(m_clip, static_cast<quint64>(frame));
    m_playing = play;
    m_panel->setPlaying(m_playing);
    m_timer.setInterval(16);
}
void RecordingTrimSession::togglePlayback() {
    if (!ready() || m_detached || m_previewFailed)
        return;
    quint64 frame = m_first;
    if (m_playing || m_position < snow_recording_clip_boundary(m_clip, m_end)) {
        // Find the frame containing the displayed position, including variable-duration frames.
        quint64 low = m_first, high = m_end;
        while (low + 1 < high) {
            const quint64 middle = low + (high - low) / 2;
            if (snow_recording_clip_boundary(m_clip, middle) <= m_position)
                low = middle;
            else
                high = middle;
        }
        frame = low;
    }
    seek(static_cast<int>(frame), !m_playing);
}
void RecordingTrimSession::setSpeed(quint32 percent) {
    if (!ready() || m_detached || percent == m_speedPercent)
        return;
    const auto revision = snow_recording_clip_set_speed(m_clip, percent);
    if (!revision) {
        if (m_panel)
            m_panel->setSpeedPercent(m_speedPercent);
        if (reportError)
            reportError(trimText(QT_TRANSLATE_NOOP("RecordingTrimSession",
                                                   "Unable to preview recording: %1"))
                            .arg(nativeError()));
        return;
    }
    m_speedPercent = percent;
    m_revision = revision;
    m_timer.setInterval(16);
}

void RecordingTrimSession::poll() {
    if (m_publication)
        m_percent = m_publication->percent.load();
    if (m_openFuture.valid() &&
        m_openFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const auto result = m_openFuture.get();
        m_clip = result.clip;
        if (m_detached) {
            releaseMedia();
            return;
        }
        if (!m_clip || !snow_recording_clip_info(m_clip, &m_info) || m_info.frame_count > INT_MAX) {
            m_timer.stop();
            report(QStringLiteral("recording.trim_open_failed"),
                   result.error.isEmpty() ? QStringLiteral("Invalid clip metadata or frame count")
                                          : result.error,
                   QtWarningMsg);
            if (reportError)
                reportError(trimText(QT_TRANSLATE_NOOP("RecordingTrimSession",
                                                       "Unable to preview recording: %1"))
                                .arg(result.error));
            return;
        }
        m_end = m_info.frame_count;
        m_panel->setTimeline(static_cast<int>(m_end), m_info.duration_us, [this](int frame) {
            return snow_recording_clip_boundary(m_clip, static_cast<quint64>(frame));
        });
        m_phase = Phase::Trimming;
        report(QStringLiteral("recording.trim_ready"));
        setBusy(false);
        if (m_saveWhenReady) {
            exportClip(true);
            return;
        }
        seek(0, true);
    }
    if (m_export) {
        float percent = 0;
        const auto state = snow_recording_clip_export_poll(m_export, &percent);
        m_percent = static_cast<double>(percent);
        if (state != SNOW_RECORDING_RENDER_STATE_RUNNING &&
            (!m_cancelFuture.valid() ||
             m_cancelFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)) {
            if (m_cancelFuture.valid())
                m_cancelFuture.get();
            const QString error = nativeText([this](char* data, size_t capacity) {
                return snow_recording_clip_export_error(m_export, data, capacity);
            });
            const bool succeeded = state == SNOW_RECORDING_RENDER_STATE_SUCCEEDED;
            retireExportTask(succeeded);
            if (succeeded)
                publish(m_cachedClip->file);
            else
                finishExport(false, error, state == SNOW_RECORDING_RENDER_STATE_CANCELED);
        }
    }
    if (m_publishFuture.valid() &&
        m_publishFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        auto result = m_publishFuture.get();
        if (m_request->kind == ExportKind::Copy) {
            if (result.ok)
                m_cachedClip->file.path = m_request->destination;
            m_cachedClip->file.temporaryDirectory = std::move(result.retainedDirectory);
        }
        finishExport(result.ok, result.error,
                     m_publication && m_publication->state.load() == Publication::Canceled);
    }
    if (m_dialog) {
        m_dialog->percent = m_percent;
        m_dialog->stage = m_publication ? SNOW_RECORDING_RENDER_STAGE_FINALIZE
                                        : SNOW_RECORDING_RENDER_STAGE_RENDER;
        m_dialog->refresh();
    }
    if (!m_clip || m_detached || m_previewFailed)
        return;
    SnowRecordingClipPreview preview{};
    auto* lease = snow_recording_clip_acquire(m_clip, &preview);
    if (lease && preview.revision >= m_revision &&
        preview.byte_count == static_cast<size_t>(m_info.width) * m_info.height * 4) {
        const QImage frame(
            preview.rgba, static_cast<int>(m_info.width), static_cast<int>(m_info.height),
            static_cast<qsizetype>(m_info.width) * 4, QImage::Format_RGBA8888,
            [](void* value) {
                snow_recording_clip_frame_destroy(static_cast<SnowRecordingClipFrame*>(value));
            },
            lease);
        m_area->setPreviewFrame(frame);
        m_position = preview.position_us;
        m_playing = preview.playing != 0;
        m_panel->setPosition(preview.position_us);
        m_panel->setPlaying(m_playing);
        m_timer.setInterval(preview.playing ? 16 : 100);
    } else if (lease)
        snow_recording_clip_frame_destroy(lease);
    const QString error = nativeText([this](char* data, size_t capacity) {
        return snow_recording_clip_error(m_clip, data, capacity);
    });
    if (!error.isEmpty()) {
        m_previewFailed = true;
        report(QStringLiteral("recording.trim_preview_failed"), error, QtWarningMsg);
        m_playing = false;
        m_panel->setPlaying(false);
        m_panel->setPlaybackAvailable(false);
        if (reportError)
            reportError(trimText(QT_TRANSLATE_NOOP("RecordingTrimSession",
                                                   "Unable to preview recording: %1"))
                            .arg(error));
    }
}

void RecordingTrimSession::exportClip(bool save) {
    if (!ready() || m_detached || m_saveDialog)
        return;
    seek(static_cast<int>(m_first), false);
    if (save)
        chooseSavePath();
    else
        beginExport(ExportKind::Copy);
}

void RecordingTrimSession::chooseSavePath() {
    const QString suffix = QFileInfo(m_outputPath).suffix();
    const QString filter = QStringLiteral("%1 (*.%2)").arg(suffix.toUpper(), suffix);
    auto* dialog =
        new QFileDialog(m_area, trimText(QT_TRANSLATE_NOOP("RecordingTrimSession", "Save to file")),
                        nextOutput(m_outputPath, m_exportContext), filter);
    m_saveDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setDefaultSuffix(suffix);
    connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
        if (m_saveDialog != dialog)
            return;
        m_saveDialog.clear();
        if (!m_detached && result == QDialog::Accepted)
            beginExport(ExportKind::Save, dialog->selectedFiles().value(0));
    });
    dialog->show();
}

void RecordingTrimSession::beginExport(ExportKind kind, QString destination) {
    if (kind == ExportKind::Save) {
        if (destination.isEmpty())
            return;
        if (QFileInfo(destination).canonicalFilePath() ==
            QFileInfo(m_sourcePath).canonicalFilePath()) {
            if (reportError)
                reportError(trimText(QT_TRANSLATE_NOOP(
                    "RecordingTrimSession",
                    "Choose a different file to preserve the original recording")));
            return;
        }
    }
    const Selection selection{m_first, m_end, m_speedPercent};
    m_percent = 0;
    m_phase = Phase::Exporting;
    auto& request =
        m_request.emplace(ExportRequest{kind,
                                        selection,
                                        std::move(destination),
                                        {},
                                        snow_recording_clip_boundary(m_clip, selection.first),
                                        snow_recording_clip_boundary(m_clip, selection.end)});
    ++m_exportAttempt;
    m_exportTimer.start();
    setBusy(true);
    const bool reuseSource = !m_deferred && selection == Selection{0, m_info.frame_count, 100} &&
                             QFileInfo::exists(m_sourcePath);
    const bool reuseCache = m_cachedClip && m_cachedClip->selection == selection &&
                            QFileInfo::exists(m_cachedClip->file.path);
    request.cacheHit = reuseSource || reuseCache;
    report(QStringLiteral("recording.trim_export_started"));
    if (reuseSource) {
        publish({m_sourcePath, {}});
        return;
    }
    if (reuseCache) {
        publish(m_cachedClip->file);
        return;
    }
    if (kind == ExportKind::Save) {
        auto directory = std::make_unique<QTemporaryDir>(
            QFileInfo(request.destination)
                .absoluteDir()
                .filePath(QStringLiteral(".snow-shot-trim-XXXXXX")));
        if (!directory->isValid()) {
            finishExport(false, directory->errorString());
            return;
        }
        const QString suffix = QFileInfo(m_outputPath).suffix();
        request.encoding = {directory->filePath(QStringLiteral("clip.") + suffix),
                            std::move(directory)};
    } else {
        request.encoding.path = nextOutput(m_outputPath, m_exportContext);
        request.destination = request.encoding.path;
    }
    m_export = snow_recording_clip_export_start_with_speed(
        m_clip, selection.first, selection.end, request.encoding.path.toUtf8().constData(),
        selection.speedPercent);
    if (!m_export) {
        finishExport(false, nativeError());
        return;
    }
    QTimer::singleShot(300, this, [this] {
        if (m_export)
            showProgress();
    });
}

void RecordingTrimSession::retireExportTask(bool succeeded) {
    auto* completed = std::exchange(m_export, nullptr);
    auto encoding = std::move(m_request->encoding);
    std::optional<CachedClip> retiredCache;
    if (succeeded)
        retiredCache =
            std::exchange(m_cachedClip, CachedClip{m_request->selection, std::move(encoding)});
    std::thread(snow_shot::runtime::trackRuntimeWork([completed,
                                                      directory =
                                                          std::move(encoding.temporaryDirectory),
                                                      retiredCache =
                                                          std::move(retiredCache)]() mutable {
        snow_recording_clip_export_destroy(completed);
        directory.reset();
        retiredCache.reset();
    })).detach();
}

void RecordingTrimSession::publish(const ExportFile& input) {
    auto& request = *m_request;
    std::unique_ptr<QTemporaryDir> retireOnCopy;
    if (request.kind == ExportKind::Copy) {
        if (!input.temporaryDirectory) {
            request.destination = input.path;
            finishExport(true, {});
            return;
        }
        request.destination = nextOutput(m_outputPath, m_exportContext);
        retireOnCopy = std::move(m_cachedClip->file.temporaryDirectory);
    }
    m_publication = std::make_shared<Publication>();
    m_publishFuture =
        std::async(std::launch::async,
                   snow_shot::runtime::trackRuntimeWork(
                       [from = input.path, to = request.destination, kind = request.kind,
                        state = m_publication, directory = std::move(retireOnCopy)]() mutable {
                           snow_shot::platform::applyApplicationQoSToCurrentThread();
                           auto result = state->write(from, to, kind);
                           if (result.ok)
                               directory.reset();
                           else
                               result.retainedDirectory = std::move(directory);
                           return result;
                       }));
    QTimer::singleShot(300, this, [this] {
        if (m_publishFuture.valid())
            showProgress();
    });
}

RecordingTrimSession::PublishResult
RecordingTrimSession::Publication::write(const QString& from, const QString& to, ExportKind kind) {
    QFile source(from);
    if (!source.open(QIODevice::ReadOnly))
        return {false, source.errorString(), {}};
    const auto writeAndCommit = [&](QFileDevice& destination, auto commit) -> PublishResult {
        QByteArray buffer(256 * 1024, Qt::Uninitialized);
        const qint64 total = qMax<qint64>(1, source.size());
        qint64 copied = 0;
        while (state.load() != Canceled) {
            const qint64 size = source.read(buffer.data(), buffer.size());
            if (size < 0)
                return {false, source.errorString(), {}};
            if (size == 0)
                break;
            if (destination.write(buffer.constData(), size) != size)
                return {false, destination.errorString(), {}};
            copied += size;
            percent.store(
                static_cast<int>(100.0 * static_cast<double>(copied) / static_cast<double>(total)));
        }
        int copying = Copying;
        if (!state.compare_exchange_strong(copying, Committing))
            return {};
        const bool ok = commit();
        return {ok, ok ? QString() : destination.errorString(), {}};
    };
    if (kind == ExportKind::Copy) {
        QTemporaryFile destination(
            QFileInfo(to).absoluteDir().filePath(QStringLiteral(".snow-shot-trim-copy-XXXXXX")));
        if (!destination.open())
            return {false, destination.errorString(), {}};
        auto result = writeAndCommit(destination, [&] { return destination.rename(to); });
        if (result.ok)
            destination.setAutoRemove(false);
        return result;
    }
    QSaveFile destination(to);
    if (!destination.open(QIODevice::WriteOnly))
        return {false, destination.errorString(), {}};
    return writeAndCommit(destination, [&] { return destination.commit(); });
}
void RecordingTrimSession::finishExport(bool ok, const QString& error, bool canceled) {
    report(ok         ? QStringLiteral("recording.trim_export_succeeded")
           : canceled ? QStringLiteral("recording.trim_export_canceled")
                      : QStringLiteral("recording.trim_export_failed"),
           !ok && !canceled && error.isEmpty()
               ? QStringLiteral("Recording export failed without a native error message")
               : error,
           !ok && !canceled ? QtWarningMsg : QtInfoMsg);
    const auto kind = m_request->kind;
    const QString completedPath = m_request->destination;
    if (ok && kind == ExportKind::Copy)
        copyRecording(completedPath);
    m_request.reset();
    m_publication.reset();
    m_phase = Phase::Trimming;
    if (m_dialog) {
        delete m_dialog;
        m_dialog = nullptr;
    }
    setBusy(false);
    if (!ok && !error.isEmpty() && reportError && !m_detached)
        reportError(
            trimText(QT_TRANSLATE_NOOP("RecordingTrimSession", "Unable to export recording: %1"))
                .arg(error));
    if (m_detached)
        releaseMedia();
    if (ok && exported) {
        const auto onExported = exported;
        onExported(completedPath);
    }
}
void RecordingTrimSession::showProgress() {
    if (m_dialog || m_detached)
        return;
    m_dialog = new RecordingRenderDialog(this, m_area ? m_area->screen() : nullptr,
                                         m_area ? m_area->geometry() : QRect(), m_area);
    m_dialog->percent = m_percent;
    m_dialog->stage =
        m_publication ? SNOW_RECORDING_RENDER_STAGE_FINALIZE : SNOW_RECORDING_RENDER_STAGE_RENDER;
    m_dialog->cancel = [this] {
        if (m_dialog->cancelRequested)
            return;
        m_dialog->cancelRequested = true;
        m_dialog->refresh();
        if (m_publication) {
            int copying = Publication::Copying;
            m_publication->state.compare_exchange_strong(copying, Publication::Canceled);
        }
        if (m_export && !m_cancelFuture.valid())
            m_cancelFuture = std::async(std::launch::async,
                                        snow_shot::runtime::trackRuntimeWork([task = m_export] {
                                            snow_recording_clip_export_cancel(task);
                                        }));
    };
    m_dialog->refresh();
    m_dialog->modal->open();
}
void RecordingTrimSession::detach() {
    m_detached = true;
    if (m_saveDialog)
        m_saveDialog->reject();
    if (m_panel)
        m_panel->setPlaybackAvailable(false);
    if (m_toolbar)
        m_toolbar->palette()->setRecordingTrimPanel(nullptr, false);
    if (m_panel)
        m_panel->deleteLater();
    if (m_dialog) {
        delete m_dialog;
        m_dialog = nullptr;
    }
    m_area = nullptr;
    m_toolbar = nullptr;
    m_panel = nullptr;
    reportError = {};
    // Export completion still reports its durable file after the windows close.
    if (!m_export && !m_publishFuture.valid() && !m_openFuture.valid())
        releaseMedia();
}
void RecordingTrimSession::prepareForSessionEnd() {
    m_timer.stop();
    reportError = {};
    exported = {};
    m_detached = true;
    if (m_saveDialog)
        m_saveDialog->reject();
    if (m_dialog)
        m_dialog->cancel = {};
    // Keep pending native open/export/publication owners alive until process exit. Their
    // destructors may cancel work or join, which is unsuitable during session termination.
    setParent(nullptr);
}
QString RecordingTrimSession::sourcePath() const {
    return m_sourcePath;
}
QString RecordingTrimSession::outputPath() const {
    return m_request && !m_request->destination.isEmpty() ? m_request->destination : m_outputPath;
}
void RecordingTrimSession::releaseMedia() {
    m_timer.stop();
    deleteLater();
}

void RecordingTrimSession::report(const QString& event, const QString& message, QtMsgType level) {
    const bool exporting = m_request.has_value();
    const Selection selection =
        exporting ? m_request->selection : Selection{m_first, m_end, m_speedPercent};
    const auto boundary = [this](quint64 frame) {
        return m_phase == Phase::Trimming && m_clip ? snow_recording_clip_boundary(m_clip, frame)
                                                    : 0;
    };
    snow_shot::diagnostics::DiagnosticsService::instance().record(
        level, QStringLiteral("snow_shot.recording"), event, message,
        {{QStringLiteral("operation"), m_operation},
         {QStringLiteral("export_attempt"), m_exportAttempt},
         {QStringLiteral("stage"), m_publication ? QStringLiteral("publish") : phase()},
         {QStringLiteral("action"),
          exporting ? (m_request->kind == ExportKind::Copy ? QStringLiteral("copy")
                                                           : QStringLiteral("save"))
                    : (m_saveWhenReady ? QStringLiteral("save") : QStringLiteral("preview"))},
         {QStringLiteral("deferred"), m_deferred},
         {QStringLiteral("detached"), m_detached},
         {QStringLiteral("cache_hit"), exporting && m_request->cacheHit},
         {QStringLiteral("width"), static_cast<int>(m_info.width)},
         {QStringLiteral("height"), static_cast<int>(m_info.height)},
         {QStringLiteral("fps"), static_cast<int>(m_options.fps)},
         {QStringLiteral("format"), static_cast<int>(m_options.format)},
         {QStringLiteral("codec"), static_cast<int>(m_options.codec)},
         {QStringLiteral("hardware"), m_options.hardware != 0},
         {QStringLiteral("quality"), static_cast<int>(m_options.quality)},
         {QStringLiteral("encoding_preset"), static_cast<int>(m_options.preset)},
         {QStringLiteral("first_frame"), static_cast<qint64>(selection.first)},
         {QStringLiteral("end_frame"), static_cast<qint64>(selection.end)},
         {QStringLiteral("start_us"),
          static_cast<qint64>(exporting ? m_request->startUs : boundary(selection.first))},
         {QStringLiteral("end_us"),
          static_cast<qint64>(exporting ? m_request->endUs : boundary(selection.end))},
         {QStringLiteral("speed_percent"), static_cast<int>(selection.speedPercent)},
         {QStringLiteral("worker_ms"),
          exporting && m_exportTimer.isValid() ? m_exportTimer.elapsed() : 0}});
}
