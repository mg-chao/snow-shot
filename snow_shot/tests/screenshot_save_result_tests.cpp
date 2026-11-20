#include "export_dialog_test_support.h"
#include "snow_shot/presentation/screenshotsaveasfiledialog.h"
#include "snow_shot/platform/filemanager.h"
#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/presentation/globalshortcutmanager.h"
#include "snow_shot/presentation/settings/settingsbackend.h"
#include "snow_shot/presentation/settings/settingsregistry.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/storage/configurationschema.h"
#include "snowimageqtcodec.h"
#include "widgets/button.h"
#include "widgets/input_line_edit.h"
#include "widgets/input_number.h"
#include "widgets/modal.h"
#include "widgets/result.h"
#include "widgets/select.h"
#include "widgets/slider.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>
#include <QMimeData>
#include <stdexcept>
#include <cstring>
#include <array>
#include <source_location>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <ShlObj.h>
#endif

namespace {
std::function<bool(const QString&)> revealFile;
}

namespace snow_shot::platform {
// Replace the native launch boundary in this workflow executable so offscreen tests never
// open Explorer/Finder. file_manager_tests.cpp covers the real platform selection arguments.
bool revealInFileManager(const QString& path, const FileManagerLauncher&) {
    if (!revealFile)
        throw std::runtime_error("unexpected file-manager launch");
    return revealFile(path);
}
} // namespace snow_shot::platform

namespace {
using namespace adqt::widgets;
namespace storage = snow_shot::storage;
namespace settings = snow_shot::presentation::settings;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void flush() {
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
void await(const std::function<bool()>& predicate,
           const std::source_location& location = std::source_location::current()) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 15000) {
        flush();
        QThread::msleep(1);
    }
    if (!predicate())
        throw std::runtime_error("save result operation timed out at line " +
                                 std::to_string(location.line()));
}
template <typename T> T* child(QObject* parent, const char* name) {
    auto* result = parent->findChild<T*>(QString::fromLatin1(name));
    require(result != nullptr, name);
    return result;
}

void settingContracts() {
    const storage::ScreenshotSettings adapter;
    constexpr auto binding = settings::SettingsSwitchBinding::ScreenshotShowSaveResult;
    const QString id = QStringLiteral("screenshot.show-save-result");
    const auto& registry = settings::builtInSettingsRegistry();
    const auto* field = registry.fieldForSwitch(binding);
    require(field && field->id == id && !field->defaultValue.toBool() &&
                field->configurationKey == QStringLiteral("screenshot/show_save_result") &&
                field->reset == settings::SettingsSectionReset::ScreenshotOutput,
            "save result needs a false Boolean default and screenshot output reset scope");
    const auto& catalog = registry.catalog();
    const auto* section =
        catalog.section(QStringLiteral("files-history"), QStringLiteral("screenshots"));
    require(section, "screenshot output section missing");
    bool adjacent = false;
    for (qsizetype index = 1; index < section->items.size(); ++index)
        adjacent |=
            section->items[index].id == id &&
            section->items[index - 1].id == QStringLiteral("screenshot.save-as-file-dialog");
    require(adjacent, "save result must immediately follow the save dialog selector");
    snow_shot::presentation::GlobalShortcutManager shortcuts;
    settings::BuiltInSettingsBackend backend(shortcuts);
    require(backend.resetSection(settings::SettingsSectionReset::ScreenshotOutput),
            "reset output settings");
    settings::SettingsRuntimeSession session(registry, backend);
    require(!adapter.showSaveResult() && !session.state(id).enabled && session.state(id).visible &&
                !backend.applySwitchValue(binding, true),
            "system dialog must show a disabled false switch and reject UI writes");
    require(adapter.setSaveAsFileDialog(QStringLiteral("snow_shot")), "select Snow Shot");
    await([&] { return session.state(id).enabled; });
    require(session.submitDraft(id, true), "enable save result");
    await([&] { return adapter.showSaveResult() && session.state(id).acceptedValue.toBool(); });
    require(adapter.setSaveAsFileDialog(QStringLiteral("system")), "select system");
    await([&] { return !session.state(id).enabled; });
    require(adapter.showSaveResult() && session.state(id).acceptedValue.toBool(),
            "disabled preference survives");
    require(adapter.setSaveAsFileDialog(QStringLiteral("snow_shot")), "restore Snow Shot");
    await([&] { return session.state(id).enabled; });
    require(backend.resetSection(settings::SettingsSectionReset::ScreenshotSettings) &&
                adapter.showSaveResult(),
            "capture settings reset must preserve output preference");
    require(backend.resetSection(settings::SettingsSectionReset::ScreenshotOutput) &&
                !adapter.showSaveResult(),
            "output reset must clear save result");
}

struct Dialog {
    QWidget owner;
    QPointer<AdModal> modal;
    QStringList events;
    QStringList writes;
    QList<int> committedWriteAttempts;
    int writeAttempts = 0;
    QString savedPath;
    bool succeeded = false;
    QImage image{160, 100, QImage::Format_RGBA8888};

    Dialog(const QString& directory, bool showResult = true,
           const QString& format = QStringLiteral("png")) {
        const storage::ScreenshotSettings settings;
        require(settings.setSaveAsFileDialog(QStringLiteral("snow_shot")) &&
                    settings.setShowSaveResult(showResult) &&
                    settings.setCopyImageFileToClipboard(false) &&
                    settings.setLastManualSaveDirectory(directory) &&
                    settings.setLastManualSaveState(format, {}),
                "configure save result fixture");
        image.fill(QColor(13, 180, 71, 160));
        owner.resize(1200, 800);
        owner.show();
        require(
            ScreenshotSaveAsFileDialog::open(
                &owner, &owner, image,
                [this](const QString& path) {
                    events << QStringLiteral("saved");
                    savedPath = path;
                },
                [this](bool success) {
                    events << QStringLiteral("finished");
                    succeeded = success;
                },
                nullptr, Qt::ApplicationModal,
                [this] {
                    return [this,
                            attempt = ++writeAttempts](const ScreenshotImageFileSaveResult& saved,
                                                       ScreenshotImageFileFormat format) {
                        require(saved.succeeded() &&
                                    saved.encodedByteCount == QFileInfo(saved.path).size() &&
                                    !saved.encodedSha256.isEmpty() &&
                                    ScreenshotImageFileService::formatForPath(saved.path) == format,
                                "each write reports the committed format, digest and size");
                        writes << saved.path;
                        committedWriteAttempts << attempt;
                    };
                }),
            "open result fixture");
        modal = child<AdModal>(&owner, "screenshotSaveAsFileModal");
        await([&] {
            return modal->contentWidget()->property("previewGeneration").toULongLong() > 0;
        });
        child<AdLineEdit>(modal->contentWidget(), "saveFilenameInput")
            ->setText(QStringLiteral("Saved image \u56fe\u50cf"));
    }
    ~Dialog() {
        if (modal && modal->isOpen())
            modal->reject();
        flush();
    }
    QWidget* content() const {
        return modal->contentWidget();
    }
    AdButton* button(const char* name) const {
        return child<AdButton>(modal->footerWidget(), name);
    }
    AdResult* result() const {
        return child<AdResult>(content(), "saveResult");
    }
    void save() {
        modal->acceptButton()->click();
        await([&] { return result()->isVisible(); });
        require(events.isEmpty() && modal->isOpen(),
                "success must keep dialog and callbacks pending");
    }
    void complete() {
        await([&] { return events.size() == 2 && !modal; });
        require(succeeded &&
                    events == QStringList{QStringLiteral("finished"), QStringLiteral("saved")},
                "dismissal reports success exactly once, Finished before Saved");
    }
};

class ResultTranslator final : public QTranslator {
  public:
    bool isEmpty() const override {
        return false;
    }
    QString translate(const char* context, const char* source, const char*, int) const override {
        return QByteArray(context) == "ScreenshotSaveAsFileDialog"
                   ? QStringLiteral("T: ") + QString::fromUtf8(source)
                   : QString();
    }
};

class LongActionTranslator final : public QTranslator {
  public:
    bool isEmpty() const override {
        return false;
    }
    QString translate(const char* context, const char* source, const char*, int) const override {
        const QByteArray text(source);
        if (QByteArray(context) == "ScreenshotSaveAsFileDialog" &&
            (text == "Continue editing" || text == "Copy image file" || text == "Copy image path" ||
             text == "Open folder")) {
            const auto label = QString::fromUtf8(source);
            return label + QStringLiteral(" / ") + label + QStringLiteral(" / ") + label;
        }
        return {};
    }
};

class StableGeometry final : public QObject {
  public:
    StableGeometry(std::initializer_list<QWidget*> widgets) {
        for (auto* widget : widgets) {
            m_geometry.insert(widget, QRect(widget->mapToGlobal(QPoint()), widget->size()));
            widget->installEventFilter(this);
        }
    }
    void verify() const {
        if (!m_failure.isEmpty())
            throw std::runtime_error(m_failure.toStdString());
    }

  protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            auto* widget = static_cast<QWidget*>(object);
            const QRect current(widget->mapToGlobal(QPoint()), widget->size());
            const auto expected = m_geometry.value(widget);
            if (current != expected && m_failure.isEmpty())
                m_failure = QStringLiteral("Save result geometry changed for %1: %2,%3 %4x%5 -> "
                                           "%6,%7 %8x%9")
                                .arg(widget->objectName())
                                .arg(expected.x())
                                .arg(expected.y())
                                .arg(expected.width())
                                .arg(expected.height())
                                .arg(current.x())
                                .arg(current.y())
                                .arg(current.width())
                                .arg(current.height());
        }
        return QObject::eventFilter(object, event);
    }

  private:
    QHash<QWidget*, QRect> m_geometry;
    QString m_failure;
};

void resultLifecycle(const QString& directory) {
    {
        Dialog dialog(directory, false);
        dialog.modal->acceptButton()->click();
        dialog.complete();
        require(QFile::remove(dialog.savedPath), "remove option-off output");
    }
    {
        Dialog dialog(directory);
        dialog.modal->reject();
        flush();
        require(!dialog.succeeded && dialog.savedPath.isEmpty() && dialog.events.size() == 1,
                "cancel before saving reports failure without Saved");
    }
    Dialog dialog(directory);
    flush();
    StableGeometry geometry{
        dialog.content()->window(), dialog.content(),
        child<ScreenshotSavePreviewCanvas>(dialog.content(), "savePreviewCanvas"),
        dialog.modal->footerWidget()};
    // Snapshot the preference once when opening.
    require(storage::ScreenshotSettings().setShowSaveResult(false), "change preference while open");
    dialog.save();
    flush();
    geometry.verify();
    require(
        dialog.result()->status() == AdResult::Status::Success &&
            dialog.result()->title() == QStringLiteral("Saved successfully") &&
            !dialog.modal->rejectButton()->isVisible() &&
            !dialog.modal->acceptButton()->isVisible() &&
            dialog.button("saveOpenFolderButton")->text() == QStringLiteral("Open folder") &&
            dialog.button("saveOpenFolderButton")->accentRole() == AdButton::AccentRole::Primary &&
            dialog.button("saveOpenFolderButton")->buttonStyle() == AdButton::ButtonStyle::Solid &&
            dialog.button("saveOpenFolderButton")->isDefault(),
        "result replaces form and footer with a primary Open folder action");
    const QString firstPath = QDir::fromNativeSeparators(dialog.result()->subTitle());
    require(QFileInfo::exists(firstPath) &&
                !child<AdLineEdit>(dialog.content(), "saveFilenameInput")->isVisible(),
            "success shows the saved absolute path instead of export settings");
    const auto assertFooterFits = [&] {
        flush();
        QList<QRect> rectangles;
        for (auto* button : std::array<QPushButton*, 4>{dialog.button("saveContinueEditingButton"),
                                                        dialog.button("saveCopyImageFileButton"),
                                                        dialog.button("saveCopyImagePathButton"),
                                                        dialog.button("saveOpenFolderButton")}) {
            const QRect rect(button->mapTo(dialog.modal->footerWidget(), QPoint()), button->size());
            require(button->isVisible() &&
                        button->size().expandedTo(button->sizeHint()) == button->size() &&
                        dialog.modal->footerWidget()->rect().contains(rect),
                    "result footer buttons must fit at their natural size");
            for (const auto& previous : rectangles)
                require(!rect.intersects(previous), "result footer buttons must not overlap");
            rectangles << rect;
        }
        for (qsizetype index = 1; index < rectangles.size(); ++index)
            require(rectangles[index].top() == rectangles[0].top() &&
                        rectangles[index - 1].right() < rectangles[index].left(),
                    "result actions stay in their natural order on one row when they fit");
        require(rectangles.last().right() == dialog.modal->footerWidget()->rect().right(),
                "the natural-width action group aligns with the right edge of the footer");
    };
    auto* resultScroll = child<QScrollArea>(dialog.content(), "saveResultScroll");
    require(!resultScroll->viewport()->autoFillBackground() &&
                !resultScroll->widget()->autoFillBackground() &&
                !dialog.result()->autoFillBackground(),
            "the result and its scroll viewport must preserve the transparent modal background");
    const QString snapshot = qEnvironmentVariable("SNOW_TEST_SAVE_RESULT_SCREENSHOT");
    if (!snapshot.isEmpty())
        require(dialog.content()->window()->grab().save(snapshot), "save result screenshot");
    assertFooterFits();
    ResultTranslator translator;
    require(QApplication::installTranslator(&translator), "install result translator");
    flush();
    require(dialog.result()->title() == QStringLiteral("T: Saved successfully") &&
                dialog.button("saveContinueEditingButton")->text() ==
                    QStringLiteral("T: Continue editing") &&
                dialog.button("saveOpenFolderButton")->text() == QStringLiteral("T: Open folder"),
            "result and footer must retranslate while visible");
    assertFooterFits();
    QApplication::removeTranslator(&translator);
    flush();
    dialog.button("saveContinueEditingButton")->click();
    flush();
    geometry.verify();
    require(!dialog.result()->isVisible() &&
                dialog.modal->acceptButton()->text() == QStringLiteral("Save") &&
                dialog.modal->rejectButton()->isVisible() &&
                child<AdLineEdit>(dialog.content(), "saveFilenameInput")->isEnabled(),
            "Continue editing restores enabled settings and Save/Cancel");
    dialog.modal->acceptButton()->click();
    auto* overwrite = child<AdModal>(dialog.content(), "saveOverwriteModal");
    overwrite->reject();
    flush();
    require(dialog.events.isEmpty() && dialog.modal->isOpen() && dialog.writeAttempts == 1,
            "declining overwrite preserves saved result session");
    child<AdLineEdit>(dialog.content(), "saveDirectoryInput")
        ->setText(firstPath + QStringLiteral("/child"));
    dialog.modal->acceptButton()->click();
    await([&] { return child<QLabel>(dialog.content(), "saveErrorLabel")->isVisible(); });
    require(dialog.events.isEmpty() && !dialog.result()->isVisible() &&
                dialog.modal->rejectButton()->isEnabled(),
            "failed repeat save must restore editing without completing the earlier save");
    child<AdLineEdit>(dialog.content(), "saveDirectoryInput")->setText(directory);
    child<AdLineEdit>(dialog.content(), "saveFilenameInput")
        ->setText(QStringLiteral("second-save"));
    dialog.save();
    flush();
    geometry.verify();
    const QString secondPath = QDir::fromNativeSeparators(dialog.result()->subTitle());
    require(secondPath != firstPath && QFileInfo::exists(secondPath) &&
                dialog.writes == QStringList{firstPath, secondPath} &&
                dialog.committedWriteAttempts == QList<int>{1, 3},
            "repeated saves capture fresh state and report only successful writes");
    dialog.button("saveContinueEditingButton")->click();
    dialog.modal->rejectButton()->click();
    dialog.complete();
    require(dialog.savedPath == secondPath,
            "cancel after saving reports the latest successful path");
}

void wrappedResultActions(const QString& directory) {
    LongActionTranslator translator;
    require(QApplication::installTranslator(&translator), "install long action labels");
    const auto restore = qScopeGuard([&] { QApplication::removeTranslator(&translator); });
    Dialog dialog(directory);
    child<AdLineEdit>(dialog.content(), "saveFilenameInput")
        ->setText(QStringLiteral("wrapped-actions"));
    flush();
    StableGeometry geometry{
        dialog.content()->window(), dialog.content(),
        child<ScreenshotSavePreviewCanvas>(dialog.content(), "savePreviewCanvas"),
        dialog.modal->footerWidget()};
    dialog.save();
    flush();
    geometry.verify();
    QList<QRect> rectangles;
    int naturalWidth = 0;
    for (const auto* name : {"saveContinueEditingButton", "saveCopyImageFileButton",
                             "saveCopyImagePathButton", "saveOpenFolderButton"}) {
        auto* button = dialog.button(name);
        const QRect rect(button->mapTo(dialog.modal->footerWidget(), QPoint()), button->size());
        naturalWidth += button->sizeHint().width();
        require(button->size() == button->sizeHint() &&
                    dialog.modal->footerWidget()->rect().contains(rect),
                "wrapped actions preserve natural sizes within the footer");
        for (const auto& previous : rectangles)
            require(!rect.intersects(previous), "wrapped actions must not overlap");
        rectangles << rect;
    }
    require(naturalWidth > dialog.modal->footerWidget()->width() &&
                rectangles.last().top() > rectangles.first().top(),
            "actions wrap only when their natural widths exceed the available space");
    dialog.button("saveContinueEditingButton")->click();
    flush();
    geometry.verify();
    dialog.modal->reject();
    dialog.complete();
}

void resultActionsFollowAvailableWidth(const QString& directory) {
    LongActionTranslator translator;
    require(QApplication::installTranslator(&translator), "install responsive action labels");
    const auto restore = qScopeGuard([&] { QApplication::removeTranslator(&translator); });
    Dialog dialog(directory);
    child<AdLineEdit>(dialog.content(), "saveFilenameInput")
        ->setText(QStringLiteral("responsive-actions"));
    dialog.save();
    const auto verifyActions = [&] {
        flush();
        auto* window = dialog.content()->window();
        require(window->screen()->availableGeometry().contains(window->frameGeometry()),
                "the measured content and footer must fit inside the available desktop");
        QList<QRect> rectangles;
        for (const auto* name : {"saveContinueEditingButton", "saveCopyImageFileButton",
                                 "saveCopyImagePathButton", "saveOpenFolderButton"}) {
            auto* button = dialog.button(name);
            const QRect rect(button->mapTo(dialog.modal->footerWidget(), QPoint()), button->size());
            require(button->size() == button->sizeHint() &&
                        dialog.modal->footerWidget()->rect().contains(rect),
                    "changing dialog width must remeasure wrapped actions without clipping");
            for (const auto& previous : rectangles)
                require(!rect.intersects(previous), "responsive actions must not overlap");
            rectangles << rect;
        }
    };
    dialog.modal->setPreferredWidth(680);
    verifyActions();
    QApplication::removeTranslator(&translator);
    verifyActions();
    require(QApplication::installTranslator(&translator), "restore long action labels");
    verifyActions();
    dialog.button("saveContinueEditingButton")->click();
    QApplication::removeTranslator(&translator);
    dialog.modal->setPreferredWidth(900);
    require(QApplication::installTranslator(&translator), "translate hidden result actions");
    flush();
    StableGeometry geometry{dialog.content()->window(), dialog.content(),
                            dialog.modal->footerWidget()};
    child<AdLineEdit>(dialog.content(), "saveFilenameInput")
        ->setText(QStringLiteral("responsive-actions-again"));
    dialog.save();
    verifyActions();
    geometry.verify();
    dialog.modal->reject();
    dialog.complete();
}

#ifdef Q_OS_WIN
QByteArray nativeClipboardBytes(UINT format) {
    const auto handle = GetClipboardData(format);
    require(handle != nullptr, "clipboard format missing");
    const auto* data = static_cast<const char*>(GlobalLock(handle));
    require(data != nullptr, "clipboard data unavailable");
    const QByteArray result(data, qsizetype(GlobalSize(handle)));
    GlobalUnlock(handle);
    return result;
}
#endif

void copyImageResults(const QString& directory) {
    for (const auto& format :
         {QStringLiteral("png"), QStringLiteral("jpeg"), QStringLiteral("pdf")}) {
        Dialog dialog(directory, true, format);
        child<AdLineEdit>(dialog.content(), "saveFilenameInput")
            ->setText(QStringLiteral("Copy \u56fe\u50cf ") + format);
        child<AdInputNumber>(dialog.content(), "saveWidthInput")->setValue(80);
        if (format != QStringLiteral("png"))
            child<AdSlider>(dialog.content(), "saveQualitySlider")->setValue(30);
        dialog.save();
        await([&] {
            return child<ScreenshotSavePreviewCanvas>(dialog.content(), "savePreviewCanvas")
                       ->outputImage()
                       .size() == QSize(80, 50);
        });
        dialog.button("saveCopyImageFileButton")->click();
        require(!dialog.button("saveContinueEditingButton")->isEnabled() &&
                    !dialog.button("saveOpenFolderButton")->isEnabled(),
                "copy disables competing actions");
        dialog.complete();
        const auto [png, copiedFile] = readExportTestClipboard();
        const auto copiedImage =
            snow_shot::image_codec::decode(png, snow::image::Format::png, "copied.png");
        require(copiedImage.size() == QSize(80, 50) &&
                    copiedImage.pixelColor(20, 20) == dialog.image.pixelColor(20, 20) &&
                    QDir::fromNativeSeparators(copiedFile) == dialog.savedPath &&
                    QFileInfo::exists(dialog.savedPath),
                "clipboard must contain resized lossless pixels plus the actual PNG/JPEG/PDF file");
    }
}

void resultActions(const QString& directory) {
    QStringList revealedPaths;
    bool revealSucceeds = true;
    revealFile = [&](const QString& path) {
        revealedPaths.append(path);
        return revealSucceeds;
    };
    const auto resetReveal = qScopeGuard([] { revealFile = {}; });
    {
        Dialog dialog(directory);
        child<AdLineEdit>(dialog.content(), "saveFilenameInput")
            ->setText(QStringLiteral("copy-path"));
        dialog.save();
        dialog.button("saveCopyImagePathButton")->click();
        dialog.complete();
        require(QApplication::clipboard()->text() == QDir::toNativeSeparators(dialog.savedPath),
                "copy path uses the absolute saved path");
    }
    {
        Dialog dialog(directory);
        child<AdLineEdit>(dialog.content(), "saveFilenameInput")
            ->setText(QStringLiteral("folder \u56fe\u50cf, #1"));
        dialog.save();
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(dialog.button("saveOpenFolderButton"), &enter);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(dialog.button("saveOpenFolderButton"), &release);
        dialog.complete();
        require(revealedPaths == QStringList{dialog.savedPath} &&
                    QFileInfo(dialog.savedPath).isFile() && QDir::isAbsolutePath(dialog.savedPath),
                "primary action reveals the exact absolute saved file, including Unicode");
    }
    {
        QTemporaryDir removedDirectory;
        Dialog dialog(removedDirectory.path());
        dialog.save();
        const QString path = QDir::fromNativeSeparators(dialog.result()->subTitle());
        require(QFile::remove(path), "remove saved file to test retry");
        dialog.button("saveCopyImageFileButton")->click();
        await([&] { return dialog.button("saveCopyImageFileButton")->isEnabled(); });
        require(dialog.events.isEmpty() &&
                    child<QLabel>(dialog.content(), "saveResultError")->isVisible(),
                "copy failure retains a retryable result");
        ResultTranslator translator;
        require(QApplication::installTranslator(&translator), "install action error translator");
        flush();
        require(child<QLabel>(dialog.content(), "saveResultError")->text() ==
                    QStringLiteral("T: The saved file no longer exists"),
                "cached action errors must retranslate");
        QApplication::removeTranslator(&translator);
        require(dialog.image.save(path), "restore saved file");
        dialog.button("saveCopyImageFileButton")->click();
        dialog.complete();
    }
    {
        Dialog dialog(directory);
        child<AdLineEdit>(dialog.content(), "saveFilenameInput")
            ->setText(QStringLiteral("folder-retry"));
        dialog.save();
        const QString path = QDir::fromNativeSeparators(dialog.result()->subTitle());
        revealedPaths.clear();
        revealSucceeds = false;
        dialog.button("saveOpenFolderButton")->click();
        require(dialog.events.isEmpty() && dialog.modal->isOpen() && dialog.result()->isVisible() &&
                    revealedPaths == QStringList{path} &&
                    dialog.button("saveOpenFolderButton")->isEnabled() &&
                    child<QLabel>(dialog.content(), "saveResultError")->isVisible(),
                "folder launch failure retains a retryable result");
        revealSucceeds = true;
        dialog.button("saveOpenFolderButton")->click();
        dialog.complete();
        require(revealedPaths == QStringList{path, path} && dialog.savedPath == path,
                "retry reveals the same saved file and dismisses only after launch succeeds");
    }
}

void cancelPendingActions(const QString& directory) {
    for (const auto* name : {"saveCopyImagePathButton", "saveCopyImageFileButton"}) {
        Dialog dialog(directory);
        child<AdLineEdit>(dialog.content(), "saveFilenameInput")
            ->setText(QString::fromLatin1(name));
        dialog.save();
        QApplication::clipboard()->setText(QStringLiteral("unchanged"));
        dialog.button(name)->click();
        dialog.modal->reject();
        dialog.complete();
        require(QApplication::clipboard()->text() == QStringLiteral("unchanged"),
                "closing during pending copy cancels publication without losing save success");
    }
    QWidget owner;
    owner.show();
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    auto artifact =
        std::make_shared<ScreenshotExportArtifact>(ScreenshotExportSource::fromImage(image));
    auto lifetime = std::make_unique<QObject>();
    int completions = 0;
    require(ScreenshotSaveAsFileDialog::open(lifetime.get(), &owner, artifact, {},
                                             [&](bool) { ++completions; }),
            "open owned result dialog");
    QPointer<AdModal> modal = child<AdModal>(lifetime.get(), "screenshotSaveAsFileModal");
    await([&] { return modal->contentWidget()->property("previewGeneration").toULongLong() > 0; });
    child<AdLineEdit>(modal->contentWidget(), "saveFilenameInput")
        ->setText(QStringLiteral("owner-destruction"));
    modal->acceptButton()->click();
    await([&] { return child<AdResult>(modal->contentWidget(), "saveResult")->isVisible(); });
    child<AdButton>(modal->footerWidget(), "saveCopyImageFileButton")->click();
    lifetime.reset();
    flush();
    require(!modal && artifact->isCancelled() && completions == 0,
            "destroying lifetime during a result action cancels work without callbacks into a dead "
            "owner");
}
} // namespace

void runScreenshotSaveResultTests() {
    settingContracts();
    QTemporaryDir directory;
    require(directory.isValid(), "save result test directory unavailable");
    resultLifecycle(directory.path());
    wrappedResultActions(directory.path());
    resultActionsFollowAvailableWidth(directory.path());
    copyImageResults(directory.path());
    resultActions(directory.path());
    cancelPendingActions(directory.path());
    require(storage::ScreenshotSettings().setShowSaveResult(false), "restore result default");
}

std::pair<QByteArray, QString> readExportTestClipboard() {
    QByteArray png;
    QString copiedFile;
#ifdef Q_OS_WIN
    // Clipboard listeners can briefly hold the native clipboard after publication. Read both
    // formats under the same lock so they always describe a single publication.
    QDeadlineTimer deadline(3000);
    while (!OpenClipboard(nullptr)) {
        require(!deadline.hasExpired(), "open clipboard for verification");
        QThread::msleep(5);
    }
    const auto close = qScopeGuard([] { CloseClipboard(); });
    png = nativeClipboardBytes(RegisterClipboardFormatW(L"PNG"));
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        const auto files = nativeClipboardBytes(CF_HDROP);
        require(files.size() > qsizetype(sizeof(DROPFILES)), "clipboard file list missing");
        DROPFILES header{};
        std::memcpy(&header, files.constData(), sizeof(header));
        require(header.fWide && qsizetype(header.pFiles) < files.size(),
                "clipboard file list must be Unicode");
        copiedFile = QString::fromWCharArray(
            reinterpret_cast<const wchar_t*>(files.constData() + header.pFiles));
    }
#else
    const auto* mime = QApplication::clipboard()->mimeData();
    require(mime && mime->urls().size() <= 1, "unexpected clipboard publication");
    png = mime->data(QStringLiteral("image/png"));
    if (mime->hasUrls())
        copiedFile = mime->urls().first().toLocalFile();
#endif
    return {png, copiedFile};
}
