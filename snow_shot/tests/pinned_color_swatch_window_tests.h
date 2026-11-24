// Included by screenshot_pinned_window_tests.cpp with its offscreen test helpers.
void pinnedColorSwatchWindowTests() {
    using Access = ScreenshotPinnedWindowTestAccess;
    auto* screen = QGuiApplication::primaryScreen();
    require(screen != nullptr, "color cards need a screen");
    const int font = QFontDatabase::addApplicationFont(
        QStringLiteral(":/recording-test-fonts/SnowRecordingTestSans-Regular.ttf"));
    require(font >= 0, "color card fixture font");
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(), 12));
    class Clipboard final : public ScreenshotPinnedClipboard {
      public:
        std::unique_ptr<QMimeData> output;
        int copies = 0;
        const QMimeData* mimeData() const override {
            return output.get();
        }
        std::optional<ScreenshotClipboardContentSnapshot> snapshot(qreal) override {
            return {};
        }
        ScreenshotClipboardCommitHandle
        publish(QObject*, std::unique_ptr<QMimeData> data,
                ScreenshotClipboardService::PublicationId,
                ScreenshotClipboardService::CommitCompletion done) override {
            output = std::move(data);
            ++copies;
            done({});
            return {};
        }
    };
    const auto decode = [](const QString& text, qreal dpr) {
        ScreenshotClipboardContentSnapshot snapshot;
        snapshot.text = text;
        snapshot.devicePixelRatio = dpr;
        snapshot.colorSwatchStyle = capturePinnedColorSwatchStyle();
        auto content = ScreenshotClipboardContentReader::decode(snapshot);
        require(content && content->originalContent.textStyle->colorSwatch, "decode color card");
        return *content;
    };
    const auto configFor = [screen](const ScreenshotClipboardContent& content) {
        ScreenshotPinnedWindow::Config config;
        config.screen = screen;
        config.nativeGeometry = physicalPinGeometry(*screen, QPoint(60, 60), QSize(342, 210));
        config.initialWindowSize = config.nativeGeometry.size();
        config.canvasSourceRect = QRectF(QPointF(), QSizeF(content.image.size()));
        config.imageSource =
            ScreenshotImageSource::fromImage(content.image, config.canvasSourceRect);
        config.formattedTextDocument = content.formattedDocument;
        config.formattedPlainText = content.plainText;
        config.formattedTextDevicePixelRatio = content.formattedTextDevicePixelRatio;
        config.originalClipboardContent = content.originalContent;
        config.automaticTextRecognition = false;
        config.checkerboardEnabled = false;
        config.initialBorderVisible = false;
        config.initialShadowVisible = false;
        return config;
    };
    auto& theme = adqt::theme::ThemeManager::instance();
    const auto oldTheme = theme.config();
    const auto restoreTheme = qScopeGuard([&] { theme.setConfig(oldTheme); });
    for (const qreal dpr : {1.0, 1.25}) {
        const auto content = decode(QStringLiteral("#7DD35380"), dpr);
        const auto expected = parsePinnedColorSwatch(content.originalContent.text);
        ScreenshotPinnedWindow window;
        auto clipboard = std::make_unique<Clipboard>();
        auto* recorded = clipboard.get();
        Access::setClipboard(window, std::move(clipboard));
        require(window.prewarm(screen) &&
                    !window.findChild<QAbstractButton*>(QStringLiteral("pinnedColorCopy0")),
                "prewarmed windows have no stale card controls");
        require(window.present(configFor(content)), "present color pin");
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        waitForUi(80);
        const auto buttons = [&window](std::size_t row) {
            return window.findChild<QAbstractButton*>(QStringLiteral("pinnedColorCopy%1").arg(row));
        };
        const auto click = [&](std::size_t row) {
            auto* button = buttons(row);
            require(button && button->isVisible() && button->isEnabled(), "copy button visible");
            const QPoint point = button->rect().center();
            const QPoint windowPoint = button->mapTo(&window, point);
            require(window.childAt(windowPoint) == button,
                    "copy button receives input above text selection");
            require(!Access::draggableAt(window, windowPoint, Qt::AltModifier) &&
                        !Access::exportEligible(window, windowPoint),
                    "copy targets exclude window and export dragging");
            const auto geometry = window.currentNativeGeometry();
            const QPointF global = button->mapToGlobal(point);
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(button, &press);
            QApplication::sendEvent(button, &release);
            require(recorded->output && recorded->output->text() == expected->clipboard[row],
                    "row copies CSS color to clipboard");
            require(window.isVisible() && window.currentNativeGeometry() == geometry &&
                        !Access::lockInteractionActive(window),
                    "copy keeps the pin open without moving it");
        };
        for (std::size_t row = 0; row < expected->clipboard.size(); ++row)
            click(row);
        const auto artifact = Access::fileSave(window);
        ScreenshotExportImageResult exported;
        bool exportComplete = false;
        require(artifact && artifact->requestImage(&window,
                                                   [&](ScreenshotExportImageResult result) {
                                                       exported = std::move(result);
                                                       exportComplete = true;
                                                   }),
                "start color card image export");
        QElapsedTimer exportTimer;
        exportTimer.start();
        while (!exportComplete && exportTimer.elapsed() < 5000)
            waitForUi(5);
        require(exportComplete && exported.succeeded() &&
                    exported.image.convertToFormat(QImage::Format_ARGB32) ==
                        Access::originalImage(window).convertToFormat(QImage::Format_ARGB32),
                "image export includes the complete swatch card raster");
        buttons(1)->setFocus();
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(buttons(1), &press);
        QApplication::sendEvent(buttons(1), &release);
        require(recorded->output->text() == expected->clipboard[1], "keyboard copies color");
        buttons(2)->setFocus();
        QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QKeyEvent enterRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(buttons(2), &enterPress);
        QApplication::sendEvent(buttons(2), &enterRelease);
        require(recorded->output->text() == expected->clipboard[2], "Enter copies color");
        const auto beforeDoubleClick = window.currentNativeGeometry();
        const QPointF doublePoint = buttons(0)->rect().center();
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, doublePoint,
                                buttons(0)->mapToGlobal(doublePoint.toPoint()), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
        QMouseEvent doubleRelease(QEvent::MouseButtonRelease, doublePoint,
                                  buttons(0)->mapToGlobal(doublePoint.toPoint()), Qt::LeftButton,
                                  Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(buttons(0), &doubleClick);
        QApplication::sendEvent(buttons(0), &doubleRelease);
        require(window.isVisible() && window.currentNativeGeometry() == beforeDoubleClick &&
                    recorded->output->text() == expected->clipboard[0],
                "double clicking a copy button does not trigger pin actions");
        require(!buttons(1)->accessibleName().isEmpty(), "copy button has accessible name");
        Access::setLock(window, true);
        click(0);
        Access::setLock(window, false);
        Access::thumbnailForHideTest(window, true);
        require(!buttons(0)->isVisible(), "thumbnail disables color controls");
        Access::thumbnailForHideTest(window, false);
        require(Access::setClickThrough(window, true), "enable click-through color pin");
        require(!buttons(0)->isVisible(), "click-through disables color controls");
        require(Access::setClickThrough(window, false), "disable click-through color pin");
        Access::editForHideTest(window);
        require(!buttons(0)->isVisible(), "drawing disables color controls");
        Access::endSwatchEditing(window);
        click(0);
        auto* recognition = Access::recognition(window);
        require(recognition, "color cards retain a selection document");
        recognition->activate(ScreenshotRecognitionSessionController::Mode::Text);
        require(!buttons(0)->isVisible(), "recognition disables color controls");
        recognition->deactivate();
        click(0);
        auto& hideToTop = Access::hideToTop(window);
        Access::toggleSwatchHideToTop(window);
        require(!buttons(0)->isVisible(), "hide-to-top disables color controls");
        hideToTop.exit(true);
        click(0);
        Access::scaleBorderFixture(window, 150);
        waitForUi(30);
        click(1);
        Access::rotateFormattedSelectionOffscreen(window);
        waitForUi(30);
        click(2);
        const auto geometry = window.currentNativeGeometry();
        const auto history = Access::drawingHistory(window);
        const auto before = Access::originalImage(window).pixelColor(0, 0);
        theme.setColorScheme(oldTheme.scheme == adqt::theme::ThemeScheme::Dark
                                 ? adqt::theme::ThemeScheme::Light
                                 : adqt::theme::ThemeScheme::Dark);
        waitForUi(30);
        require(Access::originalImage(window).pixelColor(0, 0) != before &&
                    window.currentNativeGeometry() == geometry &&
                    Access::drawingHistory(window) == history,
                "theme updates preserve geometry and annotations");
        click(0);
        class Translator final : public QTranslator {
          public:
            QString colorLabel = QStringLiteral("Translated color:");

            QString translate(const char* context, const char* source, const char*,
                              int) const override {
                if (QByteArray(context) != "PinnedColorSwatch")
                    return {};
                if (QByteArray(source) == "Color:")
                    return colorLabel;
                if (QByteArray(source) == "Copy HEX color")
                    return QStringLiteral("Translated copy HEX");
                return {};
            }
        } translator;
        QCoreApplication::installTranslator(&translator);
        auto removeTranslator =
            qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
        QEvent language(QEvent::LanguageChange);
        QApplication::sendEvent(&window, &language);
        require(buttons(0)->accessibleName() == QStringLiteral("Translated copy HEX"),
                "language updates button accessibility");
        const auto displayedRecognitionText = [&window]() {
            auto* layer =
                window.findChild<QGraphicsView*>(QStringLiteral("screenshotClipboardText"));
            if (layer && layer->isVisible() && layer->scene())
                for (auto* item : layer->scene()->items())
                    if (auto* text = dynamic_cast<QGraphicsTextItem*>(item))
                        return text->document()->toPlainText();
            return QString();
        };
        recognition->activate(ScreenshotRecognitionSessionController::Mode::Text);
        require(displayedRecognitionText().startsWith(translator.colorLabel) &&
                    recognition->originalText() == displayedRecognitionText(),
                "opening recognition after a language change uses the refreshed color card");
        const auto copiedRecognition = recognition->recognitionClipboardMimeData();
        require(copiedRecognition && copiedRecognition->text() == displayedRecognitionText(),
                "recognition copies the refreshed color labels");
        translator.colorLabel = QStringLiteral("Updated color:");
        QApplication::sendEvent(&window, &language);
        require(recognition->active() &&
                    displayedRecognitionText().startsWith(translator.colorLabel),
                "language changes refresh visible recognition without deactivating it");
        recognition->beginTextEditing();
        translator.colorLabel = QStringLiteral("Editing color:");
        QApplication::sendEvent(&window, &language);
        const QString originalDraft = recognition->textDraft();
        require(recognition->editing() && originalDraft.startsWith(translator.colorLabel),
                "language changes refresh an unedited draft without leaving editing");
        const QString editedDraft = QStringLiteral("Edited color card");
        recognition->setTextDraft(editedDraft);
        translator.colorLabel = QStringLiteral("Latest color:");
        QApplication::sendEvent(&window, &language);
        require(recognition->active() && recognition->editing() &&
                    recognition->textDraft() == editedDraft &&
                    recognition->originalText().startsWith(translator.colorLabel),
                "language changes preserve edited drafts and refresh their original text");
        recognition->undoTextEdit();
        require(recognition->textDraft() == originalDraft,
                "language changes preserve the text undo history");
        recognition->redoTextEdit();
        require(recognition->textDraft() == editedDraft,
                "language changes preserve the text redo history");
        recognition->resetTextEditing();
        require(recognition->textDraft().startsWith(translator.colorLabel),
                "reset uses the latest color labels");
        recognition->endTextEditing();
        require(displayedRecognitionText().startsWith(translator.colorLabel),
                "leaving editing restores the refreshed formatted document");
        recognition->deactivate();
        QCoreApplication::removeTranslator(&translator);
        removeTranslator.dismiss();
        QApplication::sendEvent(&window, &language);
        Access::copyOriginalContent(window);
        require(recorded->output->text() == content.originalContent.text,
                "Copy Original preserves input");
        const auto persisted = window.persistenceSnapshot();
        require(persisted.textStyle && persisted.textStyle->colorSwatch &&
                    persisted.textStyle->colorSwatchSize == QSize(342, 210),
                "snapshot retains swatch presentation and layout");
        const auto rerendered = ScreenshotClipboardContentReader::renderOriginalText(
            content.originalContent, dpr, {}, {}, capturePinnedColorSwatchStyle());
        require(rerendered.has_value(), "restore color source");
        auto restoredConfig = configFor(*rerendered);
        restoredConfig.restorePersistentState = true;
        restoredConfig.persistedFirstCreationTextDpi = dpr;
        restoredConfig.persistedImageTransform = persisted.imageTransform;
        restoredConfig.persistedQuarterTurns = persisted.quarterTurns;
        restoredConfig.nativeGeometry = persisted.nativeGeometry;
        restoredConfig.initialWindowSize = persisted.initialWindowSize;
        restoredConfig.canvasSourceRect = persisted.canvasSourceRect;
        restoredConfig.contentCanvasRect = persisted.contentCanvasRect;
        restoredConfig.surfaceCanvasRect = persisted.surfaceCanvasRect;
        ScreenshotPinnedWindow restored;
        require(restored.present(restoredConfig), "restore transformed color card");
        restored.setAttribute(Qt::WA_DeleteOnClose, false);
        waitForUi(40);
        auto* restoredButton =
            restored.findChild<QAbstractButton*>(QStringLiteral("pinnedColorCopy2"));
        require(restoredButton && restoredButton->isVisible() &&
                    restored.childAt(restoredButton->mapTo(
                        &restored, restoredButton->rect().center())) == restoredButton,
                "restored copy target follows persisted rotation and scale");
        ScreenshotClipboardContentSnapshot ordinary;
        ordinary.text = QStringLiteral("ordinary text");
        auto replacement = ScreenshotClipboardContentReader::decode(ordinary);
        require(replacement && window.automationReplaceContent(*replacement),
                "replace color with ordinary text");
        require(!buttons(0) && !window.persistenceSnapshot()
                                    .textStyle.value_or(snow_shot::storage::PinnedTextStyle{})
                                    .colorSwatch,
                "replacement clears swatch controls");
        require(window.automationReplaceContent(content), "replace ordinary text with color");
        click(2);
        theme.setConfig(oldTheme);
    }
    ScreenshotSelectionExportUiServices services;
    services.prewarmPinnedWindow(screen);
    QPointer<ScreenshotPinnedWindow> colorPin(hiddenPinnedWindowExcept({}));
    require(colorPin && !colorPin->findChild<QAbstractButton*>(QStringLiteral("pinnedColorCopy0")),
            "pooled shell starts without swatch controls");
    require(services.presentDecodedContentOnScreen(
                decode(QStringLiteral("#7DD353"), 1), screen, true,
                snow_shot::storage::PinnedWindowCreationSource::Clipboard),
            "present swatch in pooled shell");
    waitForUi(100);
    require(colorPin->findChild<QAbstractButton*>(QStringLiteral("pinnedColorCopy0")),
            "consumed pooled shell creates swatch controls");
    QPointer<ScreenshotPinnedWindow> textPin(hiddenPinnedWindowExcept({colorPin}));
    ScreenshotClipboardContentSnapshot ordinary;
    ordinary.text = QStringLiteral("ordinary pooled text");
    const auto textContent = ScreenshotClipboardContentReader::decode(ordinary);
    require(textPin && textContent &&
                services.presentDecodedContentOnScreen(
                    *textContent, screen, true,
                    snow_shot::storage::PinnedWindowCreationSource::Clipboard),
            "present ordinary text in replenished shell");
    waitForUi(100);
    require(!textPin->findChild<QAbstractButton*>(QStringLiteral("pinnedColorCopy0")) &&
                !textPin->persistenceSnapshot()
                     .textStyle.value_or(snow_shot::storage::PinnedTextStyle{})
                     .colorSwatch,
            "pooled presentations do not leak swatch state to ordinary text");
    colorPin->close();
    textPin->close();
    require(processUntilDeleted(colorPin, 2000) && processUntilDeleted(textPin, 2000),
            "close pooled swatch fixtures");
}
