// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageexporteditor_p.h"
#include "snow_shot/presentation/components/aspectratiolockbutton.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "snow_shot/presentation/styles/outputdescriptionstyle.h"
#include "snow_shot/storage/settingsadapters.h"
#include "screenshotsaveexportpipeline.h"
#include "screenshotexportmemory_p.h"
#include "widgets/button.h"
#include "widgets/form.h"
#include "widgets/input_number.h"
#include "widgets/select.h"
#include "widgets/segmented.h"
#include "widgets/scroll_area.h"
#include "widgets/slider.h"
#include "theme/theme_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace {
using namespace adqt::widgets;
using snow_shot::presentation::styles::applyOutputDescriptionStyle;
namespace pipeline = screenshot_save_export;
using Format = ScreenshotImageFileFormat;

constexpr int kSaveFormContentWidth = 328;
constexpr int kAspectRatioLockButtonSize = 32;
constexpr int kMaximumDimension = 1000000;

constexpr std::array<Format, 8> kFormats{
    Format::Png, Format::Jpeg, Format::Bmp,  Format::Webp,
    Format::Jxl, Format::Avif, Format::Tiff, Format::Pdf,
};

bool formatSupportsLossless(Format format) {
    return format == Format::Pdf || format == Format::Webp || format == Format::Jxl ||
           format == Format::Avif;
}

bool samePreviewPixels(ScreenshotSaveExportOptions left, ScreenshotSaveExportOptions right) {
    if ((left.format == Format::Png || left.format == Format::Tiff) && left.format == right.format)
        left.compressionLevel = right.compressionLevel;
    if (left.format == Format::Pdf && right.format == Format::Pdf) {
        // The paper is drawn by the preview canvas; title and page layout never alter the tiles.
        left.pdfPageSize = right.pdfPageSize;
        left.pdfTitle = right.pdfTitle;
    }
    return left == right;
}

QString translated(const char* text) {
    return QCoreApplication::translate("ScreenshotSaveAsFileDialog", text);
}

void configureExportOptionsForm(AdForm* form) {
    form->setFormLayout(AdForm::FormLayout::Vertical);
    form->setLabelAlign(AdForm::LabelAlign::Left);
    form->setColon(false);
    form->setRequiredMark(AdForm::RequiredMark::Hidden);
}

} // namespace

ImageExportEditor::ImageExportEditor(ScreenshotSaveDialogState state, bool includePdf)
    : m_state(std::move(state)), m_includePdf(includePdf) {
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(100);
    connect(&m_renderTimer, &QTimer::timeout, this, [this] { startRender(); });
    if (!m_includePdf && m_state.output.format == Format::Pdf)
        m_state.output.format = Format::Png;
    loadRememberedFormatOptions();
    applyRememberedFormatOptions();
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    m_preview = new ScreenshotSavePreviewCanvas(this);
    layout->addWidget(m_preview, 1);
    m_outputDescription = new QLabel(this);
    m_outputDescription->setObjectName(QStringLiteral("saveOutputDescription"));
    m_outputDescription->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_outputDescription->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* scroll = new AdScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(340);
    scroll->setMinimumHeight(200);
    m_form = new AdForm;
    configureExportOptionsForm(m_form);
    m_form->setContentsMargins(0, 0, 12, 0);
    scroll->setContentWidget(m_form);
    m_form->setAutoFillBackground(false);
    m_rightPanel = new QStackedWidget(this);
    m_rightPanel->setFixedWidth(340);
    m_rightPanel->addWidget(scroll);
    layout->addWidget(m_rightPanel);
}

void ImageExportEditor::initializeControls() {
    m_format = new AdSelect(m_form);
    m_format->setObjectName(QStringLiteral("saveFormatSelect"));
    addField("Image format", m_format, "format");

    auto* dimensions = new QWidget(m_form);
    dimensions->setObjectName(QStringLiteral("saveDimensionsForm"));
    dimensions->setFixedWidth(kSaveFormContentWidth);
    auto* dimensionsLayout = new QVBoxLayout(dimensions);
    dimensionsLayout->setContentsMargins(0, 0, 0, 0);
    dimensionsLayout->setSpacing(8);
    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    m_sizeLabel = new QLabel(dimensions);
    m_sizeLabel->setObjectName(QStringLiteral("saveSizeLabel"));
    m_sizeLabel->setWordWrap(true);
    header->addWidget(m_sizeLabel, 1);
    m_sizeUnit = new AdSegmented(dimensions);
    m_sizeUnit->setObjectName(QStringLiteral("saveSizeUnitSegmented"));
    m_sizeUnit->setControlSize(AdSegmented::ControlSize::Small);
    m_sizeUnit->setDistribution(AdSegmented::Distribution::Content);
    m_sizeUnit->addOption({}, QStringLiteral("pixels"));
    m_sizeUnit->addOption({}, QStringLiteral("percentage"));
    m_sizeUnit->setCurrentValue(QStringLiteral("pixels"));
    header->addWidget(m_sizeUnit, 0, Qt::AlignRight | Qt::AlignVCenter);
    dimensionsLayout->addLayout(header);
    auto* inputs = new QHBoxLayout;
    inputs->setSpacing(0);
    m_width = number("saveWidthInput");
    m_height = number("saveHeightInput");
    inputs->addWidget(m_width, 1);
    m_lock = new AspectRatioLockButton(dimensions);
    m_lock->setObjectName(QStringLiteral("saveAspectLockButton"));
    m_lock->setFixedSize(kAspectRatioLockButtonSize, kAspectRatioLockButtonSize);
    m_lock->setChecked(true);
    inputs->addWidget(m_lock, 0, Qt::AlignVCenter);
    inputs->addWidget(m_height, 1);
    dimensionsLayout->addLayout(inputs);
    m_form->addField({}, dimensions, QStringLiteral("dimensions"));

    m_quality = new AdSlider(m_form);
    m_quality->setTracking(false);
    m_quality->setObjectName(QStringLiteral("saveQualitySlider"));
    m_quality->setRange(0, 100);
    m_quality->setValue(m_state.output.quality);
    m_quality->setSingleStep(1);
    m_qualityItem = addField("Quality", m_quality, "quality");
    m_compression = new AdSelect(m_form);
    m_compression->setObjectName(QStringLiteral("saveCompressionLevelSelect"));
    m_compressionItem = addField("Compression level", m_compression, "compression-level");
    m_error = new QLabel(m_form);
    m_error->setObjectName(QStringLiteral("saveErrorLabel"));
    m_error->setWordWrap(true);
    auto* errorItem = m_form->addField({}, m_error);
    errorItem->setNoStyle(true);
    m_error->hide();
    m_form->layout()->setAlignment(Qt::AlignTop);
    connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged, this,
            [this] {
                updateSizeLabelTheme();
                updateOutputDescriptionTheme();
                showError(m_error->text());
                m_preview->update();
            });

    connect(m_format, &AdSelect::currentValueChanged, this, [this](const QVariant& value) {
        rememberCurrentFormatOptions();
        m_state.output.format = ScreenshotImageFileService::formatForKey(value.toString());
        applyRememberedFormatOptions();
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
            dimensionChanged(true, m_percentage ? m_width->value() : m_state.output.size.width());
        updateControls();
    });
    connect(m_sizeUnit, &AdSegmented::currentValueChanged, this, [this](const QVariant& value) {
        commitDimensionEdit();
        m_percentage = value.toString() == QStringLiteral("percentage");
        syncDimensionInputs(true);
    });
    connect(m_quality, &AdSlider::valueChanged, this, [this](double value) {
        m_state.output.quality = qRound(value);
        rememberCurrentFormatOptions();
        changed();
    });
    connect(m_compression, &AdSelect::currentValueChanged, this, [this](const QVariant& value) {
        m_state.output.compressionLevel =
            ScreenshotImageFileService::compressionLevelForKey(value.toString());
        rememberCurrentFormatOptions();
        changed();
    });
}

void ImageExportEditor::retranslateControls() {
    m_lock->setToolTip(tr("Lock aspect ratio"));
    m_lock->setAccessibleName(m_lock->toolTip());
    m_format->setAccessibleName(tr("Image format"));
    m_width->setAccessibleName(tr("Width"));
    m_height->setAccessibleName(tr("Height"));
    m_width->setToolTip(tr("Width"));
    m_height->setToolTip(tr("Height"));
    m_sizeLabel->setText(tr("Size"));
    m_sizeUnit->setAccessibleName(tr("Size unit"));
    m_sizeUnit->setOptionLabel(0, tr("Pixels"));
    m_sizeUnit->setOptionLabel(1, tr("Percentage"));
    updateSizeLabelTheme();
    updateOutputDescriptionTheme();
    updateOutputDescription();
    m_quality->setAccessibleName(tr("Quality"));
    m_compression->setAccessibleName(tr("Compression level"));
    for (auto* item : findChildren<AdFormItem*>()) {
        const QByteArray source = item->property("exportLabel").toByteArray();
        if (!source.isEmpty())
            item->setLabel(translated(source.constData()));
    }
    const QSignalBlocker blocker(m_format);
    QVector<AdSelect::Option> options;
    for (const auto& entry : {std::pair<const char*, const char*>("png", "PNG"),
                              {"jpeg", "JPEG"},
                              {"bmp", "BMP"},
                              {"webp", "WebP"},
                              {"jxl", "JPEG XL"},
                              {"avif", "AVIF"},
                              {"tiff", "TIFF"},
                              {"pdf", "PDF"}}) {
        if (!m_includePdf && QLatin1StringView(entry.first) == QLatin1StringView("pdf"))
            continue;
        AdSelect::Option option;
        option.value = QString::fromLatin1(entry.first);
        option.label = translated(entry.second);
        options.push_back(option);
    }
    m_format->setOptions(options);
    m_format->setCurrentValue(ScreenshotImageFileService::formatKey(m_state.output.format));
    {
        const QSignalBlocker compressionBlocker(m_compression);
        m_compression->setOptions({
            {QStringLiteral("low"), tr("Low")},
            {QStringLiteral("medium"), tr("Medium")},
            {QStringLiteral("high"), tr("High")},
        });
        m_compression->setCurrentValue(
            ScreenshotImageFileService::compressionLevelKey(m_state.output.compressionLevel));
    }
    updateControls();
}

AdInputNumber* ImageExportEditor::number(const char* name) {
    auto* input = new AdInputNumber;
    input->setObjectName(QString::fromLatin1(name));
    input->setDecimals(0);
    input->setKeyboardTracking(false);
    input->setRange(0, kMaximumDimension);
    input->setSingleStep(1);
    input->setStepButtonLayout(AdInputNumber::StepButtonLayout::Compact);
    input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return input;
}

AdFormItem* ImageExportEditor::addField(const char* label, QWidget* editor, const char* key) {
    auto* item = m_form->addField({}, editor, QString::fromLatin1(key));
    item->setProperty("exportLabel", label);
    return item;
}

void ImageExportEditor::loadRememberedFormatOptions() {
    const QJsonObject remembered =
        snow_shot::storage::ScreenshotSettings().manualSaveFormatOptions();
    for (Format format : kFormats) {
        ScreenshotImageEncodingOptions options;
        const QJsonObject stored =
            remembered.value(ScreenshotImageFileService::formatKey(format)).toObject();
        options.quality = stored.value(QStringLiteral("quality")).toInt(100);
        options.compressionLevel = ScreenshotImageFileService::compressionLevelForKey(
            stored.value(QStringLiteral("compression_level")).toString());
        m_formatOptions.insert(static_cast<int>(format), options);
    }
}

void ImageExportEditor::rememberCurrentFormatOptions() {
    const int index = static_cast<int>(m_state.output.format);
    const ScreenshotImageEncodingOptions current{m_state.output.quality,
                                                 m_state.output.compressionLevel};
    if (current != m_formatOptions[index]) {
        m_formatOptions[index] = current;
        m_modifiedFormats.insert(index);
    }
}

void ImageExportEditor::applyRememberedFormatOptions() {
    const auto options = m_formatOptions.value(static_cast<int>(m_state.output.format));
    m_state.output.quality = options.quality;
    m_state.output.compressionLevel = options.compressionLevel;
    if (m_quality != nullptr) {
        const QSignalBlocker blocker(m_quality);
        m_quality->setValue(options.quality);
    }
    if (m_compression != nullptr) {
        const QSignalBlocker blocker(m_compression);
        m_compression->setCurrentValue(
            ScreenshotImageFileService::compressionLevelKey(options.compressionLevel));
    }
}

QJsonObject ImageExportEditor::serializedFormatOptions() const {
    // Other open export windows may have saved preferences since this one was opened.
    QJsonObject result = snow_shot::storage::ScreenshotSettings().manualSaveFormatOptions();
    for (Format format : kFormats) {
        const auto key = ScreenshotImageFileService::formatKey(format);
        if (result.contains(key) && !m_modifiedFormats.contains(static_cast<int>(format)))
            continue;
        const auto options = m_formatOptions.value(static_cast<int>(format));
        QJsonObject stored;
        if (ScreenshotImageFileService::supportsQuality(format))
            stored.insert(QStringLiteral("quality"), qBound(0, options.quality, 100));
        if (ScreenshotImageFileService::supportsCompressionLevel(format)) {
            stored.insert(
                QStringLiteral("compression_level"),
                ScreenshotImageFileService::compressionLevelKey(options.compressionLevel));
        }
        if (!stored.isEmpty())
            result.insert(key, stored);
    }
    return result;
}

void ImageExportEditor::persistFormatOptions() {
    rememberCurrentFormatOptions();
    if (snow_shot::storage::ScreenshotSettings().setLastManualSaveState(
            ScreenshotImageFileService::formatKey(m_state.output.format),
            serializedFormatOptions()))
        m_modifiedFormats.clear();
}

void ImageExportEditor::showError(const QString& message) {
    m_error->setText(message);
    m_error->setVisible(!message.isEmpty());
    const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
    QPalette palette = m_error->palette();
    palette.setColor(QPalette::WindowText, theme.colorError);
    m_error->setPalette(palette);
}

void ImageExportEditor::updateSizeLabelTheme() {
    const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
    auto font = m_form->font();
    font.setPixelSize(qMax(12, qRound(theme.fontSize)));
    font.setWeight(QFont::Normal);
    m_sizeLabel->setFont(font);
    auto palette = m_sizeLabel->palette();
    palette.setColor(QPalette::WindowText, theme.colorText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.colorTextDisabled);
    m_sizeLabel->setPalette(palette);
}

void ImageExportEditor::updateOutputDescriptionTheme() {
    const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
    applyOutputDescriptionStyle(*m_outputDescription, m_form->font(), theme);
}

QString ImageExportEditor::formattedOutputBytes(qint64 bytes) const {
    constexpr qint64 kibibyte = 1024;
    constexpr qint64 mebibyte = kibibyte * 1024;
    constexpr qint64 gibibyte = mebibyte * 1024;
    if (bytes < kibibyte)
        return tr("%1B").arg(bytes);
    if (bytes < mebibyte)
        return tr("%1KB").arg(qRound64(double(bytes) / kibibyte));
    if (bytes < gibibyte)
        return tr("%1MB").arg(qRound64(double(bytes) / mebibyte));
    return tr("%1GB").arg(qRound64(double(bytes) / gibibyte));
}

void ImageExportEditor::updateOutputDescription() {
    if (m_pendingArtifact || !m_encoded || !m_requestedOptions ||
        m_encoded->options != *m_requestedOptions || m_state.sourceSize.width() <= 0) {
        m_outputDescription->clear();
        return;
    }
    const QSize size = m_encoded->options.size;
    QString scale = QString::number(100.0 * size.width() / m_state.sourceSize.width(), 'f', 2);
    while (scale.endsWith(QLatin1Char('0')))
        scale.chop(1);
    if (scale.endsWith(QLatin1Char('.')))
        scale.chop(1);
    m_outputDescription->setText(tr("%1x%2(%3%) · %4")
                                     .arg(size.width())
                                     .arg(size.height())
                                     .arg(scale)
                                     .arg(formattedOutputBytes(m_encoded->fileBytes)));
}

void ImageExportEditor::commitDimensionEdit() {
    if (auto* focus = QApplication::focusWidget();
        focus && (focus == m_width || m_width->isAncestorOf(focus) || focus == m_height ||
                  m_height->isAncestorOf(focus)))
        focus->clearFocus();
}

void ImageExportEditor::syncDimensionInputs(bool preserveEmpty) {
    const auto sync = [this, preserveEmpty](AdInputNumber* input, int pixels, int source) {
        const QSignalBlocker blocker(input);
        const bool empty = preserveEmpty && !input->hasValue();
        input->setRange(0, m_percentage && source > 0 ? 100.0 * kMaximumDimension / source
                                                      : kMaximumDimension);
        input->setSuffixText(m_percentage ? QStringLiteral("%") : QString());
        if (empty)
            input->clear();
        else
            input->setValue(m_percentage && source > 0 ? 100.0 * pixels / source : pixels);
    };
    // Display rounding must never feed back into the exact pixel export dimensions.
    sync(m_width, m_state.output.size.width(), m_state.sourceSize.width());
    sync(m_height, m_state.output.size.height(), m_state.sourceSize.height());
}

void ImageExportEditor::dimensionChanged(bool width, double value) {
    const QSignalBlocker widthBlock(m_width);
    const QSignalBlocker heightBlock(m_height);
    if (m_percentage) {
        const auto pixels = [value](int source) {
            return value > 0
                       ? qRound(std::clamp(value * source / 100.0, 1.0, double(kMaximumDimension)))
                       : 0;
        };
        if (width)
            m_state.output.size.setWidth(pixels(m_state.sourceSize.width()));
        else
            m_state.output.size.setHeight(pixels(m_state.sourceSize.height()));
        if (m_state.lockAspectRatio && value > 0) {
            m_state.output.size =
                QSize(pixels(m_state.sourceSize.width()), pixels(m_state.sourceSize.height()));
            (width ? m_height : m_width)->setValue(value);
        }
    } else {
        m_state.setDimension(width, qRound(value));
        if (width && m_state.lockAspectRatio)
            m_height->setValue(m_state.output.size.height());
        if (!width && m_state.lockAspectRatio)
            m_width->setValue(m_state.output.size.width());
    }
    changed();
}

void ImageExportEditor::updateControls() {
    m_qualityItem->setItemHidden(
        !ScreenshotImageFileService::supportsQuality(m_state.output.format));
    m_compressionItem->setItemHidden(
        !ScreenshotImageFileService::supportsCompressionLevel(m_state.output.format));
    AdSlider::Mark minimumMark;
    minimumMark.label = tr("0%");
    AdSlider::Mark maximumMark;
    maximumMark.label = formatSupportsLossless(m_state.output.format) ? tr("Lossless") : tr("100%");
    m_quality->setMarks({{m_quality->minimum(), minimumMark}, {m_quality->maximum(), maximumMark}});
}

void ImageExportEditor::changed() {
    m_state.output.pdfTitle = QFileInfo(m_state.outputPath()).completeBaseName();
    m_preview->setPdfLayout(
        m_state.output.format == Format::Pdf
            ? screenshot_pdf::layout(m_state.output.size, m_state.output.pdfPageSize)
            : screenshot_pdf::Layout{});
    updateControls();
    if (m_phase != Phase::Editing || !m_source.rows.isValid())
        return;
    validateFields();
    const QString error = m_state.imageValidationError();
    const auto options = error.isEmpty()
                             ? std::make_optional(pipeline::normalizedOptions(m_state.output))
                             : std::nullopt;
    if (options && options == m_requestedOptions)
        return;
    m_requestedOptions = options;
    m_renderTimer.stop();
    m_outputDescription->clear();
    ++m_generation;
    m_job.cancel();
    m_renderGeneration.reset();
    cancelDecode();
    showError(error);
    if (!options) {
        m_preview->setBusy(false);
        return;
    }
    m_preview->setBusy(true);
    const bool cachedPng = options->format == Format::Png && options->size == m_state.sourceSize &&
                           m_artifact->cachedPng(options->compressionLevel).isValid();
    if (m_generation == 1 || cachedPng || (m_encoded && m_encoded->options == *options))
        startRender();
    else
        m_renderTimer.start();
}

void ImageExportEditor::startRender() {
    if (closed() || !m_source.rows.isValid() || !m_requestedOptions)
        return;
    if (m_encoded && m_encoded->options == *m_requestedOptions) {
        updateOutputDescription();
        if (saving())
            exportReady();
        else
            startDecode();
        return;
    }
    if (m_renderGeneration == m_generation)
        return;
    const quint64 generation = m_generation;
    const auto options = *m_requestedOptions;
    m_renderGeneration = generation;
    if (options.format == Format::Png && options.size == m_state.sourceSize) {
        auto cached = m_artifact->cachedPng(options.compressionLevel);
        if (cached.isValid() || !m_artifact->shouldCachePng(options.size)) {
            renderPrepared(generation, options, std::move(cached));
            return;
        }
        const bool started = m_artifact->requestPng(
            this, options.compressionLevel,
            [this, generation, options](ScreenshotExportEncodingResult result) {
                if (closed() || generation != m_generation)
                    return;
                if (!result.succeeded()) {
                    m_renderGeneration.reset();
                    exportFailed(tr("The export could not be prepared: %1").arg(result.error));
                    return;
                }
                renderPrepared(generation, options, std::move(result.image));
            });
        if (!started) {
            m_renderGeneration.reset();
            exportFailed(tr("The screenshot export queue is full"));
        }
        return;
    }
    renderPrepared(generation, options);
}

void ImageExportEditor::renderPrepared(quint64 generation,
                                       const ScreenshotSaveExportOptions& options,
                                       snow_shot::storage::PreparedPngImage png) {
    const auto prepared =
        m_preparedPixels && m_preparedPixels->size == options.size ? m_preparedPixels : nullptr;
    auto encoded = std::make_shared<std::shared_ptr<pipeline::Encoded>>();
    m_job = ScreenshotExportCoordinator::shared().submit(
        this, ScreenshotExportCoordinator::Priority::Foreground,
        [source = m_source, options, prepared, cachedPdf = m_encoded ? m_encoded->pdf : nullptr,
         encoded, png](const ScreenshotExportCancellation& cancellation) {
            ScreenshotExportTaskResult result;
            auto pixels = prepared;
            if (!pixels)
                pixels = pipeline::preparePixels(source, options.size, cancellation, &result.error);
            if (pixels)
                *encoded = pipeline::render(std::move(pixels), options, cancellation, &result.error,
                                            cachedPdf, png);
            if (!*encoded)
                result.failureStage = ScreenshotExportFailureStage::Render;
            return result;
        },
        [this, generation, encoded](ScreenshotExportTaskResult result) {
            if (closed() || generation != m_generation)
                return;
            m_renderGeneration.reset();
            m_job = {};
            if (*encoded && (*encoded)->pixels) {
                m_preparedPixels = (*encoded)->pixels;
                setProperty("preparedPixelsIdentity",
                            QVariant::fromValue<qulonglong>(
                                reinterpret_cast<quintptr>(m_preparedPixels.get())));
            }
            if (!result.succeeded() || !*encoded) {
                exportFailed(tr("The export could not be prepared: %1").arg(result.error));
                return;
            }
            m_encoded = *encoded;
            updateOutputDescription();
            setProperty("encodedGeneration", QVariant::fromValue(generation));
            if (generation == m_generation)
                startRender();
        },
        screenshot_export_memory::resizedEncodingWorkingBytes(
            m_source.rows.size, options.size, options.format,
            {options.quality, options.compressionLevel}, bool(prepared)));
    if (!m_job.isValid()) {
        m_renderGeneration.reset();
        exportFailed(tr("The screenshot export queue is full"));
    }
}

void ImageExportEditor::cancelDecode() {
    ++m_decodeSerial;
    m_decodeJob.cancel();
    m_decodeJob = {};
}

void ImageExportEditor::publishPreview(QImage image, const ScreenshotSaveExportOptions& options) {
    m_renderedPreviewOptions = options;
    m_preview->setOutput(std::move(image), options.size,
                         [encoded = m_encoded](QRect region,
                                               const ScreenshotExportCancellation& cancellation,
                                               QString* error) {
                             return pipeline::decodeRegion(*encoded, region, cancellation, error);
                         });
    m_preview->setBusy(false);
    setProperty("previewGeneration", QVariant::fromValue(m_generation));
}

void ImageExportEditor::startDecode() {
    if (closed() || saving() || !m_encoded || !m_requestedOptions ||
        m_encoded->options != *m_requestedOptions)
        return;
    if (m_renderedPreviewOptions &&
        samePreviewPixels(*m_renderedPreviewOptions, *m_requestedOptions)) {
        const bool metadataChanged = *m_renderedPreviewOptions != *m_requestedOptions;
        m_renderedPreviewOptions = m_requestedOptions;
        m_preview->setBusy(false);
        if (metadataChanged)
            setProperty("previewGeneration", QVariant::fromValue(m_generation));
        return;
    }
    if (m_encoded->codecResult.roundTrip == snow::image::PixelRoundTrip::exact &&
        m_encoded->pixels) {
        if (m_encoded->options.size == m_source.rows.size) {
            publishPreview(m_source.preview, m_encoded->options);
            return;
        }
        const QImage& exact = m_encoded->pixels->exactImage;
        if (!exact.isNull() && exact.width() <= 2048 && exact.height() <= 2048) {
            publishPreview(exact, m_encoded->options);
            return;
        }
    }
    if (m_decodeJob.isValid())
        return;
    m_preview->setBusy(true);
    if (m_encoded->codecResult.roundTrip != snow::image::PixelRoundTrip::exact)
        setProperty("previewDecodeCount", property("previewDecodeCount").toInt() + 1);
    const quint64 generation = m_generation;
    const quint64 serial = ++m_decodeSerial;
    const auto options = m_encoded->options;
    m_decodeJob = ScreenshotExportCoordinator::shared().submit(
        this, ScreenshotExportCoordinator::Priority::Foreground,
        [encoded = m_encoded, source = m_source](const ScreenshotExportCancellation& cancellation) {
            ScreenshotExportTaskResult result;
            result.image = pipeline::preview(*encoded, source, cancellation, &result.error);
            if (result.image.isNull())
                result.failureStage = ScreenshotExportFailureStage::Render;
            return result;
        },
        [this, generation, serial, options](ScreenshotExportTaskResult result) {
            if (closed() || serial != m_decodeSerial || generation != m_generation || saving())
                return;
            m_decodeJob = {};
            m_preview->setBusy(false);
            if (!result.succeeded()) {
                showError(tr("The encoded preview could not be displayed: %1").arg(result.error));
                return;
            }
            publishPreview(std::move(result.image), options);
        },
        screenshot_export_memory::previewWorkingBytes(options.size));
    if (!m_decodeJob.isValid()) {
        m_preview->setBusy(false);
        showError(tr("The screenshot export queue is full"));
    }
}

void ImageExportEditor::loadSource(std::shared_ptr<ScreenshotExportArtifact> artifact) {
    const auto revision = ++m_sourceRevision;
    if (m_pendingArtifact && m_pendingArtifact != artifact)
        m_pendingArtifact->cancel();
    m_sourceJob.cancel();
    m_pendingArtifact = artifact;
    updateOutputDescription();
    m_preview->setBusy(true);
    updatePresentation();
    const bool started = artifact->requestRowSource(
        this, [this, artifact, revision](ScreenshotImageRowSource rows, QString error) {
            if (closed() || revision != m_sourceRevision)
                return;
            if (!error.isEmpty() || !rows.isValid()) {
                sourceFailed(error);
                return;
            }
            auto result = std::make_shared<pipeline::Source>();
            m_sourceJob = ScreenshotExportCoordinator::shared().submit(
                this, ScreenshotExportCoordinator::Priority::Foreground,
                [rows, result](const ScreenshotExportCancellation& cancellation) {
                    ScreenshotExportTaskResult status;
                    *result = pipeline::prepare(rows, cancellation, &status.error);
                    if (!result->rows.isValid())
                        status.failureStage = ScreenshotExportFailureStage::Source;
                    return status;
                },
                [this, result, artifact, revision](ScreenshotExportTaskResult status) {
                    if (closed() || revision != m_sourceRevision)
                        return;
                    if (!status.succeeded() || !result->rows.isValid()) {
                        sourceFailed(status.error);
                        return;
                    }
                    m_sourceJob = {};
                    m_pendingArtifact.reset();
                    if (m_source.rows.isValid())
                        ++m_generation;
                    m_renderTimer.stop();
                    m_job.cancel();
                    cancelDecode();
                    m_renderGeneration.reset();
                    m_requestedOptions.reset();
                    m_renderedPreviewOptions.reset();
                    m_preparedPixels.reset();
                    m_encoded.reset();
                    m_artifact = artifact;
                    m_source = *result;
                    m_state.sourceSize = result->rows.size;
                    m_state.output.size = m_state.sourceSize;
                    syncDimensionInputs();
                    updatePresentation();
                    m_preview->setSource(
                        m_source.preview, m_state.sourceSize,
                        [rows = m_source.rows](QRect region,
                                               const ScreenshotExportCancellation& cancellation,
                                               QString* error) {
                            return pipeline::readRegion(rows, region, cancellation, error);
                        });
                    m_preview->setOutput(m_source.preview, m_state.sourceSize);
                    changed();
                },
                screenshot_export_memory::previewWorkingBytes(rows.size));
            if (!m_sourceJob.isValid())
                sourceFailed(tr("The screenshot export queue is full"));
        });
    if (!started)
        sourceFailed({});
}

void ImageExportEditor::sourceFailed(const QString& error) {
    m_pendingArtifact.reset();
    updateOutputDescription();
    updatePresentation();
    m_preview->setBusy(false);
    showError(error.isEmpty() ? tr("The screenshot could not be prepared")
                              : tr("The screenshot could not be prepared: %1").arg(error));
}

void ImageExportEditor::cancelRender(bool cancelSource) {
    m_phase = Phase::Closed;
    ++m_sourceRevision;
    ++m_generation;
    m_renderTimer.stop();
    m_sourceJob.cancel();
    m_job.cancel();
    cancelDecode();
    if (m_pendingArtifact)
        m_pendingArtifact->cancel();
    if (cancelSource && m_artifact)
        m_artifact->cancel();
}

bool ScreenshotSaveDialogState::lossless() const {
    return output.quality == 100 && formatSupportsLossless(output.format);
}
