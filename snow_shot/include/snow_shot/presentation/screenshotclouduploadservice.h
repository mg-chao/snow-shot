#ifndef SNOW_SHOT_SCREENSHOTCLOUDUPLOADSERVICE_H
#define SNOW_SHOT_SCREENSHOTCLOUDUPLOADSERVICE_H

#include "snow_shot/clouduploadconfiguration.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include <QPointer>

struct ScreenshotCloudUploadOptions {
    ScreenshotImageFileFormat format = ScreenshotImageFileFormat::Png;
    ScreenshotImageEncodingOptions encoding;
    ScreenshotPdfOptions pdf;
    QString filenameFormat;
    int transferTimeoutMs = 60000;
};
// A completed encoding. Temporary exports retain their directory through every reader.
struct ScreenshotEncodedExport {
    QString path;
    ScreenshotImageFileFormat format = ScreenshotImageFileFormat::Png;
    QByteArray sha256{};
    qint64 byteCount = -1;
    std::shared_ptr<void> lifetime{};
    std::shared_ptr<QFile> reader{};
    QByteArray bytes{};

    [[nodiscard]] bool available() const {
        return !path.isEmpty() || !bytes.isEmpty();
    }
    static ScreenshotEncodedExport fromSave(const ScreenshotImageFileSaveResult& result,
                                            ScreenshotImageFileFormat format) {
        return {result.path, format,       result.encodedSha256, result.encodedByteCount,
                {},          result.reader};
    }
    static ScreenshotEncodedExport fromSave(const ScreenshotExportTaskResult& result,
                                            ScreenshotImageFileFormat format) {
        return {result.savedPath,        format, result.encodedSha256,
                result.encodedByteCount, {},     result.reader};
    }
    static ScreenshotEncodedExport fromClipboard(const ScreenshotExportClipboardResult& result,
                                                 ScreenshotImageFileFormat format, bool reusePng) {
        return {result.payload.filePath(),
                format,
                result.fileSha256,
                result.fileByteCount,
                {},
                {},
                reusePng && result.payload.filePath().isEmpty() ? result.payload.pngBytes()
                                                                : QByteArray{}};
    }
};

struct ScreenshotCloudUploadResult {
    enum class Status { Succeeded, Failed, Cancelled };
    Status status = Status::Failed;
    QUrl url;
    QString error;
    ScreenshotEncodedExport file{};
};
class ScreenshotCloudUploadJob final : public QObject {
    Q_OBJECT
  public:
    ~ScreenshotCloudUploadJob() override;
    void cancel();
    [[nodiscard]] QString preparedPath() const;
  signals:
    void progress(int percent);
    void finished(const ScreenshotCloudUploadResult& result);

  private:
    friend class ScreenshotCloudUploadService;
    ScreenshotCloudUploadJob(std::shared_ptr<ScreenshotExportArtifact> artifact,
                             snow_shot::CloudUploadConfiguration config,
                             ScreenshotCloudUploadOptions options, QObject* parent);
    void prepare();
    void readyToUpload();
    void upload(const QByteArray& hash);
    void complete(ScreenshotCloudUploadResult result);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
class ScreenshotCloudUploadService final {
  public:
    using Completion = std::function<void(const ScreenshotCloudUploadResult&)>;
    // The receiver owns the job. It cancels on destruction and deletes itself after completion.
    [[nodiscard]] static ScreenshotCloudUploadJob*
    upload(std::shared_ptr<ScreenshotExportArtifact> artifact,
           snow_shot::CloudUploadConfiguration config, ScreenshotCloudUploadOptions options,
           QObject* receiver, Completion completion = {}, bool cancelSourceOnAbort = true);
    [[nodiscard]] static ScreenshotCloudUploadJob*
    uploadEncoded(ScreenshotEncodedExport file, snow_shot::CloudUploadConfiguration config,
                  ScreenshotCloudUploadOptions options, QObject* receiver);
    [[nodiscard]] static ScreenshotCloudUploadOptions currentOptions();
    [[nodiscard]] static std::optional<snow_shot::CloudUploadConfiguration> configuration();
};
#endif
