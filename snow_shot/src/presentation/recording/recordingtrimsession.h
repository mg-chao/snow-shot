#ifndef SNOW_SHOT_RECORDINGTRIMSESSION_H
#define SNOW_SHOT_RECORDINGTRIMSESSION_H

#include "snow_recording.h"
#include "snow_shot/runtime/runtimeactivitytracker.h"
#include "snow_shot/storage/screenshotexportvariables.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <future>
#include <functional>
#include <atomic>
#include <memory>
#include <optional>

class ScreenRecordingAreaWindow;
class ScreenRecordingToolbarWindow;
class RecordingTrimToolbar;
class RecordingRenderDialog;
class QFileDialog;
class QTemporaryDir;

// The media lifetime is independent of the two recording windows. Native work never joins on UI.
class RecordingTrimSession final : public QObject {
  public:
    RecordingTrimSession(ScreenRecordingAreaWindow* area, ScreenRecordingToolbarWindow* toolbar,
                         QObject* parent);
    ~RecordingTrimSession() override;
    void open(const QString& sourcePath, const QString& outputPath,
              const SnowRecordingClipOptions& options, bool deferred, bool saveWhenReady,
              snow_shot::storage::ScreenshotExportContext exportContext);
    void exportClip(bool save);
    void detach();
    void prepareForSessionEnd();
    [[nodiscard]] QString sourcePath() const;
    [[nodiscard]] QString outputPath() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] QString phase() const;
    std::function<void(const QString&)> reportError;
    std::function<void(const QString&)> exported;

  private:
    enum class Phase { Preparing, Trimming, Exporting };
    Phase m_phase = Phase::Preparing;
    struct OpenResult {
        SnowRecordingClip* clip = nullptr;
        QString error;
    };
    struct PublishResult {
        bool ok = false;
        QString error;
        std::unique_ptr<QTemporaryDir> retainedDirectory;
    };
    struct Selection {
        quint64 first = 0;
        quint64 end = 0;
        quint32 speedPercent = 100;
        bool operator==(const Selection&) const = default;
    };
    struct ExportFile {
        QString path;
        std::unique_ptr<QTemporaryDir> temporaryDirectory;
    };
    struct CachedClip {
        Selection selection;
        ExportFile file;
    };
    enum class ExportKind { Save, Copy };
    struct ExportRequest {
        ExportKind kind;
        Selection selection;
        QString destination;
        ExportFile encoding;
        quint64 startUs;
        quint64 endUs;
        bool cacheHit = false;
    };
    struct Publication {
        // Cancellation and final replacement have a single, nonblocking winner.
        enum State { Copying, Canceled, Committing };
        std::atomic<int> state{Copying};
        std::atomic<int> percent{0};
        PublishResult write(const QString& from, const QString& to, ExportKind kind);
    };
    std::shared_ptr<Publication> m_publication;
    void poll();
    void seek(int frame, bool play);
    void togglePlayback();
    void setSpeed(quint32 percent);
    void setBusy(bool busy);
    void finishExport(bool ok, const QString& error, bool canceled = false);
    void chooseSavePath();
    void beginExport(ExportKind kind, QString destination = {});
    void publish(const ExportFile& input);
    void retireExportTask(bool succeeded);
    void releaseMedia();
    void showProgress();
    void report(const QString& event, const QString& message = {}, QtMsgType level = QtInfoMsg);
    QPointer<ScreenRecordingAreaWindow> m_area;
    QPointer<ScreenRecordingToolbarWindow> m_toolbar;
    QPointer<RecordingTrimToolbar> m_panel;
    QTimer m_timer;
    std::future<OpenResult> m_openFuture;
    std::future<PublishResult> m_publishFuture;
    std::future<void> m_cancelFuture;
    SnowRecordingClip* m_clip = nullptr;
    snow_shot::runtime::RuntimeActivityLease m_mediaActivity;
    SnowRecordingClipExport* m_export = nullptr;
    SnowRecordingClipInfo m_info{};
    SnowRecordingClipOptions m_options{};
    QString m_operation;
    QElapsedTimer m_exportTimer;
    int m_exportAttempt = 0;
    quint64 m_revision = 0;
    quint64 m_position = 0;
    quint64 m_first = 0;
    quint64 m_end = 0;
    quint32 m_speedPercent = 100;
    QString m_sourcePath;
    QString m_outputPath;
    snow_shot::storage::ScreenshotExportContext m_exportContext;
    std::optional<CachedClip> m_cachedClip;
    std::optional<ExportRequest> m_request;
    bool m_deferred = false;
    bool m_busy = true;
    bool m_detached = false;
    bool m_saveWhenReady = false;
    bool m_previewFailed = false;
    bool m_playing = false;
    QPointer<QFileDialog> m_saveDialog;
    QPointer<RecordingRenderDialog> m_dialog;
    double m_percent = 0;
};

#endif
