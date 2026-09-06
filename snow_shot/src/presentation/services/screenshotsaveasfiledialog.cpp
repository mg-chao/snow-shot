#include "snow_shot/presentation/screenshotsaveasfiledialog.h"

#include "snow_shot/presentation/components/aspectratiolockbutton.h"
#include "snow_shot/presentation/components/pathinput.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "snow_shot/storage/settingsadapters.h"
#include "screenshotsaveexportpipeline.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/detail/flow_layout.h"
#include "widgets/field_group.h"
#include "widgets/form.h"
#include "widgets/input_line_edit.h"
#include "widgets/input_number.h"
#include "widgets/modal.h"
#include "widgets/select.h"
#include "widgets/scroll_area.h"
#include "widgets/slider.h"
#include "theme/theme_manager.h"
#include "antd_icons.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
using namespace adqt::widgets;
namespace icons = adqt::icons::antd::outlined;
namespace pipeline = screenshot_save_export;
using Format = ScreenshotImageFileFormat;
using Shortcut = snow_shot::storage::ScreenshotSavePathShortcut;

constexpr int kSaveFormContentWidth = 328;
constexpr int kAspectRatioLockButtonSize = 32;
constexpr int kDimensionFieldWidth = (kSaveFormContentWidth - kAspectRatioLockButtonSize) / 2;
constexpr int kDimensionLockControlHeight = 62;

bool formatSupportsLossless(Format format) {
    return format == Format::Webp || format == Format::Jxl || format == Format::Avif;
}

QString translated(const char* text) {
    return QCoreApplication::translate("ScreenshotSaveAsFileDialog", text);
}

void configureForm(AdForm* form) {
    form->setFormLayout(AdForm::FormLayout::Vertical);
    form->setLabelAlign(AdForm::LabelAlign::Left);
    form->setColon(false);
    form->setRequiredMark(AdForm::RequiredMark::Hidden);
}

class SaveContent final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ScreenshotSaveAsFileDialog)
  public:
    SaveContent(AdModal* modal, std::shared_ptr<ScreenshotExportArtifact> artifact,
                ScreenshotSaveAsFileDialog::Saved saved)
        : m_modal(modal), m_artifact(std::move(artifact)), m_saved(std::move(saved)) {
        setObjectName(QStringLiteral("saveDialogContent"));
        m_state = ScreenshotSaveDialogState::initial({});
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(20);
        m_preview = new ScreenshotSavePreviewCanvas(this);
        layout->addWidget(m_preview, 1);
        auto* scroll = new AdScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFixedWidth(340);
        scroll->setMinimumHeight(200);
        m_form = new AdForm;
        configureForm(m_form);
        m_form->setContentsMargins(0, 0, 12, 0);
        scroll->setContentWidget(m_form);
        m_form->setAutoFillBackground(false);
        layout->addWidget(scroll);

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
        m_format = new AdSelect(m_form);
        m_format->setObjectName(QStringLiteral("saveFormatSelect"));
        addField("Image format", m_format, "format");

        auto* dimensions = new AdForm(m_form);
        configureForm(dimensions);
        dimensions->setObjectName(QStringLiteral("saveDimensionsForm"));
        dimensions->setFormLayout(AdForm::FormLayout::Inline);
        dimensions->setFixedWidth(kSaveFormContentWidth);
        m_width = number("saveWidthInput");
        m_height = number("saveHeightInput");
        auto* widthItem = dimensions->addField({}, m_width, QStringLiteral("width"));
        widthItem->setProperty("exportLabel", "Width");
        widthItem->setItemLayout(AdFormItem::ItemLayout::Vertical);
        widthItem->setFixedWidth(kDimensionFieldWidth);

        auto* lockControl = new QWidget(dimensions);
        lockControl->setFixedSize(kAspectRatioLockButtonSize, kDimensionLockControlHeight);
        auto* lockLayout = new QVBoxLayout(lockControl);
        lockLayout->setContentsMargins(0, 30, 0, 0);
        lockLayout->setSpacing(0);
        m_lock = new AspectRatioLockButton(lockControl);
        m_lock->setObjectName(QStringLiteral("saveAspectLockButton"));
        m_lock->setChecked(true);
        lockLayout->addWidget(m_lock);
        auto* lockItem = dimensions->addField({}, lockControl);
        lockItem->setItemLayout(AdFormItem::ItemLayout::Vertical);
        lockItem->setNoStyle(true);
        lockItem->setFixedWidth(kAspectRatioLockButtonSize);

        auto* heightItem = dimensions->addField({}, m_height, QStringLiteral("height"));
        heightItem->setProperty("exportLabel", "Height");
        heightItem->setItemLayout(AdFormItem::ItemLayout::Vertical);
        heightItem->setFixedWidth(kDimensionFieldWidth);
        auto* dimensionsItem = m_form->addField({}, dimensions, QStringLiteral("dimensions"));
        dimensionsItem->setNoStyle(true);

        m_quality = new AdSlider(m_form);
        m_quality->setObjectName(QStringLiteral("saveQualitySlider"));
        m_quality->setRange(1, 100);
        m_quality->setValue(100);
        m_quality->setSingleStep(1);
        addField("Quality", m_quality, "quality");
        m_error = new QLabel(m_form);
        m_error->setObjectName(QStringLiteral("saveErrorLabel"));
        m_error->setWordWrap(true);
        auto* errorItem = m_form->addField({}, m_error);
        errorItem->setNoStyle(true);
        m_error->hide();
        m_form->layout()->setAlignment(Qt::AlignTop);
        connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged,
                this, [this] {
                    showError(m_error->text());
                    m_preview->update();
                });

        m_debounce.setSingleShot(true);
        m_debounce.setInterval(140);
        connect(&m_debounce, &QTimer::timeout, this, [this] { startRender(); });
        m_menuCloseTimer.setSingleShot(true);
        m_menuCloseTimer.setInterval(150);
        connect(&m_menuCloseTimer, &QTimer::timeout, this, [this] {
            if (m_menu && m_menu->isVisible() && !shortcutMenuContains(QCursor::pos()))
                m_menu->hide();
        });
        connect(m_directory, &DirectoryPathInput::textChanged, this, [this](const QString& value) {
            m_state.directory = value;
            validateFields();
        });
        connect(m_filename, &AdLineEdit::textChanged, this, [this](const QString& value) {
            m_state.filename = value;
            validateFields();
        });
        connect(m_format, &AdSelect::currentValueChanged, this, [this](const QVariant& value) {
            m_state.output.format = ScreenshotImageFileService::formatForKey(value.toString());
            changed();
        });
        connect(m_width, &AdInputNumber::valueChanged, this,
                [this](double value) { dimensionChanged(true, value); });
        connect(m_height, &AdInputNumber::valueChanged, this,
                [this](double value) { dimensionChanged(false, value); });
        connect(m_width, &AdInputNumber::hasValueChanged, this, [this](bool hasValue) {
            if (!hasValue)
                dimensionChanged(true, 0);
        });
        connect(m_height, &AdInputNumber::hasValueChanged, this, [this](bool hasValue) {
            if (!hasValue)
                dimensionChanged(false, 0);
        });
        connect(m_lock, &QAbstractButton::toggled, this, [this](bool checked) {
            m_state.lockAspectRatio = checked;
            if (checked && m_state.output.size.width() > 0)
                dimensionChanged(true, m_state.output.size.width());
            updateControls();
        });
        connect(m_quality, &AdSlider::valueChanged, this, [this](double value) {
            m_state.output.quality = qRound(value);
            changed();
        });
        retranslate();
        m_form->setDisabled(true);
        m_preview->setBusy(true);
        QTimer::singleShot(0, this, [this] { loadSource(); });
    }

    ~SaveContent() override {
        cancel(!m_closed);
    }
    void cancel(bool cancelSource = false) {
        m_closed = true;
        ++m_generation;
        m_debounce.stop();
        m_menuCloseTimer.stop();
        m_job.cancel();
        m_saveJob.cancel();
        if (cancelSource)
            m_artifact->cancel();
    }
    bool saving() const {
        return m_saving;
    }
    void save() {
        if (m_saving || !m_source.rows.isValid())
            return;
        m_state.directory = m_directory->text();
        m_state.filename = m_filename->text();
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
        if (event->type() == QEvent::Enter) {
            if (auto* button = qobject_cast<AdButton*>(object);
                button && button->property("shortcutIndex").isValid())
                openShortcutMenu(button, button->property("shortcutIndex").toInt());
        }
        if (m_menu && m_menu->isVisible() &&
            (object == m_menu || object == m_menu->triggerWidget())) {
            if (event->type() == QEvent::Enter) {
                m_menuCloseTimer.stop();
            } else if (event->type() == QEvent::Leave) {
                m_menuCloseTimer.start();
            } else if (event->type() == QEvent::MouseMove) {
                // QMenu also receives grabbed mouse moves outside its native popup window.
                const auto* mouse = static_cast<QMouseEvent*>(event);
                if (shortcutMenuContains(mouse->globalPosition().toPoint()))
                    m_menuCloseTimer.stop();
                else if (!m_menuCloseTimer.isActive())
                    m_menuCloseTimer.start();
            }
        }
        return QWidget::eventFilter(object, event);
    }

  private:
    AdInputNumber* number(const char* name) {
        auto* input = new AdInputNumber;
        input->setObjectName(QString::fromLatin1(name));
        input->setDecimals(0);
        input->setRange(0, 1000000);
        input->setSingleStep(1);
        input->setStepButtonLayout(AdInputNumber::StepButtonLayout::Compact);
        input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return input;
    }
    void addField(const char* label, QWidget* editor, const char* key) {
        auto* item = m_form->addField({}, editor, QString::fromLatin1(key));
        item->setProperty("exportLabel", label);
    }
    void validateFields() {
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
        m_width->setStatus(imageError.isEmpty() ? AdInputNumber::Status::None
                                                : AdInputNumber::Status::Error);
        m_height->setStatus(imageError.isEmpty() ? AdInputNumber::Status::None
                                                 : AdInputNumber::Status::Error);
        m_modal->acceptButton()->setEnabled(!m_saving && error.isEmpty());
    }
    void showError(const QString& message) {
        m_error->setText(message);
        m_error->setVisible(!message.isEmpty());
        const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
        QPalette palette = m_error->palette();
        palette.setColor(QPalette::WindowText, theme.colorError);
        m_error->setPalette(palette);
    }
    void retranslate() {
        m_modal->setWindowTitle(tr("Save as file"));
        m_modal->setAcceptText(tr("Save"));
        m_modal->setRejectText(tr("Cancel"));
        m_directory->setBrowseButtonText(tr("Select save directory"));
        m_lock->setToolTip(tr("Lock aspect ratio"));
        m_lock->setAccessibleName(m_lock->toolTip());
        m_directory->lineEdit()->setAccessibleName(tr("Save path"));
        m_filename->setAccessibleName(tr("File name"));
        m_format->setAccessibleName(tr("Image format"));
        m_width->setAccessibleName(tr("Width"));
        m_height->setAccessibleName(tr("Height"));
        m_quality->setAccessibleName(tr("Quality"));
        for (auto* item : findChildren<AdFormItem*>()) {
            const QByteArray source = item->property("exportLabel").toByteArray();
            if (!source.isEmpty())
                item->setLabel(translated(source.constData()));
        }
        const QSignalBlocker blocker(m_format);
        QVector<AdSelect::Option> options;
        for (const auto& entry : {std::pair<const char*, const char*>("png", "PNG"),
                                  {"jpeg", "JPEG"},
                                  {"webp", "WebP"},
                                  {"jxl", "JPEG XL"},
                                  {"avif", "AVIF"}}) {
            AdSelect::Option option;
            option.value = QString::fromLatin1(entry.first);
            option.label = translated(entry.second);
            options.push_back(option);
        }
        m_format->setOptions(options);
        const auto key = m_state.output.format == Format::Jpeg
                             ? QStringLiteral("jpeg")
                             : ScreenshotImageFileService::extension(m_state.output.format);
        m_format->setCurrentValue(key);
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
                expand->setIconRef(icons::Down());
                expand->setFixedSize(24, 24);
                expand->setToolTip(tr("Edit or delete save path"));
                expand->setAccessibleName(expand->toolTip());
                expand->setProperty("shortcutIndex", index);
                expand->installEventFilter(this);
                connect(expand, &QAbstractButton::clicked, this,
                        [this, expand, index] { openShortcutMenu(expand, index); });
                group->addControl(expand);
            }
            m_shortcutsLayout->addWidget(group);
        };
        add(tr("App directory"), snow_shot::storage::ScreenshotSettings().imageSaveDirectory(), -2);
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
    bool shortcutMenuContains(const QPoint& globalPosition) const {
        if (!m_menu)
            return false;
        const auto* trigger = m_menu->triggerWidget();
        return m_menu->rect().contains(m_menu->mapFromGlobal(globalPosition)) ||
               (trigger && trigger->rect().contains(trigger->mapFromGlobal(globalPosition)));
    }
    void openShortcutMenu(AdButton* trigger, int index) {
        if ((m_menu && m_menu->isVisible()) || index < 0 || index >= m_shortcuts.size())
            return;
        if (m_menu)
            m_menu->deleteLater();
        auto* menu = new AdContextMenu(this);
        m_menu = menu;
        menu->setObjectName(QStringLiteral("savePathMenu"));
        menu->setTriggerWidget(trigger);
        menu->installEventFilter(this);
        connect(menu, &QMenu::aboutToHide, &m_menuCloseTimer, &QTimer::stop);
        const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
        auto tokens = menu->componentTokens();
        tokens.minimumWidth = 0;
        tokens.text = theme.colorPrimary;
        tokens.hoverText = theme.colorPrimary;
        menu->setComponentTokens(tokens);
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
                showError(tr("The save paths could not be stored"));
        });
        menu->popupAt(trigger->mapToGlobal(QPoint(0, trigger->height())));
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
        auto* path = new AdLineEdit(form);
        path->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        path->setObjectName(QStringLiteral("savePathValueInput"));
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
                        pathItem->setErrorMessages({tr("The save paths could not be stored")});
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
    void dimensionChanged(bool width, double value) {
        m_state.setDimension(width, qRound(value));
        const QSignalBlocker widthBlock(m_width);
        const QSignalBlocker heightBlock(m_height);
        if (width && m_state.lockAspectRatio)
            m_height->setValue(m_state.output.size.height());
        if (!width && m_state.lockAspectRatio)
            m_width->setValue(m_state.output.size.width());
        changed();
    }
    void updateControls() {
        m_quality->setEnabled(m_state.output.format != Format::Png);
        AdSlider::Mark minimumMark;
        minimumMark.label = tr("0%");
        AdSlider::Mark maximumMark;
        maximumMark.label =
            formatSupportsLossless(m_state.output.format) ? tr("Lossless") : tr("100%");
        m_quality->setMarks(
            {{m_quality->minimum(), minimumMark}, {m_quality->maximum(), maximumMark}});
    }
    void changed() {
        updateControls();
        if (m_closed || !m_source.rows.isValid())
            return;
        ++m_generation;
        m_job.cancel();
        validateFields();
        showError({});
        m_preview->setOutput(m_source.preview, m_state.output.size);
        m_preview->setBusy(true);
        m_debounce.start();
    }
    void loadSource() {
        const bool started = m_artifact->requestRowSource(
            this, [this](ScreenshotImageRowSource rows, QString error) {
                if (m_closed)
                    return;
                if (!error.isEmpty() || !rows.isValid()) {
                    sourceFailed(error);
                    return;
                }
                auto result = std::make_shared<pipeline::Source>();
                m_job = ScreenshotExportCoordinator::shared().submit(
                    this, ScreenshotExportCoordinator::Priority::Foreground,
                    [rows, result](const ScreenshotExportCancellation& cancellation) {
                        ScreenshotExportTaskResult status;
                        *result = pipeline::prepare(rows, cancellation, &status.error);
                        if (!result->rows.isValid())
                            status.failureStage = ScreenshotExportFailureStage::Source;
                        return status;
                    },
                    [this, result](ScreenshotExportTaskResult status) {
                        m_job = {};
                        if (m_closed)
                            return;
                        if (!status.succeeded() || !result->rows.isValid()) {
                            sourceFailed(status.error);
                            return;
                        }
                        m_source = *result;
                        m_state.sourceSize = result->rows.size;
                        m_state.output.size = m_state.sourceSize;
                        const QSignalBlocker widthBlock(m_width);
                        const QSignalBlocker heightBlock(m_height);
                        m_width->setValue(m_state.sourceSize.width());
                        m_height->setValue(m_state.sourceSize.height());
                        m_form->setDisabled(false);
                        m_modal->acceptButton()->setEnabled(true);
                        m_preview->setSource(m_source.preview, m_state.sourceSize);
                        changed();
                    });
                if (!m_job.isValid())
                    sourceFailed(tr("The screenshot export queue is full"));
            });
        if (!started)
            sourceFailed(tr("The screenshot could not be prepared"));
    }
    void sourceFailed(const QString& error) {
        m_preview->setBusy(false);
        showError(tr("The screenshot could not be prepared: %1").arg(error));
    }
    void startRender() {
        if (m_closed || m_saving || !m_source.rows.isValid())
            return;
        if (m_renderRunning)
            return;
        const QString error = m_state.imageValidationError();
        if (!error.isEmpty()) {
            showError(error);
            m_preview->setBusy(false);
            if (m_saving)
                saveFailed(error);
            return;
        }
        const quint64 generation = m_generation;
        const auto options = m_state.output;
        auto encoded = std::make_shared<std::shared_ptr<pipeline::Encoded>>();
        m_renderRunning = true;
        m_job = ScreenshotExportCoordinator::shared().submit(
            this, ScreenshotExportCoordinator::Priority::Foreground,
            [source = m_source, options,
             encoded](const ScreenshotExportCancellation& cancellation) {
                ScreenshotExportTaskResult result;
                *encoded = pipeline::render(source, options, cancellation, &result.error, true);
                if (!*encoded)
                    result.failureStage = ScreenshotExportFailureStage::Render;
                return result;
            },
            [this, generation, encoded](ScreenshotExportTaskResult result) {
                m_renderRunning = false;
                m_job = {};
                if (m_closed)
                    return;
                if (generation != m_generation) {
                    if (!m_debounce.isActive())
                        startRender();
                    return;
                }
                m_preview->setBusy(false);
                if (!result.succeeded() || !*encoded) {
                    saveFailed(tr("The export could not be prepared: %1").arg(result.error));
                    return;
                }
                const auto& current = *encoded;
                if (!current->preview.isNull())
                    m_preview->setOutput(current->preview, m_state.output.size);
                else
                    showError(tr("The encoded preview could not be displayed: %1")
                                  .arg(current->previewError));
                setProperty("previewGeneration", QVariant::fromValue(generation));
            });
        if (!m_job.isValid()) {
            m_renderRunning = false;
            saveFailed(tr("The screenshot export queue is full"));
        }
    }
    void beginSave() {
        m_saving = true;
        m_form->setDisabled(true);
        m_modal->setAcceptButtonBusy(true);
        m_modal->setCloseButtonVisible(false);
        m_modal->setCloseOnEscape(false);
        if (m_modal->rejectButton())
            m_modal->rejectButton()->setEnabled(false);
        m_debounce.stop();
        ++m_generation;
        m_job.cancel();
        writeFile();
    }
    void saveFailed(const QString& error) {
        m_saving = false;
        m_preview->setBusy(false);
        m_form->setDisabled(false);
        m_modal->setAcceptButtonBusy(false);
        m_modal->setCloseButtonVisible(true);
        m_modal->setCloseOnEscape(true);
        if (m_modal->rejectButton())
            m_modal->rejectButton()->setEnabled(true);
        validateFields();
        showError(error);
    }
    void writeFile() {
        const QString path = m_state.outputPath();
        m_saveJob = ScreenshotExportCoordinator::shared().submit(
            this, ScreenshotExportCoordinator::Priority::Foreground,
            [source = m_source, options = m_state.output,
             path](const ScreenshotExportCancellation& cancellation) {
                ScreenshotExportTaskResult result;
                const auto encoded = pipeline::render(source, options, cancellation, &result.error);
                if (!encoded) {
                    result.failureStage = ScreenshotExportFailureStage::Render;
                    return result;
                }
                const auto saved = ScreenshotImageFileService::writeEncodedFile(
                    encoded->path, path, encoded->options.format,
                    [&cancellation] { return cancellation.isCancellationRequested(); });
                result.savedPath = saved.path;
                result.error = saved.error;
                if (!saved.succeeded())
                    result.failureStage = ScreenshotExportFailureStage::File;
                return result;
            },
            [this](ScreenshotExportTaskResult result) {
                m_saveJob = {};
                if (m_closed)
                    return;
                if (!result.succeeded()) {
                    saveFailed(tr("The screenshot could not be saved: %1").arg(result.error));
                    return;
                }
                static_cast<void>(
                    snow_shot::storage::ScreenshotSettings().setLastManualSaveDirectory(
                        QFileInfo(result.savedPath).absolutePath()));
                m_saving = false;
                const auto saved = m_saved;
                const auto path = result.savedPath;
                m_modal->accept();
                if (saved)
                    saved(path);
            });
        if (!m_saveJob.isValid())
            saveFailed(tr("The screenshot export queue is full"));
    }

    AdModal* m_modal;
    std::shared_ptr<ScreenshotExportArtifact> m_artifact;
    ScreenshotSaveAsFileDialog::Saved m_saved;
    ScreenshotSaveDialogState m_state;
    pipeline::Source m_source;
    ScreenshotExportJobHandle m_job;
    ScreenshotExportJobHandle m_saveJob;
    QTimer m_debounce;
    quint64 m_generation = 0;
    bool m_closed = false;
    bool m_saving = false;
    bool m_renderRunning = false;
    ScreenshotSavePreviewCanvas* m_preview = nullptr;
    AdForm* m_form = nullptr;
    DirectoryPathInput* m_directory = nullptr;
    AdLineEdit* m_filename = nullptr;
    AdSelect* m_format = nullptr;
    AdInputNumber* m_width = nullptr;
    AdInputNumber* m_height = nullptr;
    AspectRatioLockButton* m_lock = nullptr;
    AdSlider* m_quality = nullptr;
    QLabel* m_error = nullptr;
    QWidget* m_shortcutsHost = nullptr;
    adqt::widgets::detail::FlowLayout* m_shortcutsLayout = nullptr;
    QVector<Shortcut> m_shortcuts;
    QPointer<AdContextMenu> m_menu;
    QTimer m_menuCloseTimer;
    QPointer<AdModal> m_editor;
    QPointer<AdModal> m_overwrite;
};

// Labels translated indirectly by the reusable form builder.
[[maybe_unused]] constexpr const char* labels[] = {
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Save path"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "File name"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Image format"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Width"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Height"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "Quality"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "0%"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "100%"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "PNG"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "JPEG"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "WebP"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "JPEG XL"),
    QT_TRANSLATE_NOOP("ScreenshotSaveAsFileDialog", "AVIF"),
};
} // namespace

ScreenshotSaveDialogState ScreenshotSaveDialogState::initial(QSize size) {
    ScreenshotSaveDialogState result;
    const snow_shot::storage::ScreenshotSettings settings;
    const QFileInfo remembered(settings.lastManualSaveDirectory());
    const QString configured = settings.imageSaveDirectory().trimmed();
    if (remembered.isDir() && remembered.isReadable() && remembered.isWritable())
        result.directory = remembered.absoluteFilePath();
    else if (!configured.isEmpty() && QDir::isAbsolutePath(configured))
        result.directory = QDir::cleanPath(configured);
    else
        result.directory = ScreenshotImageFileService::saveDialogDirectory({}, configured);
    result.filename =
        ScreenshotImageFileService::suggestedBaseName(settings.manualSaveFilenameFormat());
    result.sourceSize = size;
    result.output.size = size;
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
    return ScreenshotImageFileService::normalizedPath(
        QDir(directory.trimmed()).filePath(filename.trimmed()), output.format);
}
bool ScreenshotSaveDialogState::lossless() const {
    return output.quality == 100 && formatSupportsLossless(output.format);
}

bool ScreenshotSaveAsFileDialog::open(QObject* lifetime, QWidget* owner,
                                      std::shared_ptr<ScreenshotExportArtifact> source, Saved saved,
                                      Finished finished) {
    if (!lifetime || !owner || !source || !source->isValid())
        return false;
    auto* modal = new AdModal(lifetime);
    modal->setObjectName(QStringLiteral("screenshotSaveAsFileModal"));
    modal->setOwnerWindow(owner);
    modal->setMode(AdModal::Mode::Window);
    modal->setWindowModality(Qt::ApplicationModal);
    modal->setCentered(true);
    modal->setMaskVisible(false);
    modal->setCloseOnMaskClick(false);
    modal->setClosePolicy(AdModal::ClosePolicy::Manual);
    const QRect available = owner->screen()->availableGeometry();
    modal->setPreferredWidth(std::min(1080, available.width() - 40));
    auto* content = new SaveContent(modal, std::move(source), std::move(saved));
    content->setFixedHeight(std::max(240, std::min(540, available.height() - 180)));
    modal->setContentWidget(content);
    QObject::connect(modal, &AdModal::closeRequested, content,
                     [modal, content](AdModal::CloseReason reason) {
                         if (content->saving())
                             return;
                         if (reason == AdModal::CloseReason::OkAction)
                             content->save();
                         else
                             modal->reject();
                     });
    QObject::connect(modal, &AdModal::finished, content,
                     [content, finished = std::move(finished)](AdModal::DialogCode code) {
                         content->cancel(code != AdModal::DialogCode::Accepted);
                         if (finished)
                             finished(code == AdModal::DialogCode::Accepted);
                     });
    QObject::connect(modal, &AdModal::finished, modal, &QObject::deleteLater);
    if (lifetime != owner)
        QObject::connect(owner, &QObject::destroyed, modal, &AdModal::reject);
    modal->open();
    modal->acceptButton()->setEnabled(false);
    return true;
}
bool ScreenshotSaveAsFileDialog::open(QObject* lifetime, QWidget* owner, const QImage& image,
                                      Saved saved, Finished finished) {
    if (image.isNull())
        return false;
    return open(
        lifetime, owner,
        std::make_shared<ScreenshotExportArtifact>(ScreenshotExportSource::fromImage(image)),
        std::move(saved), std::move(finished));
}
