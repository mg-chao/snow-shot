#include "snow_shot/presentation/screenshotsaveasfiledialog.h"
#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/presentation/globalshortcutmanager.h"
#include "snow_shot/presentation/settings/settingsbackend.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationschema.h"
#include "screenshotsaveexportpipeline.h"
#include "snowimageqtcodec.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/form.h"
#include "widgets/input_line_edit.h"
#include "widgets/input_number.h"
#include "widgets/modal.h"
#include "widgets/select.h"
#include "widgets/slider.h"
#include "widgets/tag.h"
#include "theme/theme_manager.h"

#include <QApplication>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QTranslator>
#include <QTemporaryDir>
#include <QThread>
#include <QWheelEvent>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace adqt::widgets;
namespace storage = snow_shot::storage;
namespace settings = snow_shot::presentation::settings;
namespace pipeline = screenshot_save_export;
using Format = ScreenshotImageFileFormat;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void processUntil(const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 30000) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    require(condition(), "timed out waiting for export dialog");
}
void flush() {
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
template <class T> T* child(QObject* parent, const char* name) {
    auto* result = parent->findChild<T*>(QString::fromLatin1(name));
    require(result != nullptr, name);
    return result;
}
QImage fixture(QSize size = QSize(160, 100)) {
    QImage image(size, QImage::Format_RGBA8888);
    image.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            image.setPixelColor(x, y, QColor((x * 9) % 256, (y * 17) % 256, (x + y) % 256));
    return image;
}
void snapshot(AdModal* modal, const QString& name) {
    const QString directory = qEnvironmentVariable("SNOW_EXPORT_TEST_SCREENSHOT_DIR");
    if (directory.isEmpty())
        return;
    require(QDir().mkpath(directory), "snapshot directory unavailable");
    flush();
    require(modal->contentWidget()->window()->grab().save(
                QDir(directory).filePath(name + QStringLiteral(".png"))),
            "dialog snapshot could not be written");
}
AdModal* openDialog(QWidget& owner, const QImage& image,
                    ScreenshotSaveAsFileDialog::Saved saved = {},
                    ScreenshotSaveAsFileDialog::Finished finished = {}) {
    require(ScreenshotSaveAsFileDialog::open(&owner, &owner, image, std::move(saved),
                                             std::move(finished)),
            "image dialog should open");
    auto* modal = child<AdModal>(&owner, "screenshotSaveAsFileModal");
    require(modal->mode() == AdModal::Mode::Window &&
                modal->windowModality() == Qt::ApplicationModal && !modal->maskVisible() &&
                modal->closePolicy() == AdModal::ClosePolicy::Manual,
            "export must use application-modal Ant window mode");
    processUntil(
        [&] { return modal->contentWidget()->property("previewGeneration").toULongLong() > 0; });
    flush();
    return modal;
}

void persistence(const QTemporaryDir& temp) {
    const QString key = QStringLiteral("screenshot/save_as_file_dialog");
    require(storage::ConfigurationSchema::defaultValue(key).toString() == QStringLiteral("system"),
            "system must be the default");
    for (const auto& value : {QStringLiteral("system"), QStringLiteral("snow_shot")})
        require(storage::ConfigurationSchema::normalize(key, value).valid,
                "supported dialog rejected");
    require(!storage::ConfigurationSchema::normalize(key, QStringLiteral("native")).valid &&
                !storage::ConfigurationSchema::normalize(key, 1).valid,
            "invalid dialog accepted");
    storage::ScreenshotSettings adapter;
    require(adapter.setSaveAsFileDialog(QStringLiteral("snow_shot")) &&
                adapter.saveAsFileDialog() == QStringLiteral("snow_shot"),
            "dialog setting did not persist");
    snow_shot::presentation::GlobalShortcutManager shortcuts;
    settings::BuiltInSettingsBackend backend(shortcuts);
    require(backend.selectValue(settings::SettingsSelectBinding::ScreenshotSaveAsFileDialog) ==
                QStringLiteral("snow_shot"),
            "settings backend failed to read dialog selection");
    require(backend.resetSection(settings::SettingsSectionReset::ScreenshotSettings) &&
                adapter.saveAsFileDialog() == QStringLiteral("system"),
            "reset must restore system dialog");
    require(backend.applySelectValue(settings::SettingsSelectBinding::ScreenshotSaveAsFileDialog,
                                     QStringLiteral("snow_shot")),
            "settings backend failed to write dialog selection");

    const QString pathKey = QStringLiteral("screenshot/save_path_shortcuts");
    const auto malformed =
        QJsonDocument::fromJson(
            R"([{"name":" Work ","path":" C:/work "},{"name":"work","path":"D:/duplicate"},{"name":"","path":"x"},{"name":"bad","path":2},42])")
            .array();
    const auto normalized = storage::ConfigurationSchema::normalize(pathKey, malformed);
    require(normalized.valid && normalized.changed && normalized.value.toArray().size() == 1,
            "shortcut normalization must discard malformed, blank and duplicate entries");
    require(!storage::ConfigurationSchema::normalize(pathKey, QJsonObject{}).valid,
            "shortcut configuration must be an array");
    const QVector<storage::ScreenshotSavePathShortcut> values{
        {QStringLiteral("Work"), temp.filePath("not-created")}};
    require(adapter.setSavePathShortcuts(values) && adapter.savePathShortcuts() == values,
            "shortcut values must round-trip before directory creation");
    require(
        !adapter.setSavePathShortcuts({values[0], {QStringLiteral("WORK"), QStringLiteral("x")}}),
        "duplicate shortcut writes must fail atomically");
    require(adapter.savePathShortcuts() == values &&
                !adapter.setSavePathShortcuts({{QStringLiteral(" "), QStringLiteral("x")}}),
            "invalid writes must preserve shortcuts");
    require(storage::ApplicationStorage::instance().flushNow().success,
            "configuration flush failed");
    QFile file(QDir(storage::ApplicationStorage::instance().configurationDirectory())
                   .filePath("config.json"));
    require(file.open(QIODevice::ReadOnly), "persisted configuration unavailable");
    require(QJsonDocument::fromJson(file.readAll())
                    .object()
                    .value("screenshot")
                    .toObject()
                    .value("save_path_shortcuts")
                    .toArray()
                    .size() == 1,
            "shortcuts were not persisted as structured JSON");
    require(adapter.setSavePathShortcuts({}), "shortcut cleanup failed");
}

void stateRules(const QTemporaryDir& temp) {
    storage::ScreenshotSettings settings;
    const QString configured = temp.filePath("configured");
    const QString remembered = temp.filePath("remembered");
    require(QDir().mkpath(remembered) && settings.setImageSaveDirectory(configured) &&
                settings.setLastManualSaveDirectory(remembered),
            "directory setup failed");
    auto state = ScreenshotSaveDialogState::initial(QSize(160, 100));
    require(state.directory == remembered && state.output.size == QSize(160, 100) &&
                state.output.format == Format::Png && state.output.quality == 100 &&
                state.lockAspectRatio,
            "opening controls must use defaults and remembered directory");
    require(settings.setLastManualSaveDirectory(temp.filePath("missing")),
            "remembered directory setup failed");
    require(
        ScreenshotSaveDialogState::initial(QSize(160, 100)).directory == configured,
        "invalid remembered directory must fall back to configured directory even before creation");
    state.setDimension(true, 80);
    require(state.output.size == QSize(80, 50),
            "locked width failed to maintain source aspect ratio");
    state.setDimension(false, 200);
    require(state.output.size == QSize(320, 200),
            "locked height failed to maintain source aspect ratio");
    state.lockAspectRatio = false;
    state.setDimension(false, 123);
    require(state.output.size == QSize(320, 123), "unlocked height must be independent");
    for (auto format : {Format::Png, Format::Jpeg, Format::Webp, Format::Jxl, Format::Avif}) {
        state.output.format = format;
        state.output.quality = 100;
        const bool supportsLossless =
            format == Format::Webp || format == Format::Jxl || format == Format::Avif;
        require(state.lossless() == supportsLossless, "lossless badge support is incorrect");
        const auto options = ScreenshotImageFileService::encodeOptions(format, 100);
        if (supportsLossless)
            require(options.lossless, "quality 100 must explicitly encode losslessly");
        state.output.quality = 75;
        require(!state.lossless(), "lossy export must not show lossless badge");
        require(state.outputPath().endsWith('.' + ScreenshotImageFileService::extension(format)),
                "output extension must match selected format");
    }
    state.output.size = QSize(0, 100);
    require(!state.validationError().isEmpty(), "zero dimensions must fail");
    state.output.format = Format::Webp;
    state.output.size = QSize(pipeline::encoderLimits(Format::Webp).width() + 1, 100);
    require(!state.imageValidationError().isEmpty(), "encoder-limit dimensions must fail");
    state.output.size = QSize(160, 100);
    state.filename = QStringLiteral("CON.png");
    require(!state.validationError().isEmpty() && state.imageValidationError().isEmpty(),
            "filename errors must not block preview rendering");
}

void encodingAndBoundedSources() {
    QObject receiver;
    bool done = false;
    QString failure;
    const auto job = ScreenshotExportCoordinator::shared().submit(
        &receiver, ScreenshotExportCoordinator::Priority::Foreground,
        [](const ScreenshotExportCancellation& cancellation) {
            QString error;
            const QImage original = fixture();
            const auto source = pipeline::prepare(snow_shot::image_codec::srgbRowSource(original),
                                                  cancellation, &error);
            require(source.preview == original,
                    "original comparison pixels changed during source preparation");
            for (auto format :
                 {Format::Png, Format::Jpeg, Format::Webp, Format::Jxl, Format::Avif}) {
                for (int quality : {100, 72}) {
                    const ScreenshotSaveExportOptions options{QSize(80, 50), format, quality};
                    const auto preview =
                        pipeline::render(source, options, cancellation, &error, true);
                    if (!preview || preview->preview.isNull())
                        throw std::runtime_error(
                            error.toStdString() +
                            (preview ? preview->previewError.toStdString() : ""));
                    require(preview->preview.size() == options.size,
                            "encoded preview size differs from form");
                    const auto output = pipeline::render(source, options, cancellation, &error);
                    require(output && snow_shot::image_codec::inspectFile(
                                          output->path,
                                          ScreenshotImageFileService::snowImageFormat(format),
                                          options.size),
                            "final encoding dimensions differ from form");
                }
                if (format != Format::Jpeg) {
                    const auto lossless = pipeline::render(source, {original.size(), format, 100},
                                                           cancellation, &error, true);
                    if (!lossless || lossless->preview != original) {
                        QString detail = ScreenshotImageFileService::extension(format);
                        if (lossless) {
                            for (int y = 0; y < original.height(); ++y)
                                for (int x = 0; x < original.width(); ++x)
                                    if (original.pixelColor(x, y) !=
                                        lossless->preview.pixelColor(x, y)) {
                                        detail += QStringLiteral(" at %1,%2: %3 -> %4")
                                                      .arg(x)
                                                      .arg(y)
                                                      .arg(original.pixelColor(x, y).name(
                                                               QColor::HexArgb),
                                                           lossless->preview.pixelColor(x, y).name(
                                                               QColor::HexArgb));
                                        throw std::runtime_error("lossless codec changed pixels: " +
                                                                 detail.toStdString());
                                    }
                        }
                        throw std::runtime_error("lossless codec changed image metadata: " +
                                                 detail.toStdString());
                    }
                }
            }
            const auto large =
                pipeline::prepare(snow_shot::image_codec::srgbRowSource(fixture(QSize(128, 6000))),
                                  cancellation, &error);
            require(large.rows.size == QSize(128, 6000) && large.preview.height() == 2048,
                    "large source must retain full dimensions and a bounded preview");
            const auto bounded = pipeline::render(large, {large.rows.size, Format::Png, 100},
                                                  cancellation, &error, true);
            require(bounded && bounded->preview.height() == 2048,
                    "large encoded preview must remain bounded");
            return ScreenshotExportTaskResult{};
        },
        [&](ScreenshotExportTaskResult result) {
            failure = result.error;
            done = true;
        });
    require(job.isValid(), "encoding test could not be scheduled");
    processUntil([&] { return done; });
    if (!failure.isEmpty())
        throw std::runtime_error(failure.toStdString());
}

void canvasInteraction() {
    ScreenshotSavePreviewCanvas canvas;
    canvas.resize(600, 400);
    QImage original(QSize(400, 200), QImage::Format_RGBA8888);
    original.fill(Qt::red);
    QImage output(original.size(), original.format());
    output.fill(Qt::green);
    canvas.setSource(original, original.size());
    canvas.setOutput(output, output.size());
    canvas.show();
    flush();
    const auto screenshot = canvas.grab().toImage();
    const qreal dpr = screenshot.devicePixelRatio();
    require(screenshot.pixelColor(qRound(200 * dpr), qRound(170 * dpr)) == QColor(Qt::red) &&
                screenshot.pixelColor(qRound(400 * dpr), qRound(170 * dpr)) == QColor(Qt::green),
            "split canvas must render original on left and output on right");
    canvas.setSplitRatio(-1);
    require(canvas.splitRatio() == 0, "split lower bound failed");
    canvas.setSplitRatio(2);
    require(canvas.splitRatio() == 1, "split upper bound failed");
    canvas.setSplitRatio(0.5);
    const double before = canvas.zoom();
    const QPointF cursor(450, 150);
    const QPointF point = (cursor - QRectF(canvas.rect()).center() - canvas.pan()) / before;
    QWheelEvent wheel(cursor, canvas.mapToGlobal(cursor.toPoint()), {}, QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&canvas, &wheel);
    require(
        canvas.zoom() > before &&
            QLineF(point, (cursor - QRectF(canvas.rect()).center() - canvas.pan()) / canvas.zoom())
                    .length() < .001,
        "zoom must preserve the image point under the cursor");
    require(child<QLabel>(&canvas, "savePreviewZoom")->text() == QStringLiteral("115%"),
            "zoom readout did not update");
    const QPointF pan = canvas.pan();
    QMouseEvent down(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100), Qt::LeftButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(125, 120), QPointF(125, 120), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent up(QEvent::MouseButtonRelease, QPointF(125, 120), QPointF(125, 120), Qt::LeftButton,
                   Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &down);
    QApplication::sendEvent(&canvas, &move);
    QApplication::sendEvent(&canvas, &up);
    require(canvas.pan() == pan + QPointF(25, 20), "canvas dragging did not pan");
}

void shortcutsAndCancellation(QWidget& owner, const QTemporaryDir& temp) {
    int saved = 0;
    int finished = 0;
    auto* modal = openDialog(
        owner, fixture(), [&](const QString&) { ++saved; },
        [&](bool accepted) { finished += accepted ? 100 : 1; });
    auto* content = modal->contentWidget();
    require(!content->findChild<AdButton*>(QStringLiteral("savePathExpand_-2")) &&
                !content->findChild<AdButton*>(QStringLiteral("savePathExpand_-1")),
            "built-in paths must have no edit/delete action");
    child<AdButton>(content, "savePathShortcut_-2")->click();
    require(child<AdLineEdit>(content, "saveDirectoryInput")->text() ==
                storage::ScreenshotSettings().imageSaveDirectory(),
            "App directory must select the configured directory");
    snapshot(modal, QStringLiteral("export-before-shortcuts"));
    require(child<AdButton>(content, "savePathAddButton")->isEnabled(),
            "Add save path must be enabled after loading");
    child<AdButton>(content, "savePathAddButton")->click();
    auto* editor = child<AdModal>(content, "savePathEditorModal");
    require(editor->mode() == AdModal::Mode::Overlay, "shortcut editor must be a widget modal");
    editor->acceptButton()->click();
    require(editor->isOpen() && storage::ScreenshotSettings().savePathShortcuts().isEmpty(),
            "empty shortcut must be rejected");
    snapshot(modal, QStringLiteral("shortcut-validation"));
    child<AdLineEdit>(editor->contentWidget(), "savePathNameInput")
        ->setText(QStringLiteral("Projects"));
    child<AdLineEdit>(editor->contentWidget(), "savePathValueInput")
        ->setText(temp.filePath("projects"));
    editor->acceptButton()->click();
    flush();
    require(storage::ScreenshotSettings().savePathShortcuts().size() == 1,
            "confirmed shortcut must persist immediately");
    child<AdButton>(content, "savePathExpand_0")->click();
    auto* menu = child<AdContextMenu>(content, "savePathMenu");
    require(menu->actions().size() == 2 && menu->actionDanger(menu->actions()[1]),
            "shortcut menu must include danger Delete");
    menu->hide();
    menu->actions()[0]->trigger();
    editor = child<AdModal>(content, "savePathEditorModal");
    child<AdLineEdit>(editor->contentWidget(), "savePathNameInput")
        ->setText(QStringLiteral("Exports"));
    editor->acceptButton()->click();
    flush();
    require(storage::ScreenshotSettings().savePathShortcuts()[0].name == QStringLiteral("Exports"),
            "shortcut edit did not persist");
    child<AdButton>(content, "savePathAddButton")->click();
    editor = child<AdModal>(content, "savePathEditorModal");
    child<AdLineEdit>(editor->contentWidget(), "savePathNameInput")
        ->setText(QStringLiteral("EXPORTS"));
    child<AdLineEdit>(editor->contentWidget(), "savePathValueInput")
        ->setText(temp.filePath("duplicate"));
    editor->acceptButton()->click();
    require(editor->isOpen() && storage::ScreenshotSettings().savePathShortcuts().size() == 1,
            "duplicate name must keep editor open");
    editor->rejectButton()->click();
    flush();
    modal->rejectButton()->click();
    flush();
    require(saved == 0 && finished == 1 &&
                storage::ScreenshotSettings().savePathShortcuts().size() == 1,
            "cancel must preserve confirmed shortcut edits and never save");
    modal = openDialog(owner, fixture());
    content = modal->contentWidget();
    child<AdButton>(content, "savePathExpand_0")->click();
    menu = child<AdContextMenu>(content, "savePathMenu");
    menu->hide();
    menu->actions()[1]->trigger();
    flush();
    require(storage::ScreenshotSettings().savePathShortcuts().isEmpty(),
            "shortcut delete did not persist");
    modal->rejectButton()->click();
    flush();
}

void previewAndSave(QWidget& owner, const QTemporaryDir& temp) {
    QString savedPath;
    int finished = 0;
    auto* modal = openDialog(
        owner, fixture(), [&](const QString& path) { savedPath = path; },
        [&](bool saved) { finished += saved ? 1 : 100; });
    auto* content = modal->contentWidget();
    auto* format = child<AdSelect>(content, "saveFormatSelect");
    auto* quality = child<AdSlider>(content, "saveQualitySlider");
    auto* badge = child<AdTag>(content, "saveLosslessBadge");
    snapshot(modal, QStringLiteral("export-light"));
    if (!qEnvironmentVariable("SNOW_EXPORT_TEST_SCREENSHOT_DIR").isEmpty()) {
        adqt::theme::ThemeManager::instance().setColorScheme(adqt::theme::ThemeScheme::Dark);
        snapshot(modal, QStringLiteral("export-dark"));
        adqt::theme::ThemeManager::instance().setColorScheme(adqt::theme::ThemeScheme::Light);
        modal->setPreferredWidth(680);
        content->setFixedHeight(360);
        static_cast<void>(modal->takeContentWidget());
        modal->setContentWidget(content);
        snapshot(modal, QStringLiteral("export-compact"));
        modal->setPreferredWidth(1080);
        content->setFixedHeight(540);
        static_cast<void>(modal->takeContentWidget());
        modal->setContentWidget(content);
    }
    require(!quality->isEnabled() && badge->isHidden(), "PNG must disable quality");
    for (const auto& key :
         {QStringLiteral("webp"), QStringLiteral("avif"), QStringLiteral("jxl")}) {
        format->setCurrentValue(key);
        quality->setValue(100);
        require(quality->isEnabled() && !badge->isHidden(),
                "lossless format must show badge at 100");
        if (key == QStringLiteral("webp"))
            snapshot(modal, QStringLiteral("export-lossless"));
        quality->setValue(99);
        require(badge->isHidden(), "quality below 100 must hide badge");
    }
    format->setCurrentValue(QStringLiteral("png"));
    const quint64 generation = content->property("previewGeneration").toULongLong();
    child<AdLineEdit>(content, "saveFilenameInput")->setText(QString());
    child<AdInputNumber>(content, "saveWidthInput")->setValue(96);
    child<AdInputNumber>(content, "saveWidthInput")->setValue(80);
    processUntil([&] { return content->property("previewGeneration").toULongLong() > generation; });
    require(child<AdInputNumber>(content, "saveHeightInput")->value() == 50 &&
                !modal->acceptButton()->isEnabled(),
            "latest geometry must render despite invalid filename and Save must remain blocked");
    child<AdLineEdit>(content, "saveFilenameInput")->setText(QStringLiteral("result.jpg"));
    const QString blocking = temp.filePath("blocking-file");
    QFile file(blocking);
    require(file.open(QIODevice::WriteOnly), "blocking file fixture failed");
    file.write("fixture");
    file.close();
    child<AdLineEdit>(content, "saveDirectoryInput")->setText(blocking + QStringLiteral("/child"));
    const auto remembered = storage::ScreenshotSettings().lastManualSaveDirectory();
    modal->acceptButton()->click();
    processUntil([&] { return !child<QLabel>(content, "saveErrorLabel")->isHidden(); });
    require(modal->isOpen() && savedPath.isEmpty() &&
                storage::ScreenshotSettings().lastManualSaveDirectory() == remembered,
            "failed write must keep dialog open and remembered directory unchanged");
    const QString directory = temp.filePath("new/nested");
    child<AdLineEdit>(content, "saveDirectoryInput")->setText(directory);
    int changes = 0;
    const auto connection =
        QObject::connect(&storage::ApplicationStorage::instance().configuration(),
                         &storage::ConfigurationStore::valueChanged, &owner,
                         [&](const QString& key, const QJsonValue&) {
                             if (key == QStringLiteral("screenshot/last_manual_save_directory"))
                                 ++changes;
                         });
    modal->acceptButton()->click();
    processUntil([&] { return !savedPath.isEmpty(); });
    QObject::disconnect(connection);
    require(finished == 1 && changes == 1 &&
                storage::ScreenshotSettings().lastManualSaveDirectory() == directory,
            "successful save must finish and remember directory exactly once");
    require(
        savedPath.endsWith(QStringLiteral("result.png")) &&
            snow_shot::image_codec::inspectFile(savedPath, snow::image::Format::png, QSize(80, 50)),
        "saved output must use normalized extension and requested dimensions");
    flush();
}

void pendingCancellation(QWidget& owner) {
    std::atomic_bool started = false;
    std::atomic_bool stopped = false;
    auto artifact = std::make_shared<ScreenshotExportArtifact>(
        ScreenshotExportSource::fromProducer([&](const ScreenshotExportCancellation& cancellation) {
            started = true;
            while (!cancellation.isCancellationRequested())
                QThread::msleep(1);
            stopped = true;
            return QImage{};
        }));
    require(ScreenshotSaveAsFileDialog::open(&owner, &owner, artifact),
            "pending-source dialog rejected");
    processUntil([&] { return started.load(); });
    child<AdModal>(&owner, "screenshotSaveAsFileModal")->rejectButton()->click();
    processUntil([&] { return stopped.load(); });
    require(artifact->isCancelled(), "closing dialog must cancel pending artifact work");
    flush();

    started = false;
    stopped = false;
    artifact = std::make_shared<ScreenshotExportArtifact>(
        ScreenshotExportSource::fromProducer({}, [&](std::function<bool()> cancelled) {
            started = true;
            if (cancelled) {
                while (!cancelled())
                    QThread::msleep(1);
                stopped = true;
            }
            return ScreenshotImageRowSource{};
        }));
    require(ScreenshotSaveAsFileDialog::open(&owner, &owner, artifact),
            "pending row-source dialog rejected");
    processUntil([&] { return started.load(); });
    child<AdModal>(&owner, "screenshotSaveAsFileModal")->rejectButton()->click();
    processUntil([&] { return stopped.load(); });
    require(artifact->isCancelled(), "closing must cancel row-source preparation");
    flush();
}

class ExportTestTranslator final : public QTranslator {
  public:
    bool isEmpty() const override {
        return false;
    }
    QString translate(const char* context, const char* source, const char*, int) const override {
        if (QByteArray(context).startsWith("ScreenshotSave"))
            return QStringLiteral("Translated ") + QString::fromUtf8(source);
        return {};
    }
};

void shortcutWrappingAndLanguageChange(QWidget& owner, const QTemporaryDir& temp) {
    const storage::ScreenshotSettings settings;
    require(
        settings.setSavePathShortcuts(
            {{QStringLiteral("A very long custom export destination name"), temp.filePath("one")},
             {QStringLiteral("Screenshots"), temp.filePath("two")},
             {QStringLiteral("Documents"), temp.filePath("three")},
             {QStringLiteral("Projects"), temp.filePath("four")}}),
        "wrapping setup failed");
    auto* modal = openDialog(owner, fixture(QSize(800, 500)));
    auto* content = modal->contentWidget();
    const auto first = child<AdButton>(content, "savePathShortcut_0");
    const auto last = child<AdButton>(content, "savePathShortcut_3");
    require(last->mapTo(content, QPoint()).y() > first->mapTo(content, QPoint()).y(),
            "custom path buttons must wrap onto multiple rows");
    require(first->width() <= 230 && first->parentWidget()->width() <= 254,
            "long shortcut names must not expand the form");
    const auto* host = first->parentWidget()->parentWidget();
    for (auto* button : host->findChildren<AdButton*>()) {
        const QRect bounds(button->mapTo(host, QPoint()), button->size());
        require(host->rect().contains(bounds), "wrapped shortcut buttons must not be clipped");
    }
    for (auto* input : content->findChildren<AdLineEdit*>())
        require(input->width() > 100, "form inputs must retain usable width");
    snapshot(modal, QStringLiteral("export-wrapped-shortcuts"));
    child<AdButton>(content, "savePathAddButton")->click();
    auto* editor = child<AdModal>(content, "savePathEditorModal");
    editor->acceptButton()->click();
    ExportTestTranslator translator;
    require(QApplication::installTranslator(&translator), "test translator unavailable");
    flush();
    require(modal->windowTitle() == QStringLiteral("Translated Save as file") &&
                editor->windowTitle() == QStringLiteral("Translated Add save path") &&
                child<AdLineEdit>(editor->contentWidget(), "savePathNameInput")->accessibleName() ==
                    QStringLiteral("Translated Name"),
            "open dialogs must retranslate captions and accessible names");
    const auto* form = qobject_cast<AdForm*>(editor->contentWidget());
    require(form->itemForName(QStringLiteral("name"))
                ->errorMessages()
                .contains(QStringLiteral("Translated Please enter a name")),
            "visible validation must retranslate with its dialog");
    QApplication::removeTranslator(&translator);
    flush();
    editor->rejectButton()->click();
    modal->rejectButton()->click();
    flush();
    require(settings.setSavePathShortcuts({}), "wrapping cleanup failed");
}

void overwriteRequiresConfirmation(QWidget& owner, const QTemporaryDir& temp) {
    const QString destination = temp.filePath(QStringLiteral("existing.png"));
    QFile existing(destination);
    require(existing.open(QIODevice::WriteOnly) && existing.write("existing") == 8,
            "overwrite fixture unavailable");
    existing.close();
    QString saved;
    auto* modal = openDialog(owner, fixture(), [&](const QString& path) { saved = path; });
    auto* content = modal->contentWidget();
    child<AdLineEdit>(content, "saveDirectoryInput")->setText(temp.path());
    child<AdLineEdit>(content, "saveFilenameInput")->setText(QStringLiteral("existing.jpg"));
    modal->acceptButton()->click();
    auto* confirmation = child<AdModal>(content, "saveOverwriteModal");
    require(confirmation->isOpen(), "normalized destination must trigger overwrite confirmation");
    confirmation->rejectButton()->click();
    flush();
    require(modal->isOpen() && saved.isEmpty() && existing.open(QIODevice::ReadOnly),
            "canceling overwrite must retain the export form");
    require(existing.readAll() == QByteArray("existing"), "canceling overwrite modified the file");
    existing.close();
    modal->acceptButton()->click();
    child<AdModal>(content, "saveOverwriteModal")->acceptButton()->click();
    processUntil([&] { return !saved.isEmpty(); });
    require(saved == destination && snow_shot::image_codec::inspectFile(
                                        saved, snow::image::Format::png, QSize(160, 100)),
            "confirmed overwrite must publish the selected encoding");
    flush();
}

void rowBackedDialogAndStalePreview(QWidget& owner, const QTemporaryDir& temp) {
    auto materializations = std::make_shared<std::atomic_int>(0);
    const auto rows = snow_shot::image_codec::srgbRowSource(fixture(QSize(64, 3000)));
    auto artifact = std::make_shared<ScreenshotExportArtifact>(ScreenshotExportSource::fromProducer(
        [materializations](const ScreenshotExportCancellation&) {
            ++*materializations;
            return QImage{};
        },
        [rows](std::function<bool()>) { return rows; }));
    QString savedPath;
    require(ScreenshotSaveAsFileDialog::open(&owner, &owner, artifact,
                                             [&](const QString& path) { savedPath = path; }),
            "row-backed dialog must open");
    auto* modal = child<AdModal>(&owner, "screenshotSaveAsFileModal");
    auto* content = modal->contentWidget();
    processUntil([&] { return content->property("previewGeneration").toULongLong() > 0; });
    flush();
    require(materializations->load() == 0, "scrolling preview must not materialize the image");

    // Occupy both workers so a preview remains pending while newer form state arrives.
    struct WorkerGate {
        std::shared_ptr<std::atomic_bool> released = std::make_shared<std::atomic_bool>(false);
        ~WorkerGate() {
            *released = true;
        }
    } gate;
    for (int worker = 0; worker < 2; ++worker) {
        const auto handle = ScreenshotExportCoordinator::shared().submit(
            &owner, ScreenshotExportCoordinator::Priority::Foreground,
            [released = gate.released](const ScreenshotExportCancellation& cancellation) {
                while (!released->load() && !cancellation.isCancellationRequested())
                    QThread::msleep(1);
                return ScreenshotExportTaskResult{};
            },
            [](ScreenshotExportTaskResult) {});
        require(handle.isValid(), "worker gate could not be queued");
    }
    const auto previous = content->property("previewGeneration").toULongLong();
    auto* width = child<AdInputNumber>(content, "saveWidthInput");
    width->setValue(32);
    processUntil([&] { return ScreenshotExportCoordinator::shared().pendingJobCount() == 3; });
    width->setValue(48);
    *gate.released = true;
    processUntil(
        [&] { return content->property("previewGeneration").toULongLong() >= previous + 2; });
    require(child<AdInputNumber>(content, "saveHeightInput")->value() == 2250,
            "stale preview must never replace latest dimensions");
    child<AdLineEdit>(content, "saveDirectoryInput")->setText(temp.filePath("scrolling"));
    child<AdLineEdit>(content, "saveFilenameInput")->setText(QStringLiteral("scroll.png"));
    modal->acceptButton()->click();
    processUntil([&] { return !savedPath.isEmpty(); });
    require(materializations->load() == 0 &&
                snow_shot::image_codec::inspectFile(savedPath, snow::image::Format::png,
                                                    QSize(48, 2250)),
            "row-backed Save must use the full source and latest settings");
    require(!artifact->isCancelled(),
            "successful export must leave its artifact available to history");
    flush();
}
} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    adqt::theme::ThemeManager::instance().applyTo(app);
    QTemporaryDir temp;
    try {
        require(temp.isValid() && QDir().mkpath(temp.filePath("app")), "test storage unavailable");
        require(storage::ApplicationStorage::instance()
                    .initialize({temp.filePath("app"), temp.filePath("data"), 60000})
                    .success,
                "test storage initialization failed");
        persistence(temp);
        stateRules(temp);
        encodingAndBoundedSources();
        canvasInteraction();
        QWidget owner;
        owner.resize(1200, 800);
        owner.show();
        shortcutsAndCancellation(owner, temp);
        previewAndSave(owner, temp);
        overwriteRequiresConfirmation(owner, temp);
        shortcutWrappingAndLanguageChange(owner, temp);
        rowBackedDialogAndStalePreview(owner, temp);
        pendingCancellation(owner);
        ScreenshotExportCoordinator::shared().shutdown();
        storage::ApplicationStorage::instance().shutdown();
        std::cout << "Export dialog workflow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        ScreenshotExportCoordinator::shared().shutdown();
        storage::ApplicationStorage::instance().shutdown();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
