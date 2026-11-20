#include "snow_shot/presentation/screenshotsaveasfiledialog.h"

#include "snow_shot/presentation/components/actionpopupmenu.h"
#include "snow_shot/presentation/components/pathinput.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/storage/exportfilename.h"
#include "imageexporteditor_p.h"
#include "screenshotsavefooter.h"
#include "screenshotsaveresult.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/detail/flow_layout.h"
#include "widgets/field_group.h"
#include "widgets/form.h"
#include "widgets/input_line_edit.h"
#include "widgets/input_number.h"
#include "widgets/modal.h"
#include "antd_icons.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace {
using namespace adqt::widgets;
namespace icons = adqt::icons::antd::outlined;
namespace pipeline = screenshot_save_export;
using Format = ScreenshotImageFileFormat;
using Shortcut = snow_shot::storage::ScreenshotSavePathShortcut;

void configureForm(AdForm* form) {
    form->setFormLayout(AdForm::FormLayout::Vertical);
    form->setLabelAlign(AdForm::LabelAlign::Left);
    form->setColon(false);
    form->setRequiredMark(AdForm::RequiredMark::Hidden);
}

class SaveContent final : public ImageExportEditor {
    Q_DECLARE_TR_FUNCTIONS(ScreenshotSaveAsFileDialog)

  public:
    using ImageExportEditor::saving;
    SaveContent(AdModal* modal, std::shared_ptr<ScreenshotExportArtifact> artifact,
                ScreenshotSaveAsFileDialog::Saved saved,
                ScreenshotSaveAsFileDialog::BeforeWrite beforeWrite, bool retainFile,
                const QDateTime& timestamp = QDateTime::currentDateTime())
        : ImageExportEditor(
              ScreenshotSaveDialogState::initial({}, timestamp, artifact->windowNames()), true),
          m_modal(modal), m_saved(std::move(saved)), m_beforeWrite(std::move(beforeWrite)),
          m_retainFile(retainFile), m_windowNames(artifact->windowNames()), m_timestamp(timestamp) {
        m_artifact = std::move(artifact);
        setObjectName(QStringLiteral("saveDialogContent"));
        setMinimumHeight(240);
        setMaximumHeight(540);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        if (m_showSaveResult) {
            m_result = new ScreenshotSaveResult(
                this,
                [this] {
                    setPhase(Phase::Editing);
                    validateFields();
                    m_filename->setFocus();
                },
                [this] { m_modal->accept(); });
            m_rightPanel->addWidget(m_result->view());
        }

        auto* directory = new QWidget(m_form);
        directory->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* directoryLayout = new QVBoxLayout(directory);
        directoryLayout->setContentsMargins(0, 0, 0, 0);
        directoryLayout->setSpacing(8);
        m_directory = new DirectoryPathInput(directory);
        m_directory->setObjectName(QStringLiteral("saveDirectoryPathInput"));
        m_directory->lineEdit()->setObjectName(QStringLiteral("saveDirectoryInput"));
        m_directory->browseButton()->setObjectName(QStringLiteral("saveDirectoryBrowseButton"));
        m_directory->setText(m_state.directory);
        directoryLayout->addWidget(m_directory);
        addField("Save path", directory, "directory");
        connect(m_directory, &DirectoryPathInput::browseRequested, this, [this] {
            const QString path = QFileDialog::getExistingDirectory(
                window(), tr("Select save directory"), m_directory->text());
            if (!path.isEmpty())
                m_directory->setText(path);
        });
        m_shortcutsHost = new QWidget(directory);
        m_shortcutsHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        m_shortcutsLayout = new adqt::widgets::detail::FlowLayout(m_shortcutsHost, 0, 6, 6);
        m_shortcutsHost->installEventFilter(this);
        directoryLayout->addWidget(m_shortcutsHost);
        rebuildShortcuts();

        m_filename = new AdLineEdit(m_form);
        m_filename->setObjectName(QStringLiteral("saveFilenameInput"));
        m_filename->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_filename->setText(m_state.filename);
        addField("File name", m_filename, "filename");
        connect(m_directory, &DirectoryPathInput::textChanged, this, [this](const QString& value) {
            m_state.directory = value;
            validateFields();
        });
        connect(m_filename, &AdLineEdit::textChanged, this, [this](const QString& value) {
            // Generated defaults are already literal stems. Only explicit edits interpret a
            // recognized image suffix; arbitrary dotted titles and names stay intact.
            QString stem = value.trimmed();
            if (ScreenshotImageFileService::formatForPath(stem))
                stem.chop(QFileInfo(stem).suffix().size() + 1);
            m_state.filename = std::move(stem);
            if (m_state.output.format == Format::Pdf)
                changed();
            else
                validateFields();
        });
        initializeControls();
        retranslate();
        m_form->setDisabled(true);
        m_preview->setBusy(true);
        QTimer::singleShot(0, this, [this] {
            if (!closed())
                loadSource(m_artifact);
        });
    }

    ~SaveContent() override {
        cancel(!closed());
    }
    void cancel(bool cancelSource = false) {
        cancelRender(cancelSource);
        if (m_menu)
            m_menu->dismissPopup();
        m_saveJob.cancel();
        if (m_result)
            m_result->cancel();
    }
    void finish(ScreenshotSaveAsFileDialog::Finished finished) {
        if (closed())
            return;
        const QString path = m_lastSavedPath;
        auto saved = std::move(m_saved);
        cancel(path.isEmpty());
        // Completion can destroy the owner and this content. Keep both callbacks and the
        // successful path locally, preserving the existing Finished-before-Saved ordering.
        if (finished)
            finished(!path.isEmpty());
        if (!path.isEmpty() && saved)
            saved(path);
    }
    void acceptAction() {
        if (m_phase == Phase::Result)
            m_result->activateDefaultAction();
        else if (m_phase == Phase::Editing)
            save();
    }
    QSize sizeHint() const override {
        // Prefer the full preview height. The modal's layout may shrink the content
        // to fit its measured header and footer inside the available screen.
        return {QWidget::sizeHint().width(), maximumHeight()};
    }
    void installFooter() {
        auto closePolicy = m_modal->closeButton()->sizePolicy();
        closePolicy.setRetainSizeWhenHidden(true);
        m_modal->closeButton()->setSizePolicy(closePolicy);
        auto* editing = new QWidget;
        auto* editingLayout = new QVBoxLayout(editing);
        editingLayout->setContentsMargins(0, 0, 0, 0);
        editingLayout->addStretch();
        auto* layout = new QHBoxLayout;
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(m_outputDescription, 1, Qt::AlignVCenter);
        layout->addWidget(m_modal->rejectButton(), 0, Qt::AlignVCenter);
        layout->addWidget(m_modal->acceptButton(), 0, Qt::AlignVCenter);
        editingLayout->addLayout(layout);
        m_footer = new ScreenshotSaveFooter(editing, m_result ? m_result->actions() : nullptr);
        m_modal->setFooterWidget(m_footer);
        updatePresentation();
    }
    void save() {
        if (m_phase != Phase::Editing || !m_source.rows.isValid())
            return;
        commitDimensionEdit();
        m_state.directory = m_directory->text();
        const QString error = m_state.validationError();
        if (!error.isEmpty()) {
            validateFields();
            showError(error);
            return;
        }
        const QString path = m_state.outputPath();
        if (QFileInfo::exists(path)) {
            auto* confirm = new AdModal(this);
            m_overwrite = confirm;
            confirm->setObjectName(QStringLiteral("saveOverwriteModal"));
            confirm->setOwnerWindow(window());
            confirm->setRenderContainer(window());
            confirm->setWindowTitle(tr("Replace file"));
            confirm->setText(
                tr("Replace the existing file \"%1\"?").arg(QFileInfo(path).fileName()));
            confirm->setProperty("filename", QFileInfo(path).fileName());
            confirm->setAcceptText(tr("Replace"));
            confirm->setRejectText(tr("Cancel"));
            confirm->setClosePolicy(AdModal::ClosePolicy::Manual);
            connect(confirm, &AdModal::closeRequested, confirm,
                    [this, confirm](AdModal::CloseReason reason) {
                        if (reason == AdModal::CloseReason::OkAction) {
                            confirm->accept();
                            beginSave();
                        } else
                            confirm->reject();
                    });
            connect(confirm, &AdModal::finished, confirm, &QObject::deleteLater);
            confirm->open();
        } else
            beginSave();
    }

  protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::LanguageChange)
            retranslate();
        QWidget::changeEvent(event);
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == m_shortcutsHost &&
            (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)) {
            updateShortcutHeight();
        }
        if (event->type() == QEvent::LanguageChange && m_editor &&
            object == m_editor->contentWidget())
            retranslateSecondary();
        return QWidget::eventFilter(object, event);
    }

  private:
    void setPhase(Phase phase) {
        m_phase = phase;
        updatePresentation();
    }
    void updatePresentation() override {
        const bool editing = m_phase == Phase::Editing;
        const bool result = m_phase == Phase::Result;
        m_form->setEnabled(editing && m_source.rows.isValid());
        m_modal->setAcceptButtonBusy(saving());
        m_modal->setCloseButtonVisible(!saving());
        m_modal->setCloseOnEscape(!saving());
        m_modal->rejectButton()->setEnabled(editing);
        m_modal->acceptButton()->setEnabled(editing && m_source.rows.isValid() &&
                                            m_state.validationError().isEmpty());
        m_modal->acceptButton()->setDefault(!result);
        m_rightPanel->setCurrentIndex(result ? 1 : 0);
        if (m_footer)
            m_footer->setResultVisible(result);
    }
    void validateFields() override {
        if (!m_source.rows.isValid())
            return;
        auto setError = [this](const QString& key, const QString& message) {
            for (auto* item : m_form->findChildren<AdFormItem*>()) {
                if (item->fieldName() == key)
                    item->setErrorMessages(message.isEmpty() ? QStringList{}
                                                             : QStringList{message});
            }
        };
        const QString imageError = m_state.imageValidationError();
        const QString error = m_state.validationError();
        const bool badPath = m_state.directory.trimmed().isEmpty() ||
                             !QDir::isAbsolutePath(m_state.directory.trimmed());
        setError(QStringLiteral("directory"), badPath ? error : QString());
        setError(QStringLiteral("filename"), !badPath && error != imageError ? error : QString());
        const bool existingFile =
            !badPath && error == imageError && QFileInfo::exists(m_state.outputPath());
        m_form->itemForName(QStringLiteral("filename"))
            ->setWarningMessages(existingFile
                                     ? QStringList{tr("A file with this name already exists")}
                                     : QStringList{});
        m_width->setStatus(imageError.isEmpty() ? AdInputNumber::Status::None
                                                : AdInputNumber::Status::Error);
        m_height->setStatus(imageError.isEmpty() ? AdInputNumber::Status::None
                                                 : AdInputNumber::Status::Error);
        m_modal->acceptButton()->setEnabled(m_phase == Phase::Editing && m_source.rows.isValid() &&
                                            error.isEmpty());
    }
    void retranslate() {
        m_modal->setWindowTitle(tr("Save as file"));
        m_modal->setAcceptText(tr("Save"));
        m_modal->setRejectText(tr("Cancel"));
        m_directory->setBrowseButtonText(tr("Select save directory"));
        m_directory->lineEdit()->setAccessibleName(tr("Save path"));
        m_filename->setAccessibleName(tr("File name"));
        retranslateControls();
        rebuildShortcuts();
        updateControls();
        retranslateSecondary();
        validateFields();
    }
    void retranslateSecondary() {
        if (m_menu) {
            const auto actions = m_menu->actions();
            if (actions.size() == 2) {
                actions[0]->setText(tr("Edit"));
                actions[1]->setText(tr("Delete"));
            }
        }
        if (m_overwrite) {
            m_overwrite->setWindowTitle(tr("Replace file"));
            m_overwrite->setText(tr("Replace the existing file \"%1\"?")
                                     .arg(m_overwrite->property("filename").toString()));
            m_overwrite->setAcceptText(tr("Replace"));
            m_overwrite->setRejectText(tr("Cancel"));
        }
        if (m_editor) {
            m_editor->setWindowTitle(m_editor->property("editingShortcut").toBool()
                                         ? tr("Edit save path")
                                         : tr("Add save path"));
            m_editor->setAcceptText(tr("OK"));
            m_editor->setRejectText(tr("Cancel"));
            auto* form = qobject_cast<AdForm*>(m_editor->contentWidget());
            if (form) {
                form->itemForName(QStringLiteral("name"))->setLabel(tr("Name"));
                form->itemForName(QStringLiteral("path"))->setLabel(tr("Path"));
                form->findChild<AdLineEdit*>(QStringLiteral("savePathNameInput"))
                    ->setAccessibleName(tr("Name"));
                form->findChild<AdLineEdit*>(QStringLiteral("savePathValueInput"))
                    ->setAccessibleName(tr("Path"));
                form->findChild<DirectoryPathInput*>(QStringLiteral("savePathValuePathInput"))
                    ->setBrowseButtonText(tr("Select save directory"));
                if (form->property("validationAttempted").toBool())
                    static_cast<void>(
                        validateShortcut(form, m_editor->property("shortcutIndex").toInt()));
            }
        }
    }
    void rebuildShortcuts() {
        while (auto* item = m_shortcutsLayout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            delete item;
        }
        m_shortcuts = snow_shot::storage::ScreenshotSettings().savePathShortcuts();
        auto add = [this](const QString& name, const QString& path, int index) {
            auto* group = new AdFieldGroup(m_shortcutsHost);
            group->setObjectName(QStringLiteral("savePathShortcutGroup_%1").arg(index));
            auto* button = new AdButton(name, group);
            button->setSizeClass(AdButton::SizeClass::Small);
            button->setMaximumWidth(230);
            button->setToolTip(path);
            button->setObjectName(QStringLiteral("savePathShortcut_%1").arg(index));
            group->addControl(button);
            connect(button, &QAbstractButton::clicked, this,
                    [this, path] { m_directory->setText(path); });
            if (index >= 0) {
                auto* expand = new AdButton(group);
                expand->setObjectName(QStringLiteral("savePathExpand_%1").arg(index));
                expand->setSizeClass(AdButton::SizeClass::Small);
                expand->setIconRef(icons::More());
                expand->setFixedSize(24, 24);
                expand->setToolTip(tr("Edit or delete save path"));
                expand->setAccessibleName(expand->toolTip());
                group->addControl(expand);
                new snow_shot::presentation::ActionPopupMenu(
                    expand, [this, expand, index] { return createShortcutMenu(expand, index); },
                    snow_shot::presentation::ActionPopupMenu::Placement::BottomLeft,
                    snow_shot::presentation::ActionPopupMenu::Surface::Widget);
            }
            m_shortcutsLayout->addWidget(group);
        };
        add(tr("App directory"),
            snow_shot::storage::expandScreenshotExportVariables(
                snow_shot::storage::ScreenshotSettings().imageSaveDirectory(), m_timestamp,
                m_windowNames),
            -2);
        add(tr("Desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), -1);
        for (int index = 0; index < m_shortcuts.size(); ++index)
            add(m_shortcuts.at(index).name, m_shortcuts.at(index).path, index);
        auto* button = new AdButton(m_shortcutsHost);
        button->setObjectName(QStringLiteral("savePathAddButton"));
        button->setSizeClass(AdButton::SizeClass::Small);
        button->setIconRef(icons::Plus());
        button->setFixedSize(24, 24);
        button->setToolTip(tr("Add save path"));
        button->setAccessibleName(button->toolTip());
        connect(button, &QAbstractButton::clicked, this, [this] { shortcutDialog(-1); });
        m_shortcutsLayout->addWidget(button);
        updateShortcutHeight();
        m_shortcutsHost->updateGeometry();
    }
    void updateShortcutHeight() {
        const int height = m_shortcutsLayout->heightForWidth(m_shortcutsHost->width());
        if (m_shortcutsHost->minimumHeight() != height)
            m_shortcutsHost->setFixedHeight(height);
    }
    AdContextMenu* createShortcutMenu(AdButton* trigger, int index) {
        if (index < 0 || index >= m_shortcuts.size())
            return nullptr;
        auto* menu = new AdContextMenu(this);
        m_menu = menu;
        menu->setObjectName(QStringLiteral("savePathMenu"));
        menu->setTriggerWidget(trigger);
        auto* edit = menu->addItem(tr("Edit"), icons::Edit());
        auto* remove = menu->addItem(tr("Delete"), icons::IconDelete());
        menu->setActionDanger(remove);
        connect(edit, &QAction::triggered, this, [this, index] { shortcutDialog(index); });
        connect(remove, &QAction::triggered, this, [this, index] {
            auto shortcuts = m_shortcuts;
            shortcuts.removeAt(index);
            if (snow_shot::storage::ScreenshotSettings().setSavePathShortcuts(shortcuts))
                rebuildShortcuts();
            else
                showError(tr("The save path settings could not be saved"));
        });
        return menu;
    }
    void shortcutDialog(int index) {
        auto* modal = new AdModal(this);
        m_editor = modal;
        modal->setProperty("editingShortcut", index >= 0);
        modal->setProperty("shortcutIndex", index);
        modal->setObjectName(QStringLiteral("savePathEditorModal"));
        modal->setOwnerWindow(window());
        modal->setRenderContainer(window());
        modal->setMode(AdModal::Mode::Overlay);
        modal->setWindowTitle(index < 0 ? tr("Add save path") : tr("Edit save path"));
        modal->setPreferredWidth(400);
        modal->setCentered(true);
        modal->setCloseOnMaskClick(false);
        modal->setClosePolicy(AdModal::ClosePolicy::Manual);
        modal->setAcceptText(tr("OK"));
        modal->setRejectText(tr("Cancel"));
        auto* form = new AdForm;
        configureForm(form);
        form->setRequiredMark(AdForm::RequiredMark::Visible);
        auto* name = new AdLineEdit(form);
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        name->setObjectName(QStringLiteral("savePathNameInput"));
        auto* path = new DirectoryPathInput(form);
        path->setObjectName(QStringLiteral("savePathValuePathInput"));
        path->lineEdit()->setObjectName(QStringLiteral("savePathValueInput"));
        path->browseButton()->setObjectName(QStringLiteral("savePathValueBrowseButton"));
        connect(path, &DirectoryPathInput::browseRequested, path, [this, path] {
            const QString directory = QFileDialog::getExistingDirectory(
                window(), tr("Select save directory"), path->text());
            if (!directory.isEmpty())
                path->setText(directory);
        });
        if (index >= 0) {
            name->setText(m_shortcuts.at(index).name);
            path->setText(m_shortcuts.at(index).path);
        }
        auto* nameItem = form->addField(tr("Name"), name, QStringLiteral("name"));
        auto* pathItem = form->addField(tr("Path"), path, QStringLiteral("path"));
        for (auto* item : {nameItem, pathItem})
            item->setRequired(true);
        modal->setContentWidget(form);
        form->installEventFilter(this);
        modal->setInitialFocusWidget(name);
        retranslateSecondary();
        connect(modal, &AdModal::closeRequested, modal,
                [this, modal, form, name, path, pathItem, index](AdModal::CloseReason reason) {
                    if (reason != AdModal::CloseReason::OkAction) {
                        modal->reject();
                        return;
                    }
                    form->setProperty("validationAttempted", true);
                    if (!validateShortcut(form, index))
                        return;
                    const QString value = name->text().trimmed();
                    const QString target = path->text().trimmed();
                    auto shortcuts = m_shortcuts;
                    const Shortcut shortcut{value, QDir::cleanPath(target)};
                    if (index < 0)
                        shortcuts.push_back(shortcut);
                    else
                        shortcuts[index] = shortcut;
                    if (!snow_shot::storage::ScreenshotSettings().setSavePathShortcuts(shortcuts)) {
                        pathItem->setErrorMessages(
                            {tr("The save path settings could not be saved")});
                        return;
                    }
                    rebuildShortcuts();
                    modal->accept();
                });
        connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
        modal->open();
    }
    bool validateShortcut(AdForm* form, int index) {
        const QString name =
            form->findChild<AdLineEdit*>(QStringLiteral("savePathNameInput"))->text().trimmed();
        const QString path =
            form->findChild<AdLineEdit*>(QStringLiteral("savePathValueInput"))->text().trimmed();
        QString nameError;
        if (name.isEmpty())
            nameError = tr("Please enter a name");
        for (int other = 0; other < m_shortcuts.size(); ++other)
            if (other != index &&
                m_shortcuts.at(other).name.compare(name, Qt::CaseInsensitive) == 0)
                nameError = tr("A save path with this name already exists");
        const QString pathError = path.isEmpty() || !QDir::isAbsolutePath(path)
                                      ? tr("Please enter an absolute directory path")
                                      : QString();
        form->itemForName(QStringLiteral("name"))
            ->setErrorMessages(nameError.isEmpty() ? QStringList{} : QStringList{nameError});
        form->itemForName(QStringLiteral("path"))
            ->setErrorMessages(pathError.isEmpty() ? QStringList{} : QStringList{pathError});
        return nameError.isEmpty() && pathError.isEmpty();
    }
    void beginSave() {
        if (m_phase != Phase::Editing)
            return;
        persistFormatOptions();
        setPhase(Phase::Saving);
        m_renderTimer.stop();
        m_savePath = m_state.outputPath();
        if (m_beforeWrite)
            m_written = m_beforeWrite();
        cancelDecode();
        startRender();
    }
    void exportFailed(const QString& error) override {
        m_written = {};
        setPhase(Phase::Editing);
        m_preview->setBusy(false);
        validateFields();
        showError(error);
        startDecode();
    }
    void exportReady() override {
        if (m_saveJob.isValid())
            return;
        m_saveJob = ScreenshotExportCoordinator::shared().submit(
            this, ScreenshotExportCoordinator::Priority::Foreground,
            [encoded = m_encoded, path = m_savePath,
             retainFile = m_retainFile](const ScreenshotExportCancellation& cancellation) {
                ScreenshotExportTaskResult result;
                const auto cancelled = [&cancellation] {
                    return cancellation.isCancellationRequested();
                };
                const auto saved =
                    encoded->pdf
                        ? ScreenshotImageFileService::writePdf(
                              *encoded->pdf, path,
                              ScreenshotPdfOptions{encoded->options.pdfPageSize,
                                                   encoded->options.quality,
                                                   {},
                                                   QDateTime::currentDateTimeUtc()},
                              cancelled, retainFile)
                        : ScreenshotImageFileService::writeEncodedFile(
                              encoded->path, path, encoded->options.format, cancelled, retainFile);
                result.savedPath = saved.path;
                result.error = saved.error;
                result.encodedSha256 = saved.encodedSha256;
                result.encodedByteCount = saved.encodedByteCount;
                result.reader = saved.reader;
                if (!saved.succeeded())
                    result.failureStage = ScreenshotExportFailureStage::File;
                return result;
            },
            [this](ScreenshotExportTaskResult result) {
                m_saveJob = {};
                if (closed())
                    return;
                if (!result.succeeded()) {
                    exportFailed(tr("The screenshot could not be saved: %1").arg(result.error));
                    return;
                }
                static_cast<void>(
                    snow_shot::storage::ScreenshotSettings().setLastManualSaveDirectory(
                        QFileInfo(result.savedPath).absolutePath()));
                m_lastSavedPath = result.savedPath;
                if (auto written = std::exchange(m_written, {})) {
                    const QPointer<SaveContent> guard(this);
                    written({result.savedPath,
                             {},
                             result.encodedSha256,
                             result.encodedByteCount,
                             result.reader},
                            m_encoded->options.format);
                    if (!guard || closed())
                        return;
                }
                if (!m_showSaveResult) {
                    m_modal->accept();
                    return;
                }
                showError({});
                m_result->present(
                    {m_lastSavedPath,
                     m_encoded->pixels,
                     {m_encoded->options.quality, m_encoded->options.compressionLevel}});
                setPhase(Phase::Result);
                startDecode();
                m_result->focusDefaultAction();
            });
        if (!m_saveJob.isValid())
            exportFailed(tr("The screenshot export queue is full"));
    }

    AdModal* m_modal;
    ScreenshotSaveAsFileDialog::Saved m_saved;
    ScreenshotSaveAsFileDialog::BeforeWrite m_beforeWrite;
    ScreenshotSaveAsFileDialog::Written m_written;
    bool m_retainFile;
    const bool m_showSaveResult = snow_shot::storage::ScreenshotSettings().showSaveResult();
    QString m_lastSavedPath;
    ScreenshotSaveFooter* m_footer = nullptr;
    ScreenshotSaveResult* m_result = nullptr;
    const snow_shot::storage::ScreenshotWindowNames m_windowNames;
    const QDateTime m_timestamp;
    QString m_savePath;
    ScreenshotExportJobHandle m_saveJob;
    DirectoryPathInput* m_directory = nullptr;
    AdLineEdit* m_filename = nullptr;
    QWidget* m_shortcutsHost = nullptr;
    adqt::widgets::detail::FlowLayout* m_shortcutsLayout = nullptr;
    QVector<Shortcut> m_shortcuts;
    QPointer<AdContextMenu> m_menu;
    QPointer<AdModal> m_editor;
    QPointer<AdModal> m_overwrite;
};

// Labels translated indirectly by the reusable form builder.
[[maybe_unused]] constexpr const char* labels[] = {
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Save path"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "File name"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "PDF"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Image format"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Width"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Height"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Quality"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Compression level"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Low"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Medium"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "High"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "0%"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "100%"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "PNG"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "JPEG"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "BMP"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "WebP"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "JPEG XL"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "AVIF"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "TIFF"),
};
} // namespace

ScreenshotSaveDialogState
ScreenshotSaveDialogState::initial(QSize size, const QDateTime& timestamp,
                                   const snow_shot::storage::ScreenshotWindowNames& names) {
    ScreenshotSaveDialogState result;
    const snow_shot::storage::ScreenshotSettings settings;
    const QFileInfo remembered(settings.lastManualSaveDirectory());
    const QString configured = snow_shot::storage::expandScreenshotExportVariables(
        settings.imageSaveDirectory(), timestamp, names);
    if (remembered.isDir() && remembered.isReadable() && remembered.isWritable())
        result.directory = remembered.absoluteFilePath();
    else if (!configured.isEmpty() && QDir::isAbsolutePath(configured))
        result.directory = QDir::cleanPath(configured);
    else
        result.directory = ScreenshotImageFileService::saveDialogDirectory(
            {}, settings.imageSaveDirectory(), timestamp, names);
    result.filename = ScreenshotImageFileService::suggestedBaseName(
        settings.manualSaveFilenameFormat(), timestamp, names);
    result.output.pdfPageSize = screenshot_pdf::pageSizeForKey(settings.pdfPageSize());
    result.sourceSize = size;
    result.output.size = size;
    result.output.format =
        ScreenshotImageFileService::formatForKey(settings.lastManualSaveFormat());
    const QJsonObject rememberedOptions =
        settings.manualSaveFormatOptions()
            .value(ScreenshotImageFileService::formatKey(result.output.format))
            .toObject();
    result.output.quality = rememberedOptions.value(QStringLiteral("quality")).toInt(100);
    result.output.compressionLevel = ScreenshotImageFileService::compressionLevelForKey(
        rememberedOptions.value(QStringLiteral("compression_level")).toString());
    return result;
}
void ScreenshotSaveDialogState::setDimension(bool width, int value) {
    if (width)
        output.size.setWidth(value);
    else
        output.size.setHeight(value);
    if (!lockAspectRatio || sourceSize.isEmpty() || value <= 0)
        return;
    const double other = width ? double(value) * sourceSize.height() / sourceSize.width()
                               : double(value) * sourceSize.width() / sourceSize.height();
    const int dimension = qRound(std::clamp(other, 1.0, 1000000.0));
    if (width)
        output.size.setHeight(dimension);
    else
        output.size.setWidth(dimension);
}
QString ScreenshotSaveDialogState::validationError() const {
    if (directory.trimmed().isEmpty() || !QDir::isAbsolutePath(directory.trimmed()))
        return QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                           "Please enter an absolute directory path");
    static const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QString name = filename.trimmed();
    if (name.isEmpty() || invalid.match(name).hasMatch() || reserved.match(name).hasMatch() ||
        name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.endsWith(QLatin1Char('.')))
        return QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                           "Please enter a valid file name");
    return imageValidationError();
}
QString ScreenshotSaveDialogState::imageValidationError() const {
    const QSize limits = pipeline::encoderLimits(output.format);
    if (output.size.isEmpty() || output.size.width() > limits.width() ||
        output.size.height() > limits.height() ||
        qint64(output.size.width()) * output.size.height() > qint64{2} * 1024 * 1024 * 1024)
        return QCoreApplication::translate("ScreenshotSaveAsFileDialog",
                                           "The dimensions are not supported by this image format");
    return {};
}
QString ScreenshotSaveDialogState::outputPath() const {
    return QDir(directory.trimmed())
        .filePath(snow_shot::storage::composeExportFileName(
            filename.trimmed(),
            QLatin1Char('.') + ScreenshotImageFileService::extension(output.format)));
}
bool ScreenshotSaveAsFileDialog::open(QObject* lifetime, QWidget* owner,
                                      std::shared_ptr<ScreenshotExportArtifact> source, Saved saved,
                                      Finished finished, QWidget* stackingOwner,
                                      Qt::WindowModality modality, BeforeWrite beforeWrite,
                                      bool retainFile) {
    if (!lifetime || !owner || !source || !source->isValid())
        return false;
    const QPointer<QWidget> stackingWindow(
        stackingOwner && stackingOwner->isVisible() ? stackingOwner : nullptr);
    auto* modal = new AdModal(lifetime);
    modal->setObjectName(QStringLiteral("screenshotSaveAsFileModal"));
    modal->setOwnerWindow(owner);
    modal->setMode(AdModal::Mode::Window);
    if (stackingWindow) {
        QObject::connect(stackingWindow, &QObject::destroyed, modal, &AdModal::reject);
    }
    modal->setWindowModality(modality);
    modal->setCentered(true);
    modal->setMaskVisible(false);
    modal->setCloseOnMaskClick(false);
    modal->setClosePolicy(AdModal::ClosePolicy::Manual);
    const QRect available = owner->screen()->availableGeometry();
    modal->setPreferredWidth(std::min(1080, available.width() - 40));
    auto* content = new SaveContent(modal, std::move(source), std::move(saved),
                                    std::move(beforeWrite), retainFile);
    modal->setContentWidget(content);
    QObject::connect(modal, &AdModal::closeRequested, content,
                     [modal, content](AdModal::CloseReason reason) {
                         if (content->saving())
                             return;
                         if (reason == AdModal::CloseReason::OkAction)
                             content->acceptAction();
                         else
                             modal->reject();
                     });
    QObject::connect(modal, &AdModal::finished, content,
                     [content, finished = std::move(finished)](AdModal::DialogCode) {
                         content->finish(finished);
                     });
    QObject::connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
    if (lifetime != owner)
        QObject::connect(owner, &QObject::destroyed, modal, &AdModal::reject);
    modal->open();
    // Preserve the display owner for centering and lifetime, while making the native dialog
    // an owned window of the toolbar. Raising an owner cannot cover its owned dialog.
    if (stackingWindow) {
        QWidget* surface = content->window();
        surface->windowHandle()->setTransientParent(stackingWindow->windowHandle());
#if defined(Q_OS_WIN)
        // QWidget keeps its original parent; synchronize the HWND owner as well as QWindow.
        if (QApplication::platformName() == QStringLiteral("windows")) {
            const auto dialogHwnd = reinterpret_cast<HWND>(surface->winId());
            const auto toolbarHwnd = reinterpret_cast<HWND>(stackingWindow->winId());
            SetWindowLongPtr(dialogHwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(toolbarHwnd));
        }
#endif
        surface->raise();
    }
    content->installFooter();
    modal->acceptButton()->setEnabled(false);
    return true;
}
bool ScreenshotSaveAsFileDialog::open(QObject* lifetime, QWidget* owner, const QImage& image,
                                      Saved saved, Finished finished, QWidget* stackingOwner,
                                      Qt::WindowModality modality, BeforeWrite beforeWrite,
                                      bool retainFile) {
    if (image.isNull())
        return false;
    return open(
        lifetime, owner,
        std::make_shared<ScreenshotExportArtifact>(ScreenshotExportSource::fromImage(image)),
        std::move(saved), std::move(finished), stackingOwner, modality, std::move(beforeWrite),
        retainFile);
}
