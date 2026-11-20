// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/screenshotexporttasks.h"
#include "snow_shot/storage/settingsadapters.h"
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace {
using Task = ScreenshotExportTasks::Task;
using Trigger = ScreenshotExportTasks::Trigger;
using Artifact = ScreenshotExportTasks::Artifact;

void publishImage(QObject* owner, Artifact image, const ScreenshotExportTasks& tasks) {
    const auto id = tasks.clipboardPublication;
    const auto report = tasks.reportError;
    const auto prepared = [owner, image, id, report](ScreenshotExportClipboardResult result) {
        if (ScreenshotClipboardService::publicationSuperseded(id))
            return;
        if (!result.succeeded()) {
            if (report)
                report(result.error);
            return;
        }
        static_cast<void>(ScreenshotClipboardService::commit(
            QGuiApplication::clipboard(), owner, std::move(result.payload), id,
            [report](ScreenshotClipboardCommitResult commit) {
                if (!commit.succeeded() && report)
                    report(commit.errorString());
            }));
    };
    const auto& options = tasks.options;
    const bool started = tasks.clipboardFile
                             ? image->requestClipboardFile(owner, options.format,
                                                           options.filenameFormat, options.encoding,
                                                           prepared, options.pdf, tasks.requestedAt)
                             : image->requestClipboard(owner, prepared);
    if (!started && report)
        report(QCoreApplication::translate("ScreenshotExportArtifact",
                                           "The screenshot export queue is full"));
}

void saveEncodedExport(QObject* owner, ScreenshotEncodedExport file,
                       const ScreenshotExportTasks& tasks,
                       ScreenshotExportCoordinator::Completion done) {
    const auto job = ScreenshotExportCoordinator::shared().submit(
        owner, ScreenshotExportCoordinator::Priority::Background,
        [file = std::move(file), directory = tasks.directory, name = tasks.options.filenameFormat,
         at = tasks.requestedAt,
         names = tasks.windowNames](const ScreenshotExportCancellation& cancel) {
            const auto cancelled = [&cancel] { return cancel.isCancellationRequested(); };
            const auto directories =
                ScreenshotImageFileService::automaticDirectories(directory, at, names);
            const auto result =
                file.path.isEmpty()
                    ? ScreenshotImageFileService::saveEncodedBytesAutomatically(
                          file.bytes, directories, file.format, name, at, cancelled, names)
                    : ScreenshotImageFileService::saveEncodedAutomatically(
                          file.path, directories, file.format, name, at, cancelled, names, false,
                          file.sha256);
            if (!result.succeeded())
                return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::File,
                                                           result.error);
            ScreenshotExportTaskResult saved;
            saved.savedPath = result.path;
            saved.encodedSha256 = result.encodedSha256;
            saved.encodedByteCount = result.encodedByteCount;
            return saved;
        },
        done);
    if (!job.isValid())
        done(ScreenshotExportTaskResult::failure(
            ScreenshotExportFailureStage::Queue,
            QCoreApplication::translate("ScreenshotExportArtifact",
                                        "The screenshot export queue is full")));
}
} // namespace

ScreenshotExportTasks ScreenshotExportTasks::capture(Trigger trigger,
                                                     snow_shot::storage::ExportTasks excluded) {
    ScreenshotExportTasks result;
    result.trigger = trigger;
    const snow_shot::storage::ScreenshotSettings settings;
    const auto allowed = snow_shot::storage::exportTaskRow(trigger).allowed & ~excluded;
    if (allowed)
        result.selected = settings.exportTasks(trigger) & allowed;
    if (result.selected.testFlag(Task::Upload)) {
        result.cloud = ScreenshotCloudUploadService::configuration();
        result.selected.setFlag(Task::Upload, result.cloud.has_value());
    }
    result.clipboardFile = (result.selected.testFlag(Task::CopyImage) ||
                            trigger == Trigger::ScreenshotCopy || trigger == Trigger::PinnedCopy) &&
                           settings.copyImageFileToClipboard();
    if (result.selected.testFlag(Task::AutoSave))
        result.directory = settings.imageSaveDirectory();
    if (result.clipboardFile || result.selected.testFlag(Task::AutoSave) || result.cloud) {
        result.options = ScreenshotCloudUploadService::currentOptions();
        result.requestedAt = QDateTime::currentDateTime();
    }
    result.deferClipboardPublication();
    return result;
}

void ScreenshotExportTasks::deferClipboardPublication() {
    if (selected & (Task::CopyImage | Task::CopyFile))
        clipboardPublication = ScreenshotClipboardService::deferPublication();
}

void ScreenshotExportTasks::run(QObject* owner, Artifact image, ScreenshotEncodedExport file,
                                QImage submittedPrint) const {
    const QPointer<QObject> guard(owner);
    if (!guard || !selected)
        return;
    if (image)
        image->setWindowNames(windowNames);
    const auto upload = [guard, config = cloud, options = options](
                            const Artifact& source, ScreenshotEncodedExport encoded) {
        if (!guard || !config)
            return;
        if (encoded.available())
            static_cast<void>(ScreenshotCloudUploadService::uploadEncoded(std::move(encoded),
                                                                          *config, options, guard));
        else if (source)
            static_cast<void>(
                ScreenshotCloudUploadService::upload(source, *config, options, guard, {}, false));
    };
    const bool save = selected.testFlag(Task::AutoSave);
    const bool shouldUpload = selected.testFlag(Task::Upload) && cloud.has_value();
    const bool uploadSaved = shouldUpload && save && !file.available();
    if (shouldUpload && !uploadSaved) {
        if (trigger == Trigger::ScreenshotPrint || trigger == Trigger::PinnedPrint) {
            if (!submittedPrint.isNull())
                upload(std::make_shared<ScreenshotExportArtifact>(
                           ScreenshotExportSource::fromImage(std::move(submittedPrint))),
                       {});
        } else if ((trigger != Trigger::ScreenshotSave && trigger != Trigger::PinnedSave) ||
                   file.reader) {
            // A retained reader identifies this particular write, even after the path is reused.
            upload(image, file);
        }
    }
    if (save) {
        const auto saved = [guard, image, upload, uploadSaved, format = options.format,
                            report = reportError](ScreenshotExportTaskResult result) {
            if (!guard)
                return;
            if (uploadSaved)
                upload(image, result.reader ? ScreenshotEncodedExport::fromSave(result, format)
                                            : ScreenshotEncodedExport{});
            if (guard && !result.succeeded() &&
                result.failureStage != ScreenshotExportFailureStage::Cancelled && report)
                report(QCoreApplication::translate("ScreenshotExportArtifact",
                                                   "Automatic screenshot saving failed: %1")
                           .arg(result.error));
        };
        // A primary upload or an image-and-file copy already produced the requested bytes.
        if (file.available() && trigger != Trigger::ScreenshotAirDrop &&
            trigger != Trigger::PinnedAirDrop)
            saveEncodedExport(owner, file, *this, saved);
        else if (!image || !image->requestAutomaticSaveToConfiguredDirectory(
                               owner, directory, options.format, options.filenameFormat,
                               options.encoding, saved, options.pdf, requestedAt, uploadSaved))
            saved(ScreenshotExportTaskResult::failure(
                ScreenshotExportFailureStage::Queue,
                QCoreApplication::translate("ScreenshotExportArtifact",
                                            "The screenshot export queue is full")));
    }
    const bool publish = guard && clipboardPublication &&
                         !ScreenshotClipboardService::publicationSuperseded(clipboardPublication);
    if (publish && selected.testFlag(Task::CopyImage) && image)
        publishImage(guard, image, *this);
    if (publish && guard && selected.testFlag(Task::CopyFile) && !file.path.isEmpty()) {
        auto* mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(file.path)});
        static_cast<void>(ScreenshotClipboardService::commitMimeData(
            QGuiApplication::clipboard(), guard, mime, clipboardPublication,
            [report = reportError](auto result) {
                if (!result.succeeded() && report)
                    report(result.errorString());
            }));
    }
    if (guard && selected.testFlag(Task::Pin) && pin && (pinSource || image))
        pin(pinSource ? pinSource : std::move(image));
}
