// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/imagecompressionwindow.h"
#include "imageexporteditor_p.h"
#include "imageimportsourcereader_p.h"
#include "screenshotexportmemory_p.h"
#include "screenshotexportfiletransfer_p.h"
#include "snow_shot/presentation/collagedocument.h"
#include "snow_shot/presentation/components/icons/snowshoticons.h"
#include "snow_shot/presentation/components/imagedropsurface.h"
#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "snow_shot/presentation/windowshortcutmanager.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/capturehistoryrepository.h"
#include "snow_shot/storage/clipboardfilecache.h"
#include "snow_shot/storage/settingsadapters.h"
#include "widgets/button.h"
#include "widgets/form.h"
#include "widgets/input_number.h"
#include "widgets/message.h"
#include "widgets/modal.h"

#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QKeySequence>
#include <QMimeData>
#include <QPointer>
#include <QScreen>
#include <QStackedWidget>

namespace snow_shot::presentation {
namespace {
using adqt::widgets::AdButton;
using adqt::widgets::AdMessage;
using adqt::widgets::AdMessageHandle;
using adqt::widgets::AdMessageService;
using adqt::widgets::AdModal;
using Reader = ScreenshotClipboardContentReader;
using FileCache = storage::ClipboardFileCache;
using Format = ScreenshotImageFileFormat;

// Lossy copies retain the decoded export while building its canonical clipboard formats.
qint64 clipboardExportWorkingBytes(const screenshot_save_export::Encoded& encoded) {
    const auto size = encoded.options.size;
    if (encoded.options.format == Format::Png) {
        qint64 bytes = encoded.fileBytes;
        if (screenshot_pinned_render::addBytes(
                bytes, screenshot_export_memory::clipboardWorkingBytes(size, encoded.fileBytes)))
            return bytes;
        return std::numeric_limits<qint64>::max();
    }
    auto bytes = screenshot_export_memory::encodingWorkingBytes(
        size, Format::Png, {}, std::numeric_limits<qint64>::max(), true);
    if (encoded.codecResult.roundTrip == snow::image::PixelRoundTrip::exact)
        return bytes;
    const auto raster = screenshot_pinned_render::rasterBytes(size);
    if (!raster || !screenshot_pinned_render::addBytes(bytes, *raster))
        return std::numeric_limits<qint64>::max();
    return std::max(bytes, screenshot_export_memory::previewWorkingBytes(size));
}

struct ClipboardExport {
    ScreenshotClipboardPayload payload;
    std::unique_ptr<FileCache::ExportDirectory> directory;
};
ScreenshotExportTaskResult prepareClipboardExport(const screenshot_save_export::Encoded& encoded,
                                                  bool includeFile, const QString& filename,
                                                  const ScreenshotExportCancellation& cancellation,
                                                  ClipboardExport& output) {
    auto cancelled = [&] { return cancellation.isCancellationRequested(); };
    ScreenshotExportTaskResult result;
    auto rows = encoded.pixels->rows;
    if (encoded.codecResult.roundTrip != snow::image::PixelRoundTrip::exact) {
        const auto image = screenshot_save_export::decode(encoded, cancellation, &result.error);
        if (image.isNull())
            return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::Render,
                                                       result.error);
        rows = image_codec::srgbRowSource(image);
    }
    rows.cancellationRequested = [prior = rows.cancellationRequested, cancelled] {
        return cancelled() || (prior && prior());
    };
    if (encoded.options.format == Format::Png) {
        QFile png(encoded.path);
        if (png.open(QIODevice::ReadOnly)) {
            const auto bytes = png.readAll();
            if (bytes.size() == encoded.fileBytes)
                output.payload = ScreenshotClipboardService::prepareEncoded(rows, bytes);
        }
    } else
        output.payload = ScreenshotClipboardService::prepare(rows);
    if (!output.payload.isValid())
        return ScreenshotExportTaskResult::failure(
            ScreenshotExportFailureStage::Render,
            QCoreApplication::translate("ImageCompressionWindow", "The image could not be copied"));
    if (includeFile) {
        output.directory = FileCache::shared().createDirectory(&result.error);
        if (!output.directory)
            return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::File,
                                                       result.error);
        const auto file = QDir(output.directory->path()).filePath(filename);
        if (!screenshot_export_file_transfer::publishExclusive(encoded.path, file, cancelled,
                                                               &result.error))
            return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::File,
                                                       result.error);
        output.payload = ScreenshotClipboardService::withFile(std::move(output.payload), file);
    }
    return result;
}

class ImageCompressionWindow final : public ImageExportEditor {
    Q_DECLARE_TR_FUNCTIONS(ImageCompressionWindow)
  public:
    explicit ImageCompressionWindow(QObject* lifetime)
        : ImageExportEditor(ScreenshotSaveDialogState::initial({}), false),
          m_modal(new AdModal(lifetime)) {
        setObjectName(QStringLiteral("imageCompressionEditor"));
        initializeControls();
        auto* layout = static_cast<QHBoxLayout*>(this->layout());
        layout->removeWidget(m_preview);
        m_surface = new QStackedWidget(this);
        m_surface->setObjectName(QStringLiteral("imageCompressionSurface"));
        m_empty = new components::ImageDropSurface(false, m_surface);
        m_empty->setObjectName(QStringLiteral("imageCompressionEmptyState"));
        m_choose = m_empty->addImageButton();
        m_choose->setObjectName(QStringLiteral("imageCompressionAddImage"));
        m_surface->addWidget(m_empty);
        m_surface->addWidget(m_preview);
        m_drop = new components::ImageDropSurface(true, m_surface);
        m_drop->setObjectName(QStringLiteral("imageCompressionDropHint"));
        m_drop->hide();
        m_surface->setAcceptDrops(true);
        m_surface->installEventFilter(this);
        layout->insertWidget(0, m_surface, 1);

        m_modal->setObjectName(QStringLiteral("imageCompressionWindow"));
        m_modal->setMode(AdModal::Mode::Window);
        m_modal->setWindowModeDetached(true);
        m_modal->setWindowModality(Qt::NonModal);
        m_modal->setWindowResizable(true);
        m_modal->setWindowTaskbarVisible(true);
        m_modal->setWindowMinimizeButtonVisible(true);
        m_modal->setWindowMaximizeButtonVisible(true);
        m_modal->setWindowAlwaysOnTopButtonVisible(true);
        m_modal->setWindowAlwaysOnTop(true);
        m_modal->setCentered(true);
        m_modal->setWindowIcon(adqt::icons::makeIcon(icons::custom::app::ApplicationIcon()));
        m_modal->setClosePolicy(AdModal::ClosePolicy::Manual);
        auto* screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const auto available = screen->availableGeometry().size() - QSize(40, 40);
            m_modal->setWindowScreen(screen);
            m_modal->setWindowPreferredSize(QSize(1200, 800).boundedTo(available));
            m_modal->setWindowMinimumSize(QSize(900, 600).boundedTo(available));
        }
        m_modal->setContentWidget(this);
        auto* footer = new QWidget;
        auto* buttons = new QHBoxLayout(footer);
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(8);
        buttons->addWidget(m_outputDescription, 1);
        m_cancel = new AdButton(footer);
        m_save = new AdButton(footer);
        m_copy = new AdButton(footer);
        m_save->setObjectName(QStringLiteral("imageCompressionSave"));
        m_copy->setObjectName(QStringLiteral("imageCompressionCopy"));
        m_copy->setButtonStyle(AdButton::ButtonStyle::Solid);
        m_copy->setAccentRole(AdButton::AccentRole::Primary);
        m_copy->setDefault(true);
        for (auto* button : {m_cancel, m_save, m_copy}) {
            button->setFocusPolicy(Qt::StrongFocus);
            buttons->addWidget(button);
        }
        m_modal->setFooterWidget(footer);
        connect(m_modal, &AdModal::finished, this, [this] { stop(); });
        connect(m_modal, &AdModal::finished, m_modal, &QObject::deleteLater);
        connect(m_modal, &AdModal::closeRequested, this, [this](AdModal::CloseReason reason) {
            if (reason == AdModal::CloseReason::OkAction) {
                if (m_copy->isEnabled())
                    beginExport({});
            } else
                m_modal->reject();
        });
        connect(m_cancel, &AdButton::clicked, m_modal, &AdModal::reject);
        connect(m_save, &AdButton::clicked, this, [this] { chooseFile(true); });
        connect(m_copy, &AdButton::clicked, this, [this] { beginExport({}); });
        connect(m_choose, &AdButton::clicked, this, [this] { chooseFile(false); });
        WindowShortcutManager::Binding pasteBinding;
        pasteBinding.id = QStringLiteral("imageCompressionPaste");
        for (const auto& sequence : QKeySequence::keyBindings(QKeySequence::Paste)) {
            if (sequence.count() == 1)
                pasteBinding.keyCombinations.append(sequence[0]);
        }
        pasteBinding.priority = WindowShortcutManager::StandardPriority::WindowCommand;
        pasteBinding.canActivate = [this](const auto& context) {
            const auto* receiver = qobject_cast<QWidget*>(context.receiver);
            return editable() && m_modal->isOpen() && !m_picker && !m_overwrite && receiver &&
                   receiver->window() == window() &&
                   !WindowShortcutManager::focusAcceptsTextInput(context.focusWidget) &&
                   !WindowShortcutManager::inputTargetsPopup(context);
        };
        pasteBinding.activate = [this](const auto&) {
            paste();
            return true;
        };
        static_cast<void>(m_shortcuts.addBinding(this, std::move(pasteBinding)));
        connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged,
                this, [this] {
                    m_empty->refreshTheme();
                    m_drop->refreshTheme();
                });
        retranslate();
        updatePresentation();
        m_modal->open();
        m_shortcuts.addScopeWindow(window());
    }
    ~ImageCompressionWindow() override {
        stop();
    }

    void present(ScreenshotExportSource source) {
        if (closed())
            return;
        if (!source.isValid()) {
            sourceFailed(tr("The image could not be loaded"));
            return;
        }
        loadSource(std::make_shared<ScreenshotExportArtifact>(std::move(source)));
    }
    void presentSnapshot(const ScreenshotPinnedImageSnapshot& snapshot) {
        if (!snapshot.error.isEmpty())
            sourceFailed(snapshot.error);
        else
            present(snapshot.source);
    }
    void openFile(const QString& path) {
        auto budget = std::make_shared<ImageStitchingImportReservation>();
        present(ScreenshotExportSource::fromProducer([path, budget](const auto& cancellation) {
            const auto cancelled = [&] { return cancellation.isCancellationRequested(); };
            auto image = readImageImportFile(path, {}, cancelled,
                                             [budget](qint64 n) { return budget->resize(n); });
            if (!image.isNull())
                budget->resize(image.sizeInBytes());
            return image;
        }));
    }
    void selectedFiles(QObject* owner, std::shared_ptr<platform::SelectedFileBackend> backend,
                       platform::SelectedFileTarget target) {
        if (!backend || !backend->isValidTarget(target))
            return;
        m_resolving = true;
        updatePresentation();
        auto selection = std::make_shared<platform::SelectedFileResult>();
        m_selectionJob = ScreenshotExportCoordinator::shared().submit(
            this, ScreenshotExportCoordinator::Priority::Background,
            [backend = std::move(backend), target, selection](const auto& cancellation) {
                const auto cancelled = [&] { return cancellation.isCancellationRequested(); };
                *selection = backend->selectedFiles(target, cancelled);
                if (selection->error == platform::SelectedFileError::None) {
                    const auto files = Reader::snapshotLocalFiles(selection->paths, cancelled,
                                                                  Reader::FilePurpose::ImageImport);
                    selection->paths.clear();
                    const auto extensions = Reader::supportedImageFileExtensions();
                    for (const auto& file : files)
                        if (extensions.contains(file.suffix))
                            selection->paths.append(file.absolutePath);
                }
                return ScreenshotExportTaskResult{};
            },
            [this, owner, selection](auto result) {
                if (closed())
                    return;
                m_resolving = false;
                updatePresentation();
                if (!result.succeeded() || selection->error != platform::SelectedFileError::None) {
                    showError(
                        selection->error == platform::SelectedFileError::PermissionDenied
                            ? tr("Allow Snow Shot to access Finder in System Settings > Privacy & "
                                 "Security > Automation, then try again")
                            : tr("Could not read the selected image files. Please try again"));
                    return;
                }
                bool first = true;
                for (const auto& path : selection->paths) {
                    (first ? this : new ImageCompressionWindow(owner))->openFile(path);
                    first = false;
                }
            });
        if (!m_selectionJob.isValid()) {
            m_resolving = false;
            updatePresentation();
            sourceFailed(tr("The screenshot export queue is full"));
        }
    }

    // Hold the empty window while an active pin finishes publishing its source snapshot.
    void resolvePin(const QString& id, const ScreenshotPinnedImageSnapshotRequest& request) {
        const auto preparation = captureScreenshotPinnedRenderPreparation();
        const auto received = [guard = QPointer<ImageCompressionWindow>(this), id,
                               preparation](std::optional<ScreenshotPinnedImageSnapshot> snapshot) {
            if (!guard || guard->closed())
                return;
            guard->m_resolving = false;
            if (snapshot) {
                guard->presentSnapshot(*snapshot);
                return;
            }
            auto budget = std::make_shared<ImageStitchingImportReservation>();
            guard->present(ScreenshotExportSource::fromProducer(
                [id, preparation, budget](const auto& cancellation) {
                    auto loaded = readImageImportPinnedRecord(
                        id, [&] { return cancellation.isCancellationRequested(); },
                        [budget](qint64 n) { return budget->resize(n); }, preparation,
                        ScreenshotPinnedRenderMode::SourceResolution);
                    if (!loaded.image.isNull())
                        budget->resize(loaded.image.sizeInBytes());
                    return loaded.image;
                }));
        };
        m_resolving = true;
        updatePresentation();
        if (request)
            request(id, received);
        else
            received(std::nullopt);
    }

  private:
    void stop() {
        if (closed())
            return;
        cancelRender(true);
        m_selectionJob.cancel();
        m_exportJob.cancel();
        m_clipboard.cancelAll();
    }
    void retranslate() {
        retranslateControls();
        m_modal->setWindowTitle(tr("Image compression"));
        m_cancel->setText(tr("Cancel"));
        m_save->setText(tr("Save"));
        m_copy->setText(tr("Copy"));
        m_choose->setText(tr("Select image"));
        for (auto* button : {m_cancel, m_save, m_copy, m_choose})
            button->setAccessibleName(button->text());
        retranslatePicker();
        retranslateOverwriteConfirmation();
        m_empty->title->setText(tr("Select an image to compress"));
        m_empty->setAccessibleName(m_empty->title->text());
        m_drop->description->setText(tr("Drop one image file at a time"));
        m_drop->setAccessibleDescription(m_drop->description->text());
        if (m_exportNotice && m_exportNotice->isOpen())
            showExportCompleted();
        updatePresentation();
    }
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::LanguageChange && m_copy)
            retranslate();
        QWidget::changeEvent(event);
    }
    void paste() {
        const auto paths = Reader::localImageFilePaths(QApplication::clipboard()->mimeData());
        if (paths.isEmpty())
            openInlineImage();
        else
            openFile(paths.first());
    }
    void openInlineImage(const QMimeData* dropMime = nullptr) {
        auto budget = std::make_shared<ImageStitchingImportReservation>();
        bool rejected = false;
        const auto reserve = [&](qint64 bytes) {
            rejected = rejected || !budget->resize(bytes);
            return !rejected;
        };
        auto snapshot =
            dropMime
                ? Reader::snapshotImageMimeData(dropMime, devicePixelRatioF(), reserve)
                : Reader::snapshotImage(QApplication::clipboard(), devicePixelRatioF(), reserve);
        if (!snapshot) {
            if (dropMime || rejected)
                sourceFailed(tr("The image could not be loaded"));
            return;
        }
        present(ScreenshotExportSource::fromProducer(
            [snapshot = std::move(*snapshot),
             budget = std::move(budget)](const auto& cancellation) {
                return readImageImportClipboard(
                    snapshot, [&] { return cancellation.isCancellationRequested(); },
                    [budget](qint64 bytes) { return budget->resize(bytes); });
            }));
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != m_surface)
            return QWidget::eventFilter(watched, event);
        if (event->type() == QEvent::Resize)
            m_drop->setGeometry(m_surface->rect());
        if (event->type() == QEvent::DragLeave) {
            m_drop->hide();
            event->accept();
            return true;
        }
        if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove &&
            event->type() != QEvent::Drop)
            return false;
        auto* drop = static_cast<QDropEvent*>(event);
        const auto* mime = drop->mimeData();
        const auto paths = Reader::localImageFilePaths(mime);
        if (!editable() || !(drop->possibleActions() & Qt::CopyAction) || paths.size() > 1 ||
            (paths.isEmpty() && !Reader::hasSupportedInlineImage(mime))) {
            m_drop->hide();
            drop->ignore();
            return true;
        }
        drop->setDropAction(Qt::CopyAction);
        drop->accept();
        if (event->type() != QEvent::Drop) {
            m_drop->setGeometry(m_surface->rect());
            m_drop->raise();
            m_drop->show();
        } else {
            m_drop->hide();
            if (paths.isEmpty())
                openInlineImage(mime);
            else
                openFile(paths.first());
        }
        return true;
    }
    bool editable() const {
        return m_phase == Phase::Editing && !m_resolving && !m_pendingArtifact;
    }
    void updatePresentation() override {
        const bool hasImage = m_source.rows.isValid();
        m_form->setEnabled(editable() && hasImage);
        m_surface->setCurrentWidget(hasImage ? static_cast<QWidget*>(m_preview) : m_empty);
        m_empty->description->setText(m_resolving || m_pendingArtifact
                                          ? tr("Loading image…")
                                          : tr("Drag an image file here, or choose select image"));
        m_empty->setAccessibleDescription(m_empty->description->text());
        m_drop->title->setText(hasImage ? tr("Drop one image to replace the current image")
                                        : tr("Drop one image to compress"));
        m_drop->setAccessibleName(m_drop->title->text());
        if (!editable())
            m_drop->hide();
        m_choose->setEnabled(editable());
        validateFields();
        m_save->setBusy(saving() && !m_savePath.isEmpty());
        m_copy->setBusy(saving() && m_savePath.isEmpty());
    }
    void validateFields() override {
        const bool valid = m_state.imageValidationError().isEmpty();
        auto status = !m_source.rows.isValid() || valid
                          ? adqt::widgets::AdInputNumber::Status::None
                          : adqt::widgets::AdInputNumber::Status::Error;
        m_width->setStatus(status);
        m_height->setStatus(status);
        bool enabled = editable() && m_source.rows.isValid() && valid;
        m_save->setEnabled(enabled);
        m_copy->setEnabled(enabled);
    }
    void retranslatePicker() {
        if (!m_picker)
            return;
        if (m_picker->acceptMode() == QFileDialog::AcceptSave) {
            m_picker->setWindowTitle(tr("Save"));
            m_picker->setNameFilter(
                ScreenshotImageFileService::dialogFilter(m_state.output.format));
        } else {
            m_picker->setWindowTitle(m_choose->text());
            QStringList patterns;
            for (const auto& extension : Reader::supportedImageFileExtensions())
                patterns.append(QStringLiteral("*.") + extension);
            m_picker->setNameFilter(tr("Images") +
                                    QStringLiteral(" (%1)").arg(patterns.join(u' ')));
        }
    }
    void retranslateOverwriteConfirmation() {
        if (!m_overwrite)
            return;
        m_overwrite->setWindowTitle(tr("Replace file"));
        m_overwrite->setText(tr("Replace the existing file \"%1\"?")
                                 .arg(m_overwrite->property("filename").toString()));
        m_overwrite->setAcceptText(tr("Replace"));
        m_overwrite->setRejectText(tr("Cancel"));
    }
    void confirmOverwrite(const QString& target) {
        auto* confirm = new AdModal(this);
        m_overwrite = confirm;
        confirm->setObjectName(QStringLiteral("imageCompressionOverwrite"));
        confirm->setOwnerWindow(window());
        confirm->setRenderContainer(window());
        confirm->setProperty("filename", QFileInfo(target).fileName());
        retranslateOverwriteConfirmation();
        connect(confirm, &AdModal::finished, this,
                [this, target, confirm](AdModal::DialogCode code) {
                    m_overwrite.clear();
                    confirm->deleteLater();
                    if (code == AdModal::DialogCode::Accepted)
                        beginExport(target, /*replaceExisting=*/true);
                });
        confirm->open();
    }
    void chooseFile(bool save) {
        if (!editable() || m_picker || m_overwrite)
            return;
        commitDimensionEdit();
        if (save && !m_state.imageValidationError().isEmpty())
            return;
        auto* dialog = new QFileDialog(window());
        m_picker = dialog;
        if (save) {
#ifndef Q_OS_MACOS
            // Defer confirmation until the output suffix has been applied where supported.
            dialog->setOption(QFileDialog::DontConfirmOverwrite);
#endif
            dialog->setAcceptMode(QFileDialog::AcceptSave);
            dialog->setFileMode(QFileDialog::AnyFile);
            dialog->setDirectory(m_state.directory);
            dialog->setDefaultSuffix(ScreenshotImageFileService::extension(m_state.output.format));
            dialog->selectFile(QFileInfo(m_state.outputPath()).fileName());
        } else {
            dialog->setFileMode(QFileDialog::ExistingFile);
        }
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(Qt::WindowModal);
        retranslatePicker();
        connect(dialog, &QDialog::accepted, this, [this, dialog, save] {
            if (!editable())
                return;
            const auto path = dialog->selectedFiles().value(0);
            if (path.isEmpty())
                return;
            if (!save) {
                openFile(path);
                return;
            }
            const auto target =
                ScreenshotImageFileService::normalizedPath(path, m_state.output.format);
            const bool exists = QFileInfo::exists(target);
            const bool pickerConfirmedTarget =
                path == target && !dialog->testOption(QFileDialog::DontConfirmOverwrite);
            if (exists && !pickerConfirmedTarget)
                confirmOverwrite(target);
            else
                beginExport(target, /*replaceExisting=*/exists);
        });
        dialog->open();
    }
    void beginExport(QString path, bool replaceExisting = false) {
        if (!editable())
            return;
        commitDimensionEdit();
        if (!m_source.rows.isValid() || !m_state.imageValidationError().isEmpty())
            return;
        if (m_exportNotice)
            m_exportNotice->close();
        showError({});
        m_savePath = std::move(path);
        m_replaceExisting = replaceExisting;
        if (m_savePath.isEmpty())
            m_publication = ScreenshotClipboardService::reservePublication();
        m_phase = Phase::Saving;
        updatePresentation();
        m_renderTimer.stop();
        cancelDecode();
        startRender();
    }
    void exportFailed(const QString& error) override {
        m_phase = Phase::Editing;
        m_preview->setBusy(false);
        updatePresentation();
        showError(error);
        startDecode();
    }
    void exportReady() override {
        if (m_exportJob.isValid())
            return;
        const auto encoded = m_encoded;
        const auto path = m_savePath;
        const bool copy = path.isEmpty();
        const bool includeFile = copy && storage::ScreenshotSettings().copyImageFileToClipboard();
        auto output = std::make_shared<ClipboardExport>();
        m_exportJob = ScreenshotExportCoordinator::shared().submit(
            this, ScreenshotExportCoordinator::Priority::Foreground,
            [encoded, path, replaceExisting = m_replaceExisting, includeFile, output,
             filename = QFileInfo(m_state.outputPath()).fileName()](const auto& cancellation) {
                auto cancelled = [&] { return cancellation.isCancellationRequested(); };
                ScreenshotExportTaskResult result;
                if (!path.isEmpty()) {
                    if (replaceExisting) {
                        const auto file = ScreenshotImageFileService::writeEncodedFile(
                            encoded->path, path, encoded->options.format, cancelled);
                        if (!file.succeeded())
                            return ScreenshotExportTaskResult::failure(
                                ScreenshotExportFailureStage::File, file.error);
                        result.savedPath = file.path;
                    } else if (screenshot_export_file_transfer::publishExclusive(
                                   encoded->path, path, cancelled, &result.error)) {
                        result.savedPath = path;
                    } else {
                        return ScreenshotExportTaskResult::failure(
                            ScreenshotExportFailureStage::File, result.error);
                    }
                    return result;
                }
                return prepareClipboardExport(*encoded, includeFile, filename, cancellation,
                                              *output);
            },
            [this, copy, output](ScreenshotExportTaskResult result) {
                m_exportJob = {};
                if (closed())
                    return;
                if (!result.succeeded()) {
                    exportFailed(result.error);
                    return;
                }
                if (!copy) {
                    static_cast<void>(storage::ScreenshotSettings().setLastManualSaveDirectory(
                        QFileInfo(result.savedPath).absolutePath()));
                    finishExport();
                    return;
                }
                // Publishing can synchronously notify clipboard listeners that dismiss this window.
                if (output->directory)
                    output->directory->retain();
                static_cast<void>(m_clipboard.commit(
                    QApplication::clipboard(), this, std::move(output->payload),
                    [this](ScreenshotClipboardCommitResult committed) {
                        if (closed())
                            return;
                        if (!committed.succeeded()) {
                            exportFailed(committed.errorString());
                            return;
                        }
                        finishExport(!committed.superseded);
                    },
                    m_publication));
            },
            copy ? clipboardExportWorkingBytes(*encoded) : 0);
        if (!m_exportJob.isValid())
            exportFailed(tr("The screenshot export queue is full"));
    }
    void finishExport(bool published = true) {
        if (published)
            persistFormatOptions();
        m_phase = Phase::Editing;
        updatePresentation();
        startDecode();
        if (published)
            showExportCompleted();
    }
    void showExportCompleted() {
        AdMessage::Request request;
        request.key = QStringLiteral("imageCompressionExport");
        request.content = tr("Export completed");
        m_exportNotice = AdMessageService::success(std::move(request), window());
    }
    AdModal* m_modal;
    AdButton* m_choose = nullptr;
    AdButton* m_cancel = nullptr;
    AdButton* m_save = nullptr;
    AdButton* m_copy = nullptr;
    QStackedWidget* m_surface = nullptr;
    components::ImageDropSurface* m_empty = nullptr;
    components::ImageDropSurface* m_drop = nullptr;
    QPointer<QFileDialog> m_picker;
    QPointer<AdModal> m_overwrite;
    QPointer<AdMessageHandle> m_exportNotice;
    bool m_resolving = false;
    QString m_savePath;
    bool m_replaceExisting = false;
    ScreenshotExportJobHandle m_exportJob;
    ScreenshotExportJobHandle m_selectionJob;
    WindowShortcutManager m_shortcuts{this};
    ScreenshotClipboardCommitScope m_clipboard;
    ScreenshotClipboardService::PublicationId m_publication = 0;
};
} // namespace

ImageCompressionWindowCoordinator::ImageCompressionWindowCoordinator(
    ScreenshotPinnedImageSnapshotRequest requestPin)
    : m_requestPin(std::move(requestPin)) {}
void ImageCompressionWindowCoordinator::shutdown() {
    for (auto* window : findChildren<AdModal*>(Qt::FindDirectChildrenOnly))
        window->reject();
}
void ImageCompressionWindowCoordinator::presentSelectedFiles(
    std::shared_ptr<platform::SelectedFileBackend> backend, platform::SelectedFileTarget target) {
    (new ImageCompressionWindow(this))->selectedFiles(this, std::move(backend), target);
}
void ImageCompressionWindowCoordinator::presentHistory(
    const storage::CaptureHistoryRecord& record) {
    auto budget = std::make_shared<ImageStitchingImportReservation>();
    (new ImageCompressionWindow(this))
        ->present(ScreenshotExportSource::fromProducer([record, budget](const auto& cancel) {
            if (cancel.isCancellationRequested() || !record.result)
                return QImage{};
            const auto bytes = screenshot_pinned_render::rasterBytes(record.result->imageSize);
            if (!bytes || !budget->resize(*bytes))
                return QImage{};
            return storage::ApplicationStorage::instance()
                .captureHistory()
                .loadResultImagePreservingRecord(record)
                .value_or(QImage{});
        }));
}
void ImageCompressionWindowCoordinator::presentPinnedRecord(const QString& id) {
    (new ImageCompressionWindow(this))->resolvePin(id, m_requestPin);
}
void ImageCompressionWindowCoordinator::presentSnapshot(
    const ScreenshotPinnedImageSnapshot& snapshot) {
    (new ImageCompressionWindow(this))->presentSnapshot(snapshot);
}
} // namespace snow_shot::presentation
