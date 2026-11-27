// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageeditingexportcontroller.h"
#include "../services/screenshotprintinteractionguard.h"
#include "snow_shot/presentation/screenshotairdropservice.h"
#include "snow_shot/presentation/screenshotclouduploadservice.h"
#include "snow_shot/presentation/screenshotmanualsave.h"
#include "snow_shot/presentation/screenshotprintservice.h"
#include "snow_shot/presentation/screenshotsaveasfiledialog.h"
#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <utility>

namespace snow_shot::presentation {
namespace {
using Operation = ImageEditingExportController::Operation;
struct Request final : QObject {
    Request(QObject* parent, Operation operation, ImageEditingExportController::Completion done)
        : QObject(parent), operation(operation), completion(std::move(done)) {}
    Operation operation;
    ImageEditingExportController::Completion completion;
    ScreenshotExportJobHandle job;
    ScreenshotManualSaveHandle save;
    ScreenshotClipboardCommitHandle clipboard;
    std::shared_ptr<ScreenshotExportArtifact> artifact;
    QPointer<ScreenshotCloudUploadJob> upload;
    QPointer<QFileDialog> dialog;
    QPointer<QMessageBox> overwrite;
    QPointer<QWidget> owner;
    std::unique_ptr<ScreenshotPrintInteractionGuard> interaction;
    bool destinationChosen = false;
};
#if SNOW_SHOT_ENABLE_IMAGE_CONVERSION || SNOW_SHOT_ENABLE_LATEX_RECOGNITION ||                     \
    SNOW_SHOT_ENABLE_QR_RECOGNITION || SNOW_SHOT_ENABLE_TABLE_RECOGNITION
struct Destination {
    QString directory;
    QString name;
};
Destination destination(Operation operation) {
    const storage::ScreenshotSettings settings;
    const auto timestamp = QDateTime::currentDateTime();
    const bool quick = operation == Operation::QuickSave;
    return {
        quick ? storage::expandScreenshotExportDirectory(settings.imageSaveDirectory(), timestamp)
              : ScreenshotImageFileService::saveDialogDirectory(settings.lastManualSaveDirectory(),
                                                                settings.imageSaveDirectory()),
        ScreenshotImageFileService::suggestedBaseName(quick ? settings.autoSaveFilenameFormat()
                                                            : settings.manualSaveFilenameFormat(),
                                                      timestamp)};
}
#endif
} // namespace

struct ImageEditingExportController::Impl {
    ImageEditingExportController& q;
    QPointer<ScreenshotPrintService> printing;
    QPointer<ScreenshotAirDropService> airDrop;
    Pin pin;
    QPointer<Request> active;
    QPointer<Request> copy;

    QPointer<Request>& activeRequest(Operation operation) {
        return operation == Operation::Copy ? copy : active;
    }

    bool current(const QPointer<Request>& request) {
        return request && activeRequest(request->operation) == request;
    }

    static void cancelRequest(QPointer<Request>& pending);

    Request* begin(Operation operation, QWidget* owner, Completion completion) {
        auto* request = new Request(&q, operation, std::move(completion));
        activeRequest(operation) = request;
        request->owner = owner;
        QObject::connect(owner, &QObject::destroyed, request, [this, pending = QPointer(request)] {
            if (current(pending))
                cancelRequest(activeRequest(pending->operation));
        });
        return request;
    }

    void finish(QPointer<Request> request, bool success, QString error = {}) {
        if (!current(request))
            return;
        activeRequest(request->operation).clear();
        request->interaction.reset();
        auto completion = std::move(request->completion);
        // Keep artifacts alive through their delivery callbacks.
        request->deleteLater();
        if (completion)
            completion(success, std::move(error));
    }

    void startFailed(QPointer<Request> request) {
        finish(request, false,
               QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                           "Export could not be started"));
    }

    void publishCopy(QPointer<Request> request, std::unique_ptr<QMimeData> mime,
                     ScreenshotClipboardService::PublicationId publication) {
        const QPointer<ImageEditingExportController> lifetime(&q);
        auto handle = ScreenshotClipboardService::publishMimeData(
            QApplication::clipboard(), request, std::move(mime), publication,
            [this, request](auto result) {
                finish(request, result.published(), result.errorString());
            });
        if (!lifetime || !current(request))
            return;
        if (handle.isValid())
            request->clipboard = std::move(handle);
        else
            startFailed(request);
    }

    void copyImage(QPointer<Request> request, ScreenshotExportSource source,
                   const CopyOptions& options) {
        request->artifact = std::make_shared<ScreenshotExportArtifact>(std::move(source));
        const auto artifact = request->artifact;
        const auto prepared = [this, request, publication = options.publication](auto result) {
            if (!current(request))
                return;
            if (!ScreenshotClipboardService::publicationCurrent(publication)) {
                finish(request, false);
                return;
            }
            if (!result.succeeded()) {
                finish(request, false, result.error);
                return;
            }
            request->clipboard = ScreenshotClipboardService::commit(
                QApplication::clipboard(), request, std::move(result.payload), publication,
                [this, request](auto committed) {
                    finish(request, committed.published(), committed.errorString());
                });
            if (!request->clipboard.isValid())
                startFailed(request);
        };
        const auto& file = options.file;
        const bool started = file ? artifact->requestClipboardFile(
                                        request, file->format, file->filenameFormat, file->encoding,
                                        prepared, file->pdf, options.requestedAt)
                                  : artifact->requestClipboard(request, prepared);
        if (!started)
            startFailed(request);
    }
    void submit(QPointer<Request> request, ScreenshotExportCoordinator::Work work,
                Operation operation, QString path) {
        request->job = ScreenshotExportCoordinator::shared().submit(
            request, ScreenshotExportCoordinator::Priority::Foreground, std::move(work),
            [this, request, operation, path = std::move(path)](auto result) {
                if (!request || active != request)
                    return;
                if (result.succeeded() && operation == Operation::Save)
                    static_cast<void>(storage::ScreenshotSettings().setLastManualSaveDirectory(
                        QFileInfo(path).absolutePath()));
                finish(request, result.succeeded(), result.error);
            });
        if (!request->job.isValid())
            finish(request, false,
                   QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                               "Export could not be started"));
    }
    void confirmOverwrite(QPointer<Request> request, const QStringList& paths,
                          std::function<void()> write) {
        QStringList existing;
        for (const auto& path : paths)
            if (QFileInfo::exists(path))
                existing.append(path);
        if (existing.isEmpty()) {
            write();
            return;
        }
        auto* dialog =
            new QMessageBox(QMessageBox::Question,
                            QCoreApplication::translate("ScreenshotRecognitionFileExport",
                                                        "Replace existing files?"),
                            QCoreApplication::translate("ScreenshotRecognitionFileExport",
                                                        "Replace the existing file(s)?\n%1")
                                .arg(existing.join(QLatin1Char('\n'))),
                            QMessageBox::Yes | QMessageBox::No, request->owner);
        request->overwrite = dialog;
        dialog->setObjectName(QStringLiteral("imageEditingRecognitionOverwriteDialog"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setOption(QMessageBox::Option::DontUseNativeDialog);
        dialog->setWindowModality(Qt::WindowModal);
        dialog->button(QMessageBox::Yes)
            ->setText(QCoreApplication::translate("ScreenshotRecognitionFileExport", "Replace"));
        dialog->button(QMessageBox::No)
            ->setText(QCoreApplication::translate("ScreenshotRecognitionFileExport", "Cancel"));
        dialog->setDefaultButton(QMessageBox::No);
        QObject::connect(dialog, &QDialog::finished, request,
                         [this, request, write = std::move(write)](int result) {
                             if (!request || active != request)
                                 return;
                             request->overwrite.clear();
                             if (result == QMessageBox::Yes)
                                 write();
                             else
                                 finish(request, false);
                         });
        QObject::connect(dialog, &QObject::destroyed, request, [this, request] {
            if (request && !request->job.isValid())
                finish(request, false);
        });
        dialog->show();
    }
#if SNOW_SHOT_ENABLE_IMAGE_CONVERSION || SNOW_SHOT_ENABLE_LATEX_RECOGNITION ||                     \
    SNOW_SHOT_ENABLE_QR_RECOGNITION || SNOW_SHOT_ENABLE_TABLE_RECOGNITION
    void saveRecognition(QPointer<Request> request, Operation operation,
                         ScreenshotRecognitionFileSnapshot snapshot, QWidget* owner) {
        const auto output = destination(operation);
        const auto kind = snapshot.kind;
        const auto write = [this, request, operation, output,
                            snapshot = std::move(snapshot)](QString path) {
            if (!request || active != request)
                return;
            if (operation == Operation::Save)
                path = ScreenshotRecognitionFileExport::normalizedPath(path, snapshot.kind);
            const auto paths = ScreenshotRecognitionFileExport::outputPaths(path, snapshot);
            auto submitSnapshot = [this, request, snapshot, output, path, operation] {
                if (!request || active != request)
                    return;
                submit(
                    request,
                    [snapshot, output, path, operation](const auto& cancel) {
                        const auto cancelled = [&cancel] {
                            return cancel.isCancellationRequested();
                        };
                        if (cancelled())
                            return ScreenshotExportTaskResult::failure(
                                ScreenshotExportFailureStage::Cancelled, {});
                        const auto result =
                            operation == Operation::QuickSave
                                ? ScreenshotRecognitionFileExport::quickSave(
                                      snapshot, output.directory, output.name, cancelled)
                                : ScreenshotRecognitionFileExport::saveToPath(snapshot, path, true,
                                                                              cancelled);
                        return result.succeeded()
                                   ? ScreenshotExportTaskResult{}
                                   : ScreenshotExportTaskResult::failure(
                                         result.wasCancelled
                                             ? ScreenshotExportFailureStage::Cancelled
                                             : ScreenshotExportFailureStage::File,
                                         result.error);
                    },
                    operation, path);
            };
            if (operation == Operation::Save)
                confirmOverwrite(request, paths, std::move(submitSnapshot));
            else
                submitSnapshot();
        };
        if (operation == Operation::QuickSave) {
            write({});
            return;
        }
        auto* dialog = new QFileDialog(
            owner, QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                               "Save as file"));
        request->dialog = dialog;
        dialog->setObjectName(QStringLiteral("imageEditingRecognitionSaveDialog"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setFileMode(QFileDialog::AnyFile);
        dialog->setWindowModality(Qt::WindowModal);
        dialog->setNameFilter(ScreenshotRecognitionFileExport::dialogFilter(kind));
        dialog->setOption(QFileDialog::DontConfirmOverwrite);
        dialog->selectFile(QDir(output.directory)
                               .filePath(output.name + QLatin1Char('.') +
                                         ScreenshotRecognitionFileExport::extension(kind)));
        QObject::connect(dialog, &QDialog::accepted, request, [this, request, dialog, write] {
            if (!request || request->destinationChosen)
                return;
            request->destinationChosen = true;
            const auto paths = dialog->selectedFiles();
            if (paths.isEmpty()) {
                finish(request, false);
                return;
            }
            auto path = paths.first();
#if SNOW_SHOT_ENABLE_DOCUMENT_RECOGNITION
            path = ScreenshotRecognitionFileExport::pathForSelectedFilter(
                path, dialog->selectedNameFilter());
#endif
            // Leave QFileDialog::done() before opening another dialog or invoking client code.
            QMetaObject::invokeMethod(
                request, [write, path] { write(path); }, Qt::QueuedConnection);
        });
        QObject::connect(dialog, &QDialog::rejected, request,
                         [this, request] { finish(request, false); });
        QObject::connect(dialog, &QObject::destroyed, request, [this, request] {
            if (request && !request->destinationChosen)
                finish(request, false);
        });
        dialog->show();
    }
#endif
    void image(QPointer<Request> request, Operation operation, ScreenshotExportSource source,
               QWidget* owner, QWidget* stackingOwner) {
        request->artifact = std::make_shared<ScreenshotExportArtifact>(std::move(source));
        const auto artifact = request->artifact;
        const auto done = [this, request](auto result) {
            finish(request, result.succeeded(), result.error);
        };
        switch (operation) {
        case Operation::Pin: {
            const QPointer<ImageEditingExportController> lifetime(&q);
            if (!artifact->requestImage(request, [lifetime, request](auto result) {
                    if (!lifetime || !request || lifetime->m_impl->active != request)
                        return;
                    [[maybe_unused]] const auto rendering = request->artifact;
                    if (!result.succeeded()) {
                        lifetime->m_impl->finish(request, false, result.error);
                        return;
                    }
                    const auto finish = [lifetime, request](bool success) {
                        if (lifetime && request)
                            lifetime->m_impl->finish(
                                request, success,
                                success ? QString{}
                                        : QCoreApplication::translate(
                                              "snow_shot::presentation::ImageEditingWindow",
                                              "Export could not be started"));
                    };
                    // Keep the callback alive if a synchronous completion destroys the editor.
                    const auto handler = lifetime->m_impl->pin;
                    if (!handler || !handler(std::move(result.image), finish))
                        finish(false);
                })) {
                if (lifetime)
                    lifetime->m_impl->finish(
                        request, false,
                        QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                                    "Export could not be started"));
            }
            break;
        }
        case Operation::Copy:
            Q_UNREACHABLE();
        case Operation::Save:
            if (storage::ScreenshotSettings().saveAsFileDialog() == QStringLiteral("snow_shot")) {
                if (!ScreenshotSaveAsFileDialog::open(
                        request, owner, artifact, {},
                        [this, request](bool saved) { finish(request, saved); }, stackingOwner,
                        Qt::WindowModal))
                    finish(
                        request, false,
                        QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                                    "Export could not be started"));
            } else {
                const auto save = ScreenshotManualSaveService::open(request, owner, artifact, done);
                if (!request || active != request)
                    return;
                request->save = save;
                if (!save.isValid())
                    finish(
                        request, false,
                        QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                                    "Export could not be started"));
            }
            break;
        case Operation::QuickSave:
            if (!artifact->requestQuickSave(request, done))
                finish(request, false,
                       QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                                   "Export could not be started"));
            break;
        case Operation::Print:
            if (!artifact->requestImage(request, [this, request,
                                                  window = QPointer<QWidget>(owner)](auto result) {
                    if (!request || active != request)
                        return;
                    if (!result.succeeded()) {
                        finish(request, false, result.error);
                        return;
                    }
                    auto& service = printing ? *printing : ScreenshotPrintService::shared();
                    if (!service.printImage(
                            request, window, std::move(result.image),
                            [this, request](auto printed) {
                                finish(request,
                                       printed.status == ScreenshotPrintService::Status::Submitted,
                                       printed.error);
                            }))
                        finish(request, false,
                               QCoreApplication::translate(
                                   "snow_shot::presentation::ImageEditingWindow",
                                   "Printing is unavailable"));
                }))
                finish(request, false,
                       QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                                   "Export could not be started"));
            break;
        case Operation::AirDrop: {
            request->interaction = std::make_unique<ScreenshotPrintInteractionGuard>(
                QList<QWidget*>{owner, stackingOwner});
            const QString filename = ScreenshotImageFileService::suggestedBaseName(
                                         storage::ScreenshotSettings().manualSaveFilenameFormat()) +
                                     QStringLiteral(".png");
            using Service = ScreenshotAirDropService;
            if (!artifact->requestImage(request, [this, request, filename](auto result) {
                    if (!current(request))
                        return;
                    if (!result.succeeded()) {
                        finish(request, false, result.error);
                        return;
                    }
                    auto& service = airDrop ? *airDrop : Service::shared();
                    if (!service.shareImage(request, request->owner, std::move(result.image),
                                            filename, [this, request](Service::Result shared) {
                                                finish(request,
                                                       shared.status == Service::Status::Shared,
                                                       shared.error);
                                            })) {
                        finish(request, false,
                               QCoreApplication::translate(
                                   "ScreenshotAirDropService",
                                   "The image could not be prepared for AirDrop"));
                    }
                }))
                startFailed(request);
            break;
        }
        case Operation::Upload: {
            const auto config = ScreenshotCloudUploadService::configuration();
            if (!config) {
                finish(request, false,
                       QCoreApplication::translate(
                           "snow_shot::presentation::ImageEditingWindow",
                           "Configure cloud upload before uploading an image"));
                break;
            }
            request->upload = ScreenshotCloudUploadService::upload(
                artifact, *config, ScreenshotCloudUploadService::currentOptions(), request,
                [this, request](auto result) {
                    if (!request || active != request)
                        return;
                    const QPointer<ImageEditingExportController> lifetime(&q);
                    const bool success =
                        result.status == ScreenshotCloudUploadResult::Status::Succeeded;
                    if (success)
                        QApplication::clipboard()->setText(result.url.toString());
                    if (lifetime)
                        finish(request, success, result.error);
                });
            break;
        }
        }
    }
};

ImageEditingExportController::ImageEditingExportController(ScreenshotPrintService* printing,
                                                           QObject* parent, Pin pin,
                                                           ScreenshotAirDropService* airDrop)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, printing, airDrop, std::move(pin))) {}
ImageEditingExportController::~ImageEditingExportController() {
    cancel();
}
bool ImageEditingExportController::busy() const {
    return m_impl->active || m_impl->copy;
}
bool ImageEditingExportController::canStart(Operation operation) const {
    return !m_impl->active && (operation == Operation::Copy || !m_impl->copy);
}
void ImageEditingExportController::start(Operation operation, Content content, QWidget* owner,
                                         QWidget* stackingOwner, Completion completion) {
    if (operation == Operation::Copy || !canStart(operation) || !owner)
        return;
    auto& d = *m_impl;
    auto* request = d.begin(operation, owner, std::move(completion));
    if (auto* source = std::get_if<ScreenshotExportSource>(&content)) {
        d.image(request, operation, std::move(*source), owner, stackingOwner);
        return;
    }
#if SNOW_SHOT_ENABLE_IMAGE_CONVERSION || SNOW_SHOT_ENABLE_LATEX_RECOGNITION ||                     \
    SNOW_SHOT_ENABLE_QR_RECOGNITION || SNOW_SHOT_ENABLE_TABLE_RECOGNITION
    d.saveRecognition(request, operation,
                      std::get<ScreenshotRecognitionFileSnapshot>(std::move(content)), owner);
#else
    d.startFailed(request);
#endif
}
bool ImageEditingExportController::startCopy(CopyContent content, CopyOptions options,
                                             QWidget* owner, Completion completion) {
    auto& d = *m_impl;
    if (!owner || !canStart(Operation::Copy) || d.copy ||
        !ScreenshotClipboardService::publicationCurrent(options.publication))
        return false;
    const QPointer<Request> request(d.begin(Operation::Copy, owner, std::move(completion)));
    if (auto* source = std::get_if<ScreenshotExportSource>(&content)) {
        d.copyImage(request, std::move(*source), options);
        return true;
    }
    const QPointer<ImageEditingExportController> self(this);
    auto prepared = [self, request, options](auto mime, QString error) {
        if (!self || !self->m_impl->current(request))
            return;
        auto& impl = *self->m_impl;
        if (!mime || !ScreenshotClipboardService::publicationCurrent(options.publication)) {
            impl.finish(request, false, std::move(error));
        } else if (mime->hasImage()) {
            impl.copyImage(
                request,
                ScreenshotExportSource::fromImage(qvariant_cast<QImage>(mime->imageData())),
                options);
        } else {
            impl.publishCopy(request, std::move(mime), options.publication);
        }
    };
    const bool accepted = std::get<PrepareCopy>(content)(request, std::move(prepared));
    if (!accepted && self && d.current(request))
        d.finish(request, false,
                 QCoreApplication::translate("snow_shot::presentation::ImageEditingWindow",
                                             "No recognition result is available to export"));
    return accepted;
}
void ImageEditingExportController::cancelCopy() {
    Impl::cancelRequest(m_impl->copy);
}
void ImageEditingExportController::cancel() {
    cancelCopy();
    Impl::cancelRequest(m_impl->active);
}
void ImageEditingExportController::Impl::cancelRequest(QPointer<Request>& pending) {
    const auto request = std::exchange(pending, {});
    if (!request)
        return;
    [[maybe_unused]] const auto completion = std::move(request->completion);
    request->job.cancel();
    request->save.cancel();
    if (!request)
        return;
    request->clipboard.cancel();
    if (request->artifact)
        request->artifact->cancel();
    if (request->upload)
        request->upload->cancel();
    if (!request)
        return;
    const auto dismiss = [request](QPointer<QDialog> dialog) {
        if (!dialog)
            return;
        if (request)
            QObject::disconnect(dialog, nullptr, request, nullptr);
        if (dialog->isVisible())
            dialog->reject();
        if (dialog)
            dialog->deleteLater();
    };
    const QPointer<QDialog> overwrite = request->overwrite;
    const QPointer<QDialog> dialog = request->dialog;
    dismiss(overwrite);
    dismiss(dialog);
    if (request)
        delete request;
}
} // namespace snow_shot::presentation
