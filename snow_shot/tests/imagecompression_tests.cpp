// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/imagecompressionwindow.h"
#include "snow_shot/presentation/screenshotclipboardservice.h"
#include "snow_shot/presentation/screenshotexportcoordinator.h"
#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/capturehistoryrepository.h"
#include "snow_shot/storage/pinnedwindowrepository.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snowimageqtcodec.h"
#include "export_dialog_test_support.h"
#include "physical_key_test_support.h"
#include "theme/theme_manager.h"
#include "widgets/button.h"
#include "widgets/input_number.h"
#include "widgets/message.h"
#include "widgets/modal.h"
#include "widgets/segmented.h"
#include "widgets/select.h"
#include "widgets/slider.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QUuid>
#include <limits>
#include <source_location>
#include <stdexcept>

namespace {
using namespace adqt::widgets;
using namespace snow_shot;
using presentation::ImageCompressionWindowCoordinator;

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void until(const std::function<bool()>& ready,
           const std::source_location where = std::source_location::current()) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 30000) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    if (!ready())
        throw std::runtime_error("Compression window timed out at line " +
                                 std::to_string(where.line()));
}
template <typename T> T* child(QObject* parent, const char* name) {
    auto* widget = parent->findChild<T*>(QString::fromLatin1(name));
    require(widget != nullptr, name);
    return widget;
}
QByteArray contents(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open compressed file");
    return file.readAll();
}
class Selection final : public platform::SelectedFileBackend {
  public:
    QStringList paths;
    platform::SelectedFileTarget captureTarget() const override {
        return {1, 1};
    }
    platform::SelectedFileResult selectedFiles(const platform::SelectedFileTarget&,
                                               const std::function<bool()>&) const override {
        return {paths, platform::SelectedFileError::None};
    }
};
void dropFile(QWidget* surface, const QString& path) {
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPointF(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &enter);
    require(enter.isAccepted(), "The preview accepts dropped image files");
    QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &drop);
    require(drop.isAccepted(), "The preview receives the dropped file");
}
void pasteKey(QWidget* receiver) {
    receiver->window()->activateWindow();
    receiver->setFocus();
    QApplication::processEvents();
    require(receiver->hasFocus(), "The paste receiver has keyboard focus");
    const auto key = QKeySequence::keyBindings(QKeySequence::Paste).first()[0];
    PhysicalKeyEvent press(QEvent::KeyPress, key.key(), key.keyboardModifiers());
    QApplication::sendEvent(receiver, &press);
    PhysicalKeyEvent release(QEvent::KeyRelease, key.key(), key.keyboardModifiers());
    QApplication::sendEvent(receiver, &release);
}

void clipboardImports(const QString& path, const QSize& fileSize) {
    ImageCompressionWindowCoordinator windows;
    windows.presentSelectedFiles({}, {});
    auto* modal = child<AdModal>(&windows, "imageCompressionWindow");
    auto* editor = modal->contentWidget();
    auto* select = child<AdButton>(editor, "imageCompressionAddImage");
    auto* copy = child<AdButton>(modal->footerWidget(), "imageCompressionCopy");
    auto* canvas = editor->findChild<ScreenshotSavePreviewCanvas*>();
    require(canvas, "The compression window has a comparison preview");
    auto* clipboard = QApplication::clipboard();
    const auto clearClipboard = qScopeGuard([clipboard] { clipboard->clear(); });

    clipboard->setText(QStringLiteral("plain text"));
    pasteKey(select);
    require(select->isVisible() && select->isEnabled() && !copy->isEnabled(),
            "Plain text leaves the compression window empty");

    QImage image(73, 47, QImage::Format_ARGB32);
    image.fill(QColor(21, 90, 141, 170));
    auto* mixed = new QMimeData;
    mixed->setImageData(image);
    mixed->setUrls({QUrl::fromLocalFile(path)});
    clipboard->setMimeData(mixed);
    pasteKey(select);
    clipboard->clear();
    until([&] { return copy->isEnabled() && canvas->outputImage().size() == image.size(); });
    require(
        canvas->outputImage().pixelColor(0, 0) == image.pixelColor(0, 0),
        "Pasting prioritizes clipboard pixels and retains their alpha after the clipboard changes");

    auto* files = new QMimeData;
    files->setUrls({QUrl::fromLocalFile(path)});
    clipboard->setMimeData(files);
    pasteKey(canvas);
    until([&] { return copy->isEnabled() && canvas->outputImage().size() == fileSize; });

    auto* broken = new QMimeData;
    broken->setData(QStringLiteral("image/png"), QByteArrayLiteral("not a PNG"));
    clipboard->setMimeData(broken);
    pasteKey(canvas);
    until([&] {
        return copy->isEnabled() && !child<QLabel>(editor, "saveErrorLabel")->text().isEmpty();
    });
    require(canvas->outputImage().size() == fileSize,
            "A failed clipboard replacement preserves the existing source");

    auto* width = child<AdInputNumber>(editor, "saveWidthInput")->findChild<QLineEdit*>();
    require(width, "The width control has a text input");
    width->setFocus();
    width->selectAll();
    clipboard->setText(QStringLiteral("120"));
    pasteKey(width);
    require(width->text() == QStringLiteral("120"), "The width field still accepts text paste");
    modal->reject();
}

AdMessageHandle* exportNotice(QWidget* window) {
    for (auto* handle : window->window()->findChildren<AdMessageHandle*>()) {
        if (handle->isOpen() && handle->key() == QStringLiteral("imageCompressionExport"))
            return handle;
    }
    return nullptr;
}
void waitForExport(AdModal* modal) {
    const QPointer<AdModal> window(modal);
    until([&] { return !window || !window->isOpen() || exportNotice(window->contentWidget()); });
    require(window && window->isOpen(), "Export leaves the compression window open");
    const auto* message = exportNotice(window->contentWidget());
    require(message && message->type() == AdMessage::Type::Success &&
                message->content() == QStringLiteral("Export completed"),
            "Export displays a success message in the compression window");
    for (const auto* name : {"imageCompressionSave", "imageCompressionCopy"}) {
        const auto* button = child<AdButton>(window->footerWidget(), name);
        require(button->isEnabled() && !button->busy(), "Export actions can be used again");
    }
}

void saveCollisionAndDialogTranslations(const ScreenshotPinnedImageSnapshot& snapshot,
                                        const QTemporaryDir& dir) {
    QTranslator translator;
    require(translator.load(QStringLiteral(":/i18n/snow_shot_zh_CN.qm")),
            "Load compression dialog translations");
    const auto translate = [&](const char* source) {
        return translator.translate("ImageCompressionWindow", source);
    };
    ImageCompressionWindowCoordinator windows;
    windows.presentSnapshot(snapshot);
    auto* modal = child<AdModal>(&windows, "imageCompressionWindow");
    auto* editor = modal->contentWidget();
    auto* save = child<AdButton>(modal->footerWidget(), "imageCompressionSave");
    child<AdSelect>(editor, "saveFormatSelect")->setCurrentValue(QStringLiteral("png"));
    until([&] { return editor->property("previewGeneration").toUInt() > 0; });

    const auto target = dir.filePath(QStringLiteral("save-collision.png"));
    save->click();
    QPointer<QFileDialog> picker = editor->window()->findChild<QFileDialog*>();
    require(picker && picker->windowTitle() == QStringLiteral("Save") &&
                picker->acceptMode() == QFileDialog::AcceptSave &&
                picker->fileMode() == QFileDialog::AnyFile &&
                !picker->testOption(QFileDialog::DontUseNativeDialog),
            "The save picker starts in English and allows the system dialog");
#ifdef Q_OS_MACOS
    require(!picker->testOption(QFileDialog::DontConfirmOverwrite),
            "The macOS save picker confirms replacement of the selected path");
#else
    require(picker->testOption(QFileDialog::DontConfirmOverwrite),
            "The save picker defers overwrite confirmation to the compression window");
#endif
    require(QApplication::installTranslator(&translator), "Switch to Simplified Chinese");
    until([&] { return picker->windowTitle() == translate("Save"); });
    require(picker->nameFilters() == QStringList{translator.translate("ScreenshotImageFileService",
                                                                      "PNG image (*.png)")},
            "The open save picker updates its name filter");
    QApplication::removeTranslator(&translator);
    until([&] { return picker->windowTitle() == QStringLiteral("Save"); });

    const auto started = std::make_shared<QSemaphore>();
    const auto release = std::make_shared<QSemaphore>();
    const auto blocker = ScreenshotExportCoordinator::shared().submit(
        editor, ScreenshotExportCoordinator::Priority::Foreground,
        [started, release](const auto&) {
            started->release();
            release->acquire();
            return ScreenshotExportTaskResult{};
        },
        [](const auto&) {}, std::numeric_limits<qint64>::max());
    require(blocker.isValid(), "Queue the worker that delays publication");
    auto unblock = qScopeGuard([release] { release->release(); });
    until([&] { return started->available() > 0; });

    picker->setOption(QFileDialog::DontConfirmOverwrite, false);
    picker->setDirectory(dir.path());
    child<QLineEdit>(picker, "fileNameEdit")->setText(QFileInfo(target).fileName());
    require(picker->selectedFiles() == QStringList{target},
            "The picker selects the unconfirmed destination");
    QMetaObject::invokeMethod(picker, "accept");
    require(save->busy(), "The unconfirmed save queues its publication");
    QFile lateFile(target);
    require(lateFile.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                lateFile.write("other export") == 12,
            "A competing export creates the destination");
    lateFile.close();
    release->release();
    unblock.dismiss();
    until([&] { return !picker && save->isEnabled() && !save->busy(); });
    require(contents(target) == "other export", "The save preserves the competing file");
    require(!exportNotice(editor), "The competing file prevents a success notice");
    require(!child<QLabel>(editor, "saveErrorLabel")->text().isEmpty(),
            "The save reports the destination collision");

    save->click();
    picker = editor->window()->findChild<QFileDialog*>();
    require(picker, "A failed save can be retried");
    picker->setOption(QFileDialog::DontConfirmOverwrite, false);
    picker->setDirectory(dir.path());
    picker->selectFile(QFileInfo(target).fileName());
    bool pickerConfirmed = false;
    QTimer::singleShot(0, picker.data(), [&] {
        if (auto* prompt = picker->findChild<QMessageBox*>()) {
            pickerConfirmed = true;
            prompt->button(QMessageBox::Yes)->click();
        }
    });
    QMetaObject::invokeMethod(picker, "accept");
    require(pickerConfirmed && save->busy() &&
                !editor->findChild<AdModal*>(QStringLiteral("imageCompressionOverwrite")),
            "The file picker's confirmation starts export without a second prompt");
    waitForExport(modal);
    until([&] { return !picker; });

    save->click();
    picker = editor->window()->findChild<QFileDialog*>();
    require(picker, "Save can reopen after replacing an existing file");
    picker->setOption(QFileDialog::DontConfirmOverwrite, false);
    picker->setDirectory(dir.path());
    child<QLineEdit>(picker, "fileNameEdit")->setText(QStringLiteral("save-collision.jpg"));
    QMetaObject::invokeMethod(picker, "accept");
    auto* confirm = child<AdModal>(editor, "imageCompressionOverwrite");
    require(confirm->text().contains(QStringLiteral("save-collision.png")),
            "Overwrite confirmation names the normalized destination");
    require(QApplication::installTranslator(&translator), "Switch the open confirmation language");
    until([&] { return confirm->windowTitle() == translate("Replace file"); });
    require(confirm->text() == translate("Replace the existing file \"%1\"?")
                                   .arg(QStringLiteral("save-collision.png")) &&
                confirm->acceptText() == translate("Replace") &&
                confirm->rejectText() == translate("Cancel"),
            "The open overwrite confirmation retranslates its message and buttons");
    QApplication::removeTranslator(&translator);
    until([&] { return confirm->windowTitle() == QStringLiteral("Replace file"); });
    confirm->accept();
    waitForExport(modal);
    require(image_codec::decodeFile(target, snow::image::Format::png).size() == snapshot.outputSize,
            "Explicit approval permits replacing the existing file");
}
} // namespace
void runImageCompressionTests() {
    QTemporaryDir dir;
    require(dir.isValid(), "Test directory is missing");
    QImage source(480, 320, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixel(x, y,
                            qRgb((x + y / 3) % 256, (y + x / 4) % 256,
                                 ((x / 8 + y / 8) % 2 == 0 ? 48 : 162) + (x % 64)));
    const auto first = dir.filePath(QStringLiteral("one.png"));
    const auto second = dir.filePath(QStringLiteral("two.png"));
    require(source.save(first) && source.save(second), "Save image inputs");
    storage::ScreenshotSettings settings;
    require(settings.setLastManualSaveFormat(QStringLiteral("pdf")), "Set PDF default");
    ScreenshotPinnedImageSnapshot snapshot;
    snapshot.source = ScreenshotExportSource::fromImage(source);
    snapshot.outputSize = source.size();
    auto selection = std::make_shared<Selection>();
    {
        ImageCompressionWindowCoordinator windows;
        windows.presentSelectedFiles(selection, {});
        auto* modal = child<AdModal>(&windows, "imageCompressionWindow");
        auto* editor = modal->contentWidget();
        auto* format = child<AdSelect>(editor, "saveFormatSelect");
        require(format->currentValue() == QStringLiteral("png") && format->options().size() == 7,
                "PDF defaults to PNG and the compression window offers seven raster formats");
        auto* copy = child<AdButton>(modal->footerWidget(), "imageCompressionCopy");
        require(!copy->isEnabled() && copy->accentRole() == AdButton::AccentRole::Primary,
                "Copy starts disabled and primary");
        auto* surface = child<QWidget>(editor, "imageCompressionSurface");
        auto* empty = child<QWidget>(surface, "imageCompressionEmptyState");
        auto* hint = child<QWidget>(surface, "imageCompressionDropHint");
        auto* select = child<AdButton>(empty, "imageCompressionAddImage");
        require(empty->isVisible() && !hint->isVisible() && select->isVisible() &&
                    select->isEnabled() &&
                    empty->accessibleName() == QStringLiteral("Select an image to compress") &&
                    empty->accessibleDescription() ==
                        QStringLiteral("Drag an image file here, or choose select image") &&
                    select->text() == QStringLiteral("Select image") &&
                    select->buttonStyle() == AdButton::ButtonStyle::Solid &&
                    select->accentRole() == AdButton::AccentRole::Primary &&
                    select->focusPolicy() == Qt::StrongFocus,
                "Empty compression shows an accessible drop prompt and primary select action");
        select->click();
        QPointer<QFileDialog> picker = editor->window()->findChild<QFileDialog*>();
        require(picker && picker->windowTitle() == select->text() &&
                    picker->fileMode() == QFileDialog::ExistingFile &&
                    !picker->testOption(QFileDialog::DontUseNativeDialog),
                "Select image allows the system picker for a single existing file");
        QTranslator translator;
        require(translator.load(QStringLiteral(":/i18n/snow_shot_zh_CN.qm")) &&
                    QApplication::installTranslator(&translator),
                "Switch the open image picker language");
        until([&] {
            return picker->windowTitle() ==
                   translator.translate("ImageSplitEditorWidget", "Select image");
        });
        require(picker->nameFilters().value(0).startsWith(
                    translator.translate("ImageCompressionWindow", "Images") + u' '),
                "The open image picker updates its name filter");
        require(empty->accessibleDescription() ==
                    translator.translate("ImageSplitEditorWidget",
                                         "Drag an image file here, or choose select image"),
                "The translated empty prompt matches image splitting");
        QApplication::removeTranslator(&translator);
        picker->reject();
        until([&] { return !picker; });
        QMimeData data;
        data.setUrls({QUrl::fromLocalFile(first)});
        QDragEnterEvent enter(QPointF(20, 20), Qt::CopyAction, &data, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(surface, &enter);
        require(enter.isAccepted() && empty->isVisible() && hint->isVisible() &&
                    hint->geometry() == surface->rect() &&
                    hint->accessibleName() == QStringLiteral("Drop one image to compress") &&
                    hint->accessibleDescription() ==
                        QStringLiteral("Drop one image file at a time"),
                "Dragging over an empty window shows the single-image import overlay");
        QDragLeaveEvent leave;
        QApplication::sendEvent(surface, &leave);
        require(empty->isVisible() && !hint->isVisible(), "Drag leave restores the empty prompt");
        select->click();
        picker = editor->window()->findChild<QFileDialog*>();
        require(picker, "The image picker can reopen after cancellation");
        picker->selectFile(first);
        QMetaObject::invokeMethod(picker, "accept");
        until([&] { return editor->property("previewGeneration").toUInt() > 0; });
        require(!empty->isVisible() && !hint->isVisible() &&
                    hint->accessibleName() ==
                        QStringLiteral("Drop one image to replace the current image"),
                "A loaded image clears the overlay and changes it to a replacement prompt");
        auto* canvas = editor->findChild<ScreenshotSavePreviewCanvas*>();
        require(canvas && canvas->splitRatio() == 0.5, "The preview divider starts centered");
        auto* fit = child<AdButton>(canvas, "savePreviewFit");
        auto* actual = child<AdButton>(canvas, "savePreviewActualSize");
        require(fit->isVisible() && fit->isEnabled() && actual->isVisible() && actual->isEnabled(),
                "The loaded compression preview exposes both floating zoom actions");
        const auto middle = canvas->rect().center();
        const QPoint right(canvas->width() * 3 / 4, middle.y());
        QMouseEvent press(QEvent::MouseButtonPress, middle, canvas->mapToGlobal(middle),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, right, canvas->mapToGlobal(right), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, right, canvas->mapToGlobal(right),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        for (auto* event : {&press, &move, &release})
            QApplication::sendEvent(canvas, event);
        require(canvas->splitRatio() > 0.74 && canvas->splitRatio() <= 0.75,
                "Dragging the center handle moves the comparison divider");
        const auto preview = canvas->outputImage();
        auto* output = child<QLabel>(modal->footerWidget(), "saveOutputDescription");
        const auto previousDescription = output->text();
        const auto bad = dir.filePath(QStringLiteral("broken.png"));
        QFile broken(bad);
        require(broken.open(QIODevice::WriteOnly) && broken.write("broken") == 6,
                "Write invalid fixture");
        broken.close();
        dropFile(surface, bad);
        require(output->text().isEmpty(), "Replacing the source clears stale export details");
        until([&] {
            return copy->isEnabled() && !child<QLabel>(editor, "saveErrorLabel")->text().isEmpty();
        });
        require(canvas->outputImage() == preview && output->text() == previousDescription,
                "Failed replacement preserves the previous output");
        auto* width = child<AdInputNumber>(editor, "saveWidthInput");
        auto* height = child<AdInputNumber>(editor, "saveHeightInput");
        child<AdSegmented>(editor, "saveSizeUnitSegmented")
            ->setCurrentValue(QStringLiteral("percentage"));
        width->setValue(50);
        require(height->value() == 50, "Locked percentage scales both axes");
        child<AdSegmented>(editor, "saveSizeUnitSegmented")
            ->setCurrentValue(QStringLiteral("pixels"));
        require(width->value() == source.width() / 2 && height->value() == source.height() / 2,
                "Switching units preserves pixel dimensions");
        dropFile(surface, second);
        until(
            [&] { return width->value() == source.width() && height->value() == source.height(); });
        modal->reject();
    }
    clipboardImports(first, source.size());
    {
        ImageCompressionWindowCoordinator windows;
        selection->paths = {first, dir.filePath(QStringLiteral("ignored.txt")), second};
        windows.presentSelectedFiles(selection, selection->captureTarget());
        until([&] {
            return windows.findChildren<AdModal*>(QStringLiteral("imageCompressionWindow"))
                       .size() == 2;
        });
    }
    {
        std::function<void(std::optional<ScreenshotPinnedImageSnapshot>)> deliver;
        ImageCompressionWindowCoordinator windows([&](const QString& id, auto done) {
            require(id == QStringLiteral("active-pin"), "Management requests its selected pin");
            deliver = std::move(done);
        });
        windows.presentPinnedRecord(QStringLiteral("active-pin"));
        auto* modal = child<AdModal>(&windows, "imageCompressionWindow");
        require(!child<AdButton>(modal->footerWidget(), "imageCompressionSave")->isEnabled(),
                "A pending pin snapshot cannot be exported");
        deliver(snapshot);
        until([&] { return modal->contentWidget()->property("previewGeneration").toUInt() > 0; });
        require(modal->contentWidget()
                        ->findChild<ScreenshotSavePreviewCanvas*>()
                        ->outputImage()
                        .size() == source.size(),
                "Management displays the immutable source-resolution snapshot");
        windows.shutdown();
        until([&] { return windows.findChildren<AdModal*>().isEmpty(); });
    }
    {
        storage::CaptureHistoryDraft draft;
        draft.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        draft.createdUtc = QDateTime::currentDateTimeUtc();
        draft.canvasBounds = source.rect();
        draft.selection.rectangle = source.rect();
        draft.selection.shadowColor = Qt::black;
        draft.canvasHistory = R"({"schemaVersion":1,"document":{},"history":{}})";
        draft.displays.push_back({QStringLiteral("display"), QStringLiteral("Display"), source});
        draft.resultImage = source.copy(10, 20, 83, 47);
        const auto stored = storage::ApplicationStorage::instance()
                                .captureHistory()
                                .publish(std::move(draft))
                                .get();
        require(stored.storage.success, "Publish a screenshot history result");
        ImageCompressionWindowCoordinator windows;
        windows.presentHistory(stored.record);
        auto* editor = child<AdModal>(&windows, "imageCompressionWindow")->contentWidget();
        until([&] { return editor->property("previewGeneration").toUInt() > 0; });
        require(editor->findChild<ScreenshotSavePreviewCanvas*>()->outputImage().size() ==
                    QSize(83, 47),
                "History compression opens the saved result at its original dimensions");
    }
    {
        storage::PinnedWindowRecord pin;
        pin.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pin.image = source;
        pin.canvasSourceRect = pin.contentCanvasRect = pin.surfaceCanvasRect = source.rect();
        pin.initialWindowSize = source.size();
        pin.nativeGeometry = QRect(QPoint{}, source.size() / 2);
        pin.opacityPercent = 50;
        require(storage::ApplicationStorage::instance().pinnedWindows().upsert(pin).success,
                "Persist a pin with a scaled, translucent window");
        ImageCompressionWindowCoordinator windows;
        windows.presentPinnedRecord(pin.id);
        auto* editor = child<AdModal>(&windows, "imageCompressionWindow")->contentWidget();
        until([&] { return editor->property("previewGeneration").toUInt() > 0; });
        const auto& rendered = editor->findChild<ScreenshotSavePreviewCanvas*>()->outputImage();
        require(rendered.size() == source.size() &&
                    rendered.pixelColor(10, 10) == source.pixelColor(10, 10),
                "A closed pin opens at source resolution and opacity");
    }
    saveCollisionAndDialogTranslations(snapshot, dir);
    QByteArray jpeg;
    for (const auto& key : {QStringLiteral("png"), QStringLiteral("jpeg"), QStringLiteral("bmp"),
                            QStringLiteral("webp"), QStringLiteral("jxl"), QStringLiteral("avif"),
                            QStringLiteral("tiff")}) {
        ImageCompressionWindowCoordinator windows;
        windows.presentSnapshot(snapshot);
        auto* modal = child<AdModal>(&windows, "imageCompressionWindow");
        auto* editor = modal->contentWidget();
        auto* format = child<AdSelect>(editor, "saveFormatSelect");
        auto* save = child<AdButton>(modal->footerWidget(), "imageCompressionSave");
        until([&] { return save->isEnabled(); });
        format->setCurrentValue(key);
        if (key == QStringLiteral("jpeg"))
            child<AdSlider>(editor, "saveQualitySlider")->setValue(10);
        auto* label = child<QLabel>(modal->footerWidget(), "saveOutputDescription");
        until([&] { return !label->text().isEmpty(); });
        if (key == QStringLiteral("jpeg")) {
            until([&] {
                return editor->property("previewGeneration") ==
                       editor->property("encodedGeneration");
            });
            for (const auto scheme :
                 {adqt::theme::ThemeScheme::Dark, adqt::theme::ThemeScheme::Light}) {
                adqt::theme::ThemeManager::instance().setColorScheme(scheme);
                QApplication::processEvents();
                const auto* copy = child<AdButton>(modal->footerWidget(), "imageCompressionCopy");
                require(label->mapToGlobal(label->rect().topRight()).x() <
                                save->mapToGlobal(QPoint()).x() &&
                            save->mapToGlobal(save->rect().topRight()).x() <
                                copy->mapToGlobal(QPoint()).x() &&
                            copy->focusPolicy() == Qt::StrongFocus,
                        "Footer fits the output and keyboard-accessible actions");
                captureExportTestWindow(modal, scheme == adqt::theme::ThemeScheme::Dark
                                                   ? QStringLiteral("compression-dark")
                                                   : QStringLiteral("compression-light"));
            }
            for (const auto& locale : {QStringLiteral("zh_CN"), QStringLiteral("zh_TW")}) {
                QTranslator translator;
                require(translator.load(QStringLiteral(":/i18n/snow_shot_%1.qm").arg(locale)) &&
                            QApplication::installTranslator(&translator),
                        "Load compression translation");
                QApplication::processEvents();
                require(modal->windowTitle() ==
                            translator.translate("ImageCompressionWindow", "Image compression"),
                        "Compression window retranslates in place");
                QApplication::removeTranslator(&translator);
                QApplication::processEvents();
            }
        }
        if (key == QStringLiteral("jpeg")) {
            // Simulate another open export window committing a different format.
            auto options = settings.manualSaveFormatOptions();
            options[QStringLiteral("webp")] = QJsonObject{{QStringLiteral("quality"), 63}};
            require(settings.setLastManualSaveState(QStringLiteral("webp"), options),
                    "Commit another window's format options");
        }
        const auto description = label->text();
        const auto codec = ScreenshotImageFileService::formatForKey(key);
        auto* quality = child<AdSlider>(editor, "saveQualitySlider");
        auto* compression = child<AdSelect>(editor, "saveCompressionLevelSelect");
        require(quality->isVisible() == ScreenshotImageFileService::supportsQuality(codec) &&
                    compression->isVisible() ==
                        ScreenshotImageFileService::supportsCompressionLevel(codec),
                "Each format exposes the same controls as manual save after theme and language "
                "changes");
        save->click();
        auto* picker = editor->window()->findChild<QFileDialog*>();
        require(picker != nullptr, "Save asks for a destination");
        const auto target =
            dir.filePath(QStringLiteral("export.") + ScreenshotImageFileService::extension(codec));
        picker->selectFile(target);
        QMetaObject::invokeMethod(picker, "accept");
        waitForExport(modal);
        const auto bytes = contents(target);
        const QString size =
            bytes.size() < 1024
                ? QString::number(bytes.size()) + QStringLiteral("B")
                : QString::number(qRound64(double(bytes.size()) / 1024)) + QStringLiteral("KB");
        require(description == QStringLiteral("480x320(100%) · ") + size,
                "Footer reports the actual dimensions and saved file size");
        require(image_codec::decodeFile(target, ScreenshotImageFileService::snowImageFormat(codec))
                        .size() == source.size(),
                "Encoded output has the requested dimensions");
        if (key == QStringLiteral("jpeg")) {
            jpeg = bytes;
            require(settings.manualSaveFormatOptions()
                            .value(QStringLiteral("webp"))
                            .toObject()
                            .value(QStringLiteral("quality"))
                            .toInt() == 63,
                    "Saving preserves format preferences updated by another window");
        }
    }
    const auto decoded = image_codec::decode(jpeg, snow::image::Format::jpeg, "result.jpg");
    require(!decoded.isNull() && decoded.convertToFormat(source.format()) != source,
            "Fixture exposes lossy differences");
    ImageCompressionWindowCoordinator windows;
    windows.presentSnapshot(snapshot);
    auto* modal = child<AdModal>(&windows, "imageCompressionWindow");
    auto* editor = modal->contentWidget();
    auto* copy = child<AdButton>(modal->footerWidget(), "imageCompressionCopy");
    until([&] { return copy->isEnabled(); });
    child<AdSelect>(editor, "saveFormatSelect")->setCurrentValue(QStringLiteral("jpeg"));
    auto* quality = child<AdSlider>(editor, "saveQualitySlider");
    quality->setValue(80);
    quality->setValue(10);
    const auto previousFormat = settings.lastManualSaveFormat();
    copy->click();
    require(copy->busy(), "The copy is pending before supersession");
    static_cast<void>(ScreenshotClipboardService::reservePublication());
    until([&] { return copy->isEnabled() && !copy->busy(); });
    require(!exportNotice(editor) && settings.lastManualSaveFormat() == previousFormat,
            "A superseded copy restores editing without reporting or remembering an export");
    for (bool withFile : {false, true}) {
        require(settings.setCopyImageFileToClipboard(withFile), "Set clipboard format preference");
        copy->click();
        require(copy->busy() && !exportNotice(editor), "A new copy clears the old success message");
        waitForExport(modal);
        until([&] {
            return editor->property("previewGeneration") == editor->property("encodedGeneration");
        });
        require(quality->value() == 10 &&
                    editor->findChild<ScreenshotSavePreviewCanvas*>()->outputImage().size() ==
                        source.size(),
                "Copy keeps the current settings and restores the comparison preview");
        const auto [png, path] = readExportTestClipboard();
        require(!path.isEmpty() == withFile, "Clipboard file preference is honored");
        const auto pasted = image_codec::decode(png, snow::image::Format::png, "clipboard.png")
                                .convertToFormat(QImage::Format_RGB32);
        require(pasted.size() == decoded.size(), "Clipboard image uses export dimensions");
        for (int y = 0; y < pasted.height(); ++y)
            for (int x = 0; x < pasted.width(); ++x)
                require(pasted.pixel(x, y) == decoded.pixel(x, y),
                        "Clipboard contains decoded compressed pixels");
        if (withFile) {
            require(settings.manualSaveFormatOptions()
                            .value(QStringLiteral("jpeg"))
                            .toObject()
                            .value(QStringLiteral("quality"))
                            .toInt() == 31,
                    "A repeat export preserves format preferences saved by another window");
            modal->reject();
            require(contents(path) == jpeg, "Clipboard file retains the exact JPEG after closing");
        } else {
            auto options = settings.manualSaveFormatOptions();
            options[QStringLiteral("jpeg")] = QJsonObject{{QStringLiteral("quality"), 31}};
            require(settings.setLastManualSaveState(QStringLiteral("jpeg"), options),
                    "Commit another window's JPEG preference between exports");
        }
    }
}
