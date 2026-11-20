#include "snow_shot/presentation/screenshotclouduploadservice.h"
#include "snow_shot/runtime/runtimeactivitytracker.h"
#include "snow_shot/network/s3uploadprotocol.h"
#include "snow_shot/presentation/screenshotencodingsettings.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/storage/exportfilename.h"
#include <QCoreApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QXmlStreamReader>
#include <QtConcurrent>
#include <atomic>
#include <algorithm>
#include <utility>

namespace {
QNetworkAccessManager* sharedNetworkAccessManager() {
    auto* application = QCoreApplication::instance();
    if (application == nullptr || QThread::currentThread() != application->thread())
        return nullptr;

    const auto managerName = QStringLiteral("snow-shot-cloud-upload-network-manager");
    auto* manager =
        application->findChild<QNetworkAccessManager*>(managerName, Qt::FindDirectChildrenOnly);
    if (manager == nullptr) {
        manager = new QNetworkAccessManager(application);
        manager->setObjectName(managerName);
    }
    return manager;
}

struct Payload {
    ScreenshotEncodedExport file;
    std::shared_ptr<QIODevice> reader;
    std::atomic_bool cancelled = false;
};
QByteArray contentType(ScreenshotImageFileFormat format) {
    switch (format) {
    case ScreenshotImageFileFormat::Png:
        return "image/png";
    case ScreenshotImageFileFormat::Jpeg:
        return "image/jpeg";
    case ScreenshotImageFileFormat::Webp:
        return "image/webp";
    case ScreenshotImageFileFormat::Jxl:
        return "image/jxl";
    case ScreenshotImageFileFormat::Avif:
        return "image/avif";
    case ScreenshotImageFileFormat::Bmp:
        return "image/bmp";
    case ScreenshotImageFileFormat::Pdf:
        return "application/pdf";
    case ScreenshotImageFileFormat::Tiff:
        return "image/tiff";
    }
    return "application/octet-stream";
}
} // namespace
struct ScreenshotCloudUploadJob::Impl {
    snow_shot::runtime::RuntimeActivityLease activity =
        snow_shot::runtime::RuntimeActivityTracker::shared().acquire();
    std::shared_ptr<ScreenshotExportArtifact> artifact;
    snow_shot::CloudUploadConfiguration config;
    ScreenshotCloudUploadOptions options;
    std::shared_ptr<Payload> payload = std::make_shared<Payload>();
    QPointer<QNetworkReply> reply;
    QString key;
    bool terminal = false;
    bool cancelSourceOnAbort = true;
};
ScreenshotCloudUploadJob::ScreenshotCloudUploadJob(
    std::shared_ptr<ScreenshotExportArtifact> artifact, snow_shot::CloudUploadConfiguration config,
    ScreenshotCloudUploadOptions options, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->artifact = std::move(artifact);
    m_impl->config = std::move(config);
    m_impl->options = std::move(options);
    QTimer::singleShot(0, this, &ScreenshotCloudUploadJob::prepare);
}
ScreenshotCloudUploadJob::~ScreenshotCloudUploadJob() {
    m_impl->terminal = true;
    m_impl->payload->cancelled = true;
    if (m_impl->artifact && m_impl->cancelSourceOnAbort)
        m_impl->artifact->cancel();
    if (m_impl->reply)
        m_impl->reply->abort();
}
QString ScreenshotCloudUploadJob::preparedPath() const {
    return m_impl->payload->file.path;
}
void ScreenshotCloudUploadJob::cancel() {
    if (m_impl->terminal)
        return;
    m_impl->payload->cancelled = true;
    if (m_impl->artifact && m_impl->cancelSourceOnAbort)
        m_impl->artifact->cancel();
    const auto reply = m_impl->reply;
    complete({ScreenshotCloudUploadResult::Status::Cancelled, {}, {}});
    if (reply)
        reply->abort();
}
void ScreenshotCloudUploadJob::complete(ScreenshotCloudUploadResult result) {
    if (std::exchange(m_impl->terminal, true))
        return;
    const auto activity = std::move(m_impl->activity);
    m_impl->artifact.reset();
    const QPointer<ScreenshotCloudUploadJob> guard(this);
    emit finished(result);
    if (guard)
        guard->deleteLater();
}
void ScreenshotCloudUploadJob::prepare() {
    if (m_impl->terminal)
        return;
    if ((!m_impl->artifact && !m_impl->payload->reader) ||
        !snow_shot::cloudUploadConfigurationUsable(m_impl->config)) {
        complete({ScreenshotCloudUploadResult::Status::Failed,
                  {},
                  tr("The cloud upload configuration or image is unavailable")});
        return;
    }
    const QString filename = snow_shot::storage::composeExportFileName(
        ScreenshotImageFileService::suggestedBaseName(m_impl->options.filenameFormat),
        u'-' + QUuid::createUuid().toString(QUuid::WithoutBraces) + u'.' +
            ScreenshotImageFileService::extension(m_impl->options.format));
    QString prefix = m_impl->config.keyPrefix;
    while (prefix.startsWith(u'/'))
        prefix.remove(0, 1);
    while (prefix.endsWith(u'/'))
        prefix.chop(1);
    m_impl->key = prefix.isEmpty() ? filename : prefix + u'/' + filename;
    if (m_impl->payload->reader) {
        readyToUpload();
        return;
    }
    auto directory = std::make_shared<QTemporaryDir>();
    if (!directory->isValid()) {
        complete({ScreenshotCloudUploadResult::Status::Failed, {}, directory->errorString()});
        return;
    }
    m_impl->payload->file = {
        directory->filePath(filename), m_impl->options.format, {}, -1, directory};
    const bool started = m_impl->artifact->requestSaveToPath(
        this, m_impl->payload->file.path, m_impl->options.format, m_impl->options.encoding,
        [this](ScreenshotExportTaskResult result) {
            if (m_impl->terminal)
                return;
            if (!result.succeeded()) {
                complete({ScreenshotCloudUploadResult::Status::Failed, {}, result.error});
                return;
            }
            // Encoding is complete. Only the temporary file is needed for hashing and upload.
            m_impl->artifact.reset();
            auto& file = m_impl->payload->file;
            file.sha256 = std::move(result.encodedSha256);
            file.byteCount = result.encodedByteCount;
            m_impl->payload->reader = ScreenshotImageFileService::openExportReader(file.path);
            readyToUpload();
        },
        m_impl->options.pdf, m_impl->payload);
    if (!started)
        complete({ScreenshotCloudUploadResult::Status::Failed,
                  {},
                  tr("The screenshot export queue is full")});
}
void ScreenshotCloudUploadJob::readyToUpload() {
    const auto payload = m_impl->payload;
    if (!payload->reader || !payload->reader->isOpen() || payload->reader->size() <= 0 ||
        (payload->file.byteCount >= 0 && payload->file.byteCount != payload->reader->size())) {
        complete({ScreenshotCloudUploadResult::Status::Failed,
                  {},
                  tr("The image could not be opened for upload")});
        return;
    }
    const QPointer<ScreenshotCloudUploadJob> guard(this);
    emit progress(0);
    if (!guard || m_impl->terminal)
        return;
    if (!payload->file.sha256.isEmpty()) {
        upload(payload->file.sha256);
        return;
    }
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher] {
        const auto hash = watcher->result();
        watcher->deleteLater();
        if (m_impl->terminal)
            return;
        if (hash.isEmpty()) {
            complete({ScreenshotCloudUploadResult::Status::Failed,
                      {},
                      tr("The image could not be prepared for upload")});
            return;
        }
        m_impl->payload->file.sha256 = hash;
        upload(hash);
    });
    watcher->setFuture(QtConcurrent::run(snow_shot::runtime::trackRuntimeWork([payload] {
        auto& input = *payload->reader;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        while (!input.atEnd()) {
            if (payload->cancelled)
                return QByteArray{};
            const auto count = input.read(buffer.data(), buffer.size());
            if (count <= 0)
                return QByteArray{};
            hash.addData(QByteArrayView(buffer.constData(), count));
        }
        return input.seek(0) ? hash.result().toHex() : QByteArray{};
    })));
}

void ScreenshotCloudUploadJob::upload(const QByteArray& hash) {
    auto* manager = sharedNetworkAccessManager();
    if (manager == nullptr) {
        complete({ScreenshotCloudUploadResult::Status::Failed,
                  {},
                  tr("Cloud upload failed because of a network error or timeout")});
        return;
    }
    const auto file = m_impl->payload->reader;
    auto request = snow_shot::s3_upload::signedPut(m_impl->config, m_impl->key, hash,
                                                   contentType(m_impl->options.format),
                                                   QDateTime::currentDateTimeUtc());
    request.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
    request.setTransferTimeout(m_impl->options.transferTimeoutMs);
    auto* reply = manager->put(request, file.get());
    // The network backend may still read during abort or source destruction.
    connect(reply, &QObject::destroyed, [payload = m_impl->payload] {});
    m_impl->reply = reply;
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (!m_impl->terminal && total > 0)
            emit progress(static_cast<int>(sent * 100 / total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_impl->terminal)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            complete({ScreenshotCloudUploadResult::Status::Succeeded,
                      snow_shot::s3_upload::objectUrl(m_impl->config, m_impl->key, true),
                      {},
                      m_impl->payload->file});
        } else {
            // Only expose an S3 error code, never response messages, URLs or signed headers.
            QString code;
            QXmlStreamReader xml(reply->isReadable() ? reply->read(64 * 1024) : QByteArray{});
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1StringView("Code")) {
                    const auto candidate = xml.readElementText().left(80);
                    if (std::all_of(candidate.cbegin(), candidate.cend(), [](QChar c) {
                            return c.isLetterOrNumber() && c.unicode() < 128;
                        }))
                        code = candidate;
                    break;
                }
            }
            const QString error =
                status > 0 ? tr("Cloud upload failed (HTTP %1%2)")
                                 .arg(status)
                                 .arg(code.isEmpty() ? QString() : QStringLiteral(", ") + code)
                           : tr("Cloud upload failed because of a network error or timeout");
            complete({ScreenshotCloudUploadResult::Status::Failed, {}, error});
        }
    });
}
ScreenshotCloudUploadJob*
ScreenshotCloudUploadService::upload(std::shared_ptr<ScreenshotExportArtifact> artifact,
                                     snow_shot::CloudUploadConfiguration config,
                                     ScreenshotCloudUploadOptions options, QObject* receiver,
                                     Completion completion, bool cancelSourceOnAbort) {
    if (!receiver)
        return nullptr;
    auto* job = new ScreenshotCloudUploadJob(std::move(artifact), std::move(config),
                                             std::move(options), receiver);
    job->m_impl->cancelSourceOnAbort = cancelSourceOnAbort;
    if (completion)
        QObject::connect(job, &ScreenshotCloudUploadJob::finished, receiver, std::move(completion));
    return job;
}
ScreenshotCloudUploadJob* ScreenshotCloudUploadService::uploadEncoded(
    ScreenshotEncodedExport file, snow_shot::CloudUploadConfiguration config,
    ScreenshotCloudUploadOptions options, QObject* receiver) {
    if (!receiver || !file.available())
        return nullptr;
    options.format = file.format;
    auto* job = new ScreenshotCloudUploadJob({}, std::move(config), std::move(options), receiver);
    const auto payload = job->m_impl->payload;
    payload->file = std::move(file);
    if (payload->file.reader) {
        payload->reader = std::move(payload->file.reader);
    } else if (!payload->file.path.isEmpty()) {
        payload->reader = ScreenshotImageFileService::openExportReader(payload->file.path);
    } else {
        auto buffer = std::make_shared<QBuffer>();
        buffer->setData(payload->file.bytes);
        buffer->open(QIODevice::ReadOnly);
        payload->reader = std::move(buffer);
    }
    return job;
}

ScreenshotCloudUploadOptions ScreenshotCloudUploadService::currentOptions() {
    const snow_shot::storage::ScreenshotSettings settings;
    return {ScreenshotImageFileService::formatForKey(settings.imageFormat()),
            snow_shot::presentation::screenshotEncodingOptions(settings),
            ScreenshotPdfOptions{screenshot_pdf::pageSizeForKey(settings.pdfPageSize())},
            settings.autoSaveFilenameFormat()};
}
std::optional<snow_shot::CloudUploadConfiguration> ScreenshotCloudUploadService::configuration() {
    const auto settings = snow_shot::storage::CloudUploadConfigurationSettings().settings();
    const auto* value = settings.selected();
    return value && snow_shot::cloudUploadConfigurationUsable(*value) ? std::optional(*value)
                                                                      : std::nullopt;
}
