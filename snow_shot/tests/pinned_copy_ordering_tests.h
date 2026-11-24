// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace pinned_copy_ordering_tests {
using Access = ScreenshotPinnedWindowTestAccess;

enum class CopyKind { Image, File, Original };
enum class Replacement { Batch, Image, Text };

void pendingAnnotationText() {
    const snow_shot::storage::ScreenshotSettings settings;
    for (const bool copyFile : {false, true}) {
        require(settings.setCopyImageFileToClipboard(copyFile), "configure annotation copy output");
        PinnedSelectionFixture fixture;
        auto& window = fixture.add({40, 60}, {360, 220});
        Access::editSelectionOffscreen(window, true);
        auto* canvas = window.findChild<SnowCanvasWidget*>();
        auto* controller = window.findChild<ScreenshotPinnedEditController*>();
        require(canvas && controller && controller->toolbarWindow() &&
                    controller->toolbarWindow()->palette()->activateDrawingShortcut(
                        QStringLiteral("text")),
                "activate the inline annotation editor");
        const QPointF position = canvas->rect().center();
        canvas_quick_selection_test::mouse(*canvas, QEvent::MouseButtonPress, position,
                                           Qt::LeftButton, Qt::LeftButton);
        canvas_quick_selection_test::mouse(*canvas, QEvent::MouseButtonRelease, position,
                                           Qt::LeftButton, Qt::NoButton);
        QInputMethodEvent input;
        input.setCommitString(QStringLiteral("Pending clipboard annotation"));
        QCoreApplication::sendEvent(canvas, &input);
        require(canvas->hasActiveTextEditing(), "annotation text is still uncommitted");
        int historyChanges = 0;
        const auto historyConnection = QObject::connect(
            canvas, &SnowCanvasWidget::historyStateChanged, &window, [&] { ++historyChanges; });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(historyConnection); });
        QApplication::clipboard()->setText(QStringLiteral("before annotation copy"));
        Access::copyCurrentViewport(window);
        require(!canvas->hasActiveTextEditing() && historyChanges > 0 &&
                    Access::exportArtifact(window),
                "snapshot preparation commits text without cancelling its own copy request");
        translation_tests::waitUntil([&] { return !Access::exportArtifact(window); },
                                     "annotation copy finishes");
        const auto urls = exportedClipboardUrls();
        require(!copyFile || urls.size() == 1, "file copy publishes the annotated image");
        const QImage copied =
            copyFile ? QImage(urls.first().toLocalFile()) : exportedClipboardImage();
        const QImage expected = pinnedExportSnapshotPixels(Access::fileSave(window), window);
        requirePinnedExportPixels(copied, expected, "clipboard contains the committed annotation");
        require(window.persistenceSnapshot().canvasSession.contains("Pending clipboard annotation"),
                "copy preserves the newly committed annotation in the document");
    }
}

void deferredCopiesYieldToBatch() {
    const snow_shot::storage::ScreenshotSettings settings;
    // Exercise requests waiting for materialization, including Original Content's separate path.
    for (const auto kind : {CopyKind::Image, CopyKind::File, CopyKind::Original}) {
        require(settings.setCopyImageFileToClipboard(kind == CopyKind::File),
                "configure delayed copy output");
        for (const bool sameOwner : {false, true}) {
            PinnedSelectionFixture fixture;
            auto& owner = fixture.add({40, 60});
            auto& target = fixture.add({230, 160}, {90, 70});
            auto config = cachedOcrPinConfig(nullptr);
            config.recognitionResults = {};
            config.recognitionVisible = false;
            config.enableEditing = false;
            ScreenshotImageLoadCallback deliver;
            config.imageLoader = [&deliver](QObject*, ScreenshotImageLoadCallback ready) {
                deliver = std::move(ready);
            };
            ScreenshotPinnedWindow lazy;
            lazy.setAttribute(Qt::WA_DeleteOnClose, false);
            const auto close = qScopeGuard([&] { lazy.close(); });
            require(lazy.present(config), "present a delayed copy source");
            translation_tests::waitUntil([&] { return bool(deliver); },
                                         "delayed image loader starts");
            if (kind == CopyKind::Original)
                Access::copyOriginalContent(lazy);
            else
                Access::copyCurrentViewport(lazy);
            Access::batchCopy(sameOwner ? lazy : owner, {&target});
            pinned_batch_test::waitForSave();
            const auto batch = batchClipboardUrls();
            require(batch.size() == 1 &&
                        QImage(batch.first().toLocalFile()).size() == QSize(90, 70),
                    "the newer batch publishes its target");
            bool snapshotReady = false;
            lazy.requestRenderedImageSnapshot([&](ScreenshotPinnedImageSnapshot snapshot) {
                require(snapshot.source.isValid(), "supersession preserves the source image");
                snapshotReady = true;
            });
            deliver(config.imageSource.materializedImage);
            translation_tests::waitUntil(
                [&] {
                    return snapshotReady && !Access::exportArtifact(lazy) &&
                           ScreenshotExportCoordinator::shared().pendingJobCount() == 0;
                },
                "older materialization and copy preparation settle");
            require(
                batchClipboardUrls() == batch,
                "older image, file and original copies cannot replace a newer batch on any pin");
        }
    }
}

void latestDeferredCopyCompletes() {
    for (const bool original : {false, true}) {
        auto config = cachedOcrPinConfig(nullptr);
        config.recognitionResults = {};
        config.recognitionVisible = false;
        config.enableEditing = false;
        ScreenshotImageLoadCallback deliver;
        config.imageLoader = [&deliver](QObject*, ScreenshotImageLoadCallback ready) {
            deliver = std::move(ready);
        };
        ScreenshotPinnedWindow window;
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        const auto close = qScopeGuard([&] { window.close(); });
        require(window.present(config), "present the latest deferred copy source");
        translation_tests::waitUntil([&] { return bool(deliver); }, "latest source starts loading");
        QApplication::clipboard()->setText(QStringLiteral("before deferred copy"));
        if (original)
            Access::copyOriginalContent(window);
        else
            Access::copyCurrentViewport(window);
        deliver(config.imageSource.materializedImage);
        translation_tests::waitUntil(
            [&] {
                return !Access::exportArtifact(window) &&
                       exportedClipboardImage().size() ==
                           config.imageSource.materializedImage.size();
            },
            "the latest original or viewport copy survives initial materialization");
        requirePinnedExportPixels(exportedClipboardImage(), config.imageSource.materializedImage,
                                  "the latest deferred copy publishes real source pixels");
    }
}

void renderingCopiesYieldToNewRequests() {
    const snow_shot::storage::ScreenshotSettings settings;
    for (const bool copyFile : {false, true}) {
        require(settings.setCopyImageFileToClipboard(copyFile), "configure deferred rendering");
        // Freeze the old request after its snapshot, before either clipboard encoder completes.
        for (const auto replacement : {Replacement::Batch, Replacement::Image, Replacement::Text}) {
            PinnedSelectionFixture fixture;
            auto& first = fixture.add({40, 60});
            auto& second = fixture.add({230, 160}, {90, 70});
            QTemporaryDir output;
            require(output.isValid() && settings.setImageSaveDirectory(output.path()) &&
                        settings.setExportTasks(snow_shot::storage::ExportTrigger::PinnedCopy,
                                                snow_shot::storage::ExportTask::AutoSave),
                    "the older copy also owns an independent automatic save");
            std::function<void(QImage)> deliver;
            auto source = ScreenshotExportSource::fromImageLoader(
                [&deliver](QObject*, std::function<void(QImage)> ready) {
                    deliver = std::move(ready);
                    return true;
                });
            Access::copyRenderedImage(
                first, std::make_shared<ScreenshotExportArtifact>(std::move(source)));
            require(settings.setExportTasks(snow_shot::storage::ExportTrigger::PinnedCopy, {}),
                    "later copies do not request automatic saving");
            translation_tests::waitUntil([&] { return bool(deliver); },
                                         "the older copy waits for rendered pixels");
            if (replacement == Replacement::Batch) {
                Access::batchCopy(second, {&second});
                pinned_batch_test::waitForSave();
            } else if (replacement == Replacement::Image) {
                Access::copyCurrentViewport(second);
            } else {
                auto config = cachedOcrPinConfig(nullptr);
                config.originalClipboardContent.text = QStringLiteral("newer original text");
                ScreenshotPinnedWindow text;
                Access::restoreOffscreen(text, config);
                Access::copyOriginalContent(text);
            }
            const auto currentUrls = [&] {
                return replacement == Replacement::Batch ? batchClipboardUrls()
                                                         : exportedClipboardUrls();
            };
            // Snapshot materialization can precede the creation of a clipboard artifact.
            // Wait for publication itself before allowing the older encoder to finish.
            translation_tests::waitUntil(
                [&] {
                    if (Access::exportArtifact(second))
                        return false;
                    if (replacement == Replacement::Text)
                        return QApplication::clipboard()->text() ==
                               QStringLiteral("newer original text");
                    if (replacement == Replacement::Batch || copyFile) {
                        const auto urls = currentUrls();
                        return urls.size() == 1 &&
                               QImage(urls.first().toLocalFile()).size() == QSize(90, 70);
                    }
                    return exportedClipboardImage().size() == QSize(90, 70);
                },
                "the newer copy publishes its content");
            const auto urls = currentUrls();
            const auto image = exportedClipboardImage();
            const auto text = QApplication::clipboard()->text();
            QImage older(120, 80, QImage::Format_ARGB32_Premultiplied);
            older.fill(Qt::red);
            deliver(older);
            translation_tests::waitUntil(
                [&] {
                    return !Access::exportArtifact(first) &&
                           ScreenshotExportCoordinator::shared().pendingJobCount() == 0;
                },
                "superseded rendering and clipboard publication settle");
            require(currentUrls() == urls && exportedClipboardImage() == image &&
                        QApplication::clipboard()->text() == text,
                    "late image and file encoders cannot replace a newer batch, image or text");
            require(QDir(output.path()).entryList(QDir::Files).isEmpty(),
                    "a superseded clipboard write must skip follow-up tasks");
        }
    }
}

void pendingBatchYieldsToRecognitionCopy() {
    PinnedSelectionFixture fixture;
    auto& owner = fixture.add({40, 60});
    auto& target = fixture.add({230, 160});
    auto config = cachedOcrPinConfig(nullptr);
    ScreenshotPinnedWindow text;
    const auto close = qScopeGuard([&] { text.close(); });
    Access::restoreOffscreen(text, config);
    auto* session = Access::recognitionOffscreen(text, config);
    require(session && session->active(), "activate cached recognized text");
    Access::batchCopy(owner, {&target});
    Access::copyEditToolbarContent(text);
    const QString copied = QApplication::clipboard()->text();
    require(copied == QStringLiteral("Saved OCR"), "recognized text reaches the clipboard");
    pinned_batch_test::waitForSave();
    require(QApplication::clipboard()->text() == copied && batchClipboardUrls().isEmpty(),
            "a pending batch cannot replace a later recognized-text copy");
}

void verifyCopyAndCloseSuperseded(const QString& expected, const std::function<void()>& copy) {
    auto config = cachedOcrPinConfig(nullptr);
    config.recognitionResults = {};
    config.recognitionVisible = false;
    config.enableEditing = false;
    ScreenshotImageLoadCallback deliver;
    config.imageLoader = [&deliver](QObject*, ScreenshotImageLoadCallback ready) {
        deliver = std::move(ready);
    };
    ScreenshotPinnedWindow source;
    source.setAttribute(Qt::WA_DeleteOnClose, false);
    const auto closeSource = qScopeGuard([&] { source.close(); });
    require(source.present(config), "present the delayed copy-and-close source");
    translation_tests::waitUntil([&] { return bool(deliver); }, "source starts loading");
    Access::copyCurrentAndClose(source);

    copy();
    require(QApplication::clipboard()->text() == expected, "the newer copy reached the clipboard");
    bool snapshotReady = false;
    source.requestRenderedImageSnapshot([&](ScreenshotPinnedImageSnapshot snapshot) {
        require(snapshot.source.isValid(), "the superseded source still has its image");
        snapshotReady = true;
    });
    deliver(config.imageSource.materializedImage);
    translation_tests::waitUntil(
        [&] {
            return snapshotReady && !Access::exportArtifact(source) &&
                   ScreenshotExportCoordinator::shared().pendingJobCount() == 0;
        },
        "the superseded copy-and-close settles");
    QCoreApplication::processEvents();
    require(source.isVisible() && QApplication::clipboard()->text() == expected,
            "the newer copy keeps the older pin open and retains the clipboard");
}

void copyAndCloseYieldsToRecognitionCopy() {
    const snow_shot::storage::TextRecognitionSettings textSettings;
    const auto oldFormatting = textSettings.defaultFormatting();
    const auto restore = qScopeGuard([&] { textSettings.setDefaultFormatting(oldFormatting); });
    require(textSettings.setDefaultFormatting(QStringLiteral("keep")),
            "configure OCR to copy its visible text directly");
    auto config = cachedOcrPinConfig(nullptr);
    ScreenshotPinnedWindow text;
    const auto closeText = qScopeGuard([&] { text.close(); });
    auto* session = Access::hiddenSelectionOffscreen(text, config);
    session->activate(ScreenshotRecognitionSessionController::Mode::Text);
    auto* content = text.findChild<ScreenshotRecognitionWindow*>();
    require(content && content->isVisible(), "show cached recognition text");

    verifyCopyAndCloseSuperseded(QStringLiteral("Saved OCR"), [&] {
        require(content->copyVisibleContentToClipboard(), "copy from the OCR content");
    });
}

void copyAndCloseYieldsToColorSwatch() {
    auto config = cachedOcrPinConfig(nullptr);
    config.recognitionResults = {};
    config.recognitionVisible = false;
    config.originalClipboardContent.text = QStringLiteral("#336699");
    snow_shot::storage::PinnedTextStyle style;
    style.colorSwatch = true;
    config.originalClipboardContent.textStyle = style;
    ScreenshotPinnedWindow swatch;
    swatch.setAttribute(Qt::WA_DeleteOnClose, false);
    const auto close = qScopeGuard([&] { swatch.close(); });
    require(swatch.present(config), "present a color swatch");
    auto* button = swatch.findChild<QAbstractButton*>(QStringLiteral("pinnedColorCopy0"));
    require(button && button->isVisible(), "show the swatch's hex copy button");
    verifyCopyAndCloseSuperseded(QStringLiteral("#336699"), [&] { button->click(); });
}

void deferredSaveYieldsToNewCopy() {
    using snow_shot::storage::ExportTask;
    using snow_shot::storage::ExportTrigger;
    const snow_shot::storage::ScreenshotSettings settings;
    QTemporaryDir directory;
    const auto oldTasks = settings.exportTasks(ExportTrigger::PinnedSave);
    const auto restore =
        qScopeGuard([&] { settings.setExportTasks(ExportTrigger::PinnedSave, oldTasks); });
    require(directory.isValid() && settings.setImageSaveDirectory(directory.path()) &&
                settings.setExportTasks(ExportTrigger::PinnedSave, ExportTask::CopyFile),
            "configure a pinned save with a clipboard follow-up");
    auto config = cachedOcrPinConfig(nullptr);
    config.recognitionResults = {};
    config.recognitionVisible = false;
    config.enableEditing = false;
    ScreenshotImageLoadCallback deliver;
    config.imageLoader = [&deliver](QObject*, ScreenshotImageLoadCallback ready) {
        deliver = std::move(ready);
    };
    ScreenshotPinnedWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    const auto close = qScopeGuard([&] { window.close(); });
    require(window.present(config), "present a pin with pending image materialization");
    translation_tests::waitUntil([&] { return bool(deliver); }, "the pin is waiting for pixels");
    Access::quickSave(window);
    QImage newerCopy(5, 3, QImage::Format_RGBA8888);
    newerCopy.fill(Qt::green);
    newerCopy.setColorSpace(QColorSpace::SRgb);
    require(ScreenshotClipboardService::publishImage(QApplication::clipboard(), newerCopy),
            "publish a newer copy before the pin is ready to save");
    deliver(config.imageSource.materializedImage);
    translation_tests::waitUntil(
        [&] {
            return QFileInfo::exists(directory.filePath(QStringLiteral("OrderedCopy.png"))) &&
                   snow_shot::runtime::RuntimeActivityTracker::shared().snapshot().activeCount == 0;
        },
        "the earlier pinned save and its follow-up settle");
    requirePinnedExportPixels(exportedClipboardImage(), newerCopy,
                              "the late pinned save preserves the newer clipboard image");
    require(exportedClipboardUrls().isEmpty(), "the older saved file must not reach the clipboard");
}

void run() {
    const snow_shot::storage::ScreenshotSettings settings;
    const auto oldActions = settings.exportTasks(snow_shot::storage::ExportTrigger::PinnedCopy);
    const auto oldName = settings.autoSaveFilenameFormat();
    const auto oldFormat = settings.imageFormat();
    const auto oldDirectory = settings.imageSaveDirectory();
    const auto oldCopyFiles = settings.copyImageFileToClipboard();
    const auto restore = qScopeGuard([&] {
        settings.setExportTasks(snow_shot::storage::ExportTrigger::PinnedCopy, oldActions);
        settings.setAutoSaveFilenameFormat(oldName);
        settings.setImageFormat(oldFormat);
        settings.setImageSaveDirectory(oldDirectory);
        settings.setCopyImageFileToClipboard(oldCopyFiles);
    });
    require(settings.setExportTasks(snow_shot::storage::ExportTrigger::PinnedCopy, {}) &&
                settings.setAutoSaveFilenameFormat(QStringLiteral("OrderedCopy")) &&
                settings.setImageFormat(QStringLiteral("png")),
            "configure deterministic clipboard exports without automatic saving");
    pendingAnnotationText();
    deferredCopiesYieldToBatch();
    require(settings.setCopyImageFileToClipboard(false), "configure deferred raster copying");
    latestDeferredCopyCompletes();
    renderingCopiesYieldToNewRequests();
    pendingBatchYieldsToRecognitionCopy();
    copyAndCloseYieldsToRecognitionCopy();
    copyAndCloseYieldsToColorSwatch();
    const auto other = QStringLiteral("copied outside the pin");
    verifyCopyAndCloseSuperseded(other, [&] { QApplication::clipboard()->setText(other); });
    deferredSaveYieldsToNewCopy();
}
} // namespace pinned_copy_ordering_tests
