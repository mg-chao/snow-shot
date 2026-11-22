// SPDX-License-Identifier: GPL-3.0-only
#include "snow_shot/presentation/screenshotmanualsave.h"

#include "snow_shot/presentation/screenshotencodingsettings.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/runtime/runtimeactivitytracker.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QThread>
#include <utility>

namespace {
class NativeSaveSession final : public QObject {
  public:
    NativeSaveSession(QObject* lifetime, QWidget* owner,
                      std::shared_ptr<ScreenshotExportArtifact> source,
                      ScreenshotManualSaveService::Completion completion,
                      Qt::WindowModality modality, ScreenshotManualSaveService::Saving saving)
        : QObject(lifetime), m_source(std::move(source)), m_completion(std::move(completion)),
          m_saving(std::move(saving)),
          m_encoding(snow_shot::presentation::screenshotEncodingOptions(
              snow_shot::storage::ScreenshotSettings())),
          m_pdf{screenshot_pdf::pageSizeForKey(
              snow_shot::storage::ScreenshotSettings().pdfPageSize())} {
        setObjectName(QStringLiteral("screenshotManualSaveSession"));
        const auto defaults =
            screenshotManualSaveDefaults(QDateTime::currentDateTime(), m_source->exportContext());
        auto* dialog = new QFileDialog(
            owner, QCoreApplication::translate("ScreenshotSaveAsFileDialog", "Save as file"));
        m_dialog = dialog;
        dialog->setObjectName(QStringLiteral("screenshotManualSaveDialog"));
        dialog->installEventFilter(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(modality);
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setFileMode(QFileDialog::AnyFile);
        dialog->setNameFilter(ScreenshotImageFileService::saveDialogFilter());
        dialog->selectNameFilter(defaults.selectedFilter);
        dialog->selectFile(defaults.initialPath);
        connect(dialog, &QDialog::rejected, this, [this] { cancel(); });
        connect(dialog, &QDialog::accepted, this, [this] { save(); });
        connect(dialog, &QObject::destroyed, this, [this] {
            m_dialog.clear();
            if (!m_writing && !m_finished)
                cancel();
        });
        connect(owner, &QObject::destroyed, this, [this] { cancel(); });
    }

    ~NativeSaveSession() override {
        const bool cancelSource = !m_finished;
        m_finished = true;
        if (cancelSource && m_source)
            m_source->cancel();
        if (m_dialog) {
            disconnect(m_dialog, nullptr, this, nullptr);
            m_dialog->reject();
        }
    }

    void present() {
        // QDialog::open() forces WindowModal. Keep the explicitly selected owner policy.
        m_dialog->show();
    }

    void cancel() {
        if (m_finished || m_cancelling)
            return;
        m_cancelling = true;
        const QPointer<NativeSaveSession> guard(this);
        const auto source = m_source;
        if (source)
            source->cancel();
        if (!guard)
            return;
        if (m_dialog)
            m_dialog->reject();
        if (guard)
            finish(
                ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::Cancelled, {}));
    }

  private:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == m_dialog && event->type() == QEvent::LanguageChange)
            m_dialog->setWindowTitle(
                QCoreApplication::translate("ScreenshotSaveAsFileDialog", "Save as file"));
        return QObject::eventFilter(object, event);
    }

    void save() {
        if (m_finished || m_writing || !m_dialog)
            return;
        const auto paths = m_dialog->selectedFiles();
        if (paths.isEmpty()) {
            cancel();
            return;
        }
        const auto destination =
            screenshotManualSaveDestination(paths.first(), m_dialog->selectedNameFilter());
        m_writing = true;
        const QPointer<NativeSaveSession> guard(this);
        if (m_saving)
            m_saving();
        if (!guard || m_finished)
            return;
        if (!m_source->requestSaveToPath(
                this, destination.path, destination.format, m_encoding,
                [this](ScreenshotExportTaskResult result) {
                    if (m_finished)
                        return;
                    if (result.succeeded())
                        static_cast<void>(
                            snow_shot::storage::ScreenshotSettings().setLastManualSaveDirectory(
                                QFileInfo(result.savedPath).absolutePath()));
                    finish(std::move(result));
                },
                m_pdf)) {
            finish(ScreenshotExportTaskResult::failure(
                ScreenshotExportFailureStage::Queue,
                QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                            "The screenshot export queue is full")));
        }
    }

    void finish(ScreenshotExportTaskResult result) {
        if (m_finished)
            return;
        m_finished = true;
        const auto source = std::move(m_source);
        Q_UNUSED(source);
        auto completion = std::move(m_completion);
        if (completion) {
            const auto activity = snow_shot::runtime::RuntimeActivityTracker::shared().acquire();
            QMetaObject::invokeMethod(
                parent(),
                [activity, completion = std::move(completion),
                 result = std::move(result)]() mutable {
                    Q_UNUSED(activity);
                    completion(std::move(result));
                },
                Qt::QueuedConnection);
        }
        deleteLater();
    }

    std::shared_ptr<ScreenshotExportArtifact> m_source;
    ScreenshotManualSaveService::Completion m_completion;
    ScreenshotManualSaveService::Saving m_saving;
    ScreenshotImageEncodingOptions m_encoding;
    ScreenshotPdfOptions m_pdf;
    QPointer<QFileDialog> m_dialog;
    bool m_writing = false;
    bool m_finished = false;
    bool m_cancelling = false;
};
} // namespace

void ScreenshotManualSaveHandle::cancel() const {
    if (m_session)
        static_cast<NativeSaveSession*>(m_session.data())->cancel();
}

bool ScreenshotManualSaveHandle::isValid() const {
    return !m_session.isNull();
}

ScreenshotManualSaveHandle ScreenshotManualSaveService::open(
    QObject* lifetime, QWidget* owner, std::shared_ptr<ScreenshotExportArtifact> source,
    Completion completion, Qt::WindowModality modality, Saving saving) {
    auto* application = QCoreApplication::instance();
    if (!lifetime || !owner || !source || !source->isValid() || !completion || !application ||
        QThread::currentThread() != application->thread() ||
        lifetime->thread() != application->thread() || owner->thread() != application->thread())
        return {};
    auto* session = new NativeSaveSession(lifetime, owner, std::move(source), std::move(completion),
                                          modality, std::move(saving));
    const QPointer<NativeSaveSession> guard(session);
    session->present();
    return guard ? ScreenshotManualSaveHandle(session) : ScreenshotManualSaveHandle{};
}
