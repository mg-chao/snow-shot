#include "ui/edit_size_format_window.h"

#include "antd_icons.h"
#include "theme/theme_manager.h"
#include "widgets/alert.h"
#include "widgets/button.h"
#include "widgets/input_number.h"
#include "widgets/modal.h"
#include "widgets/select.h"
#include "widgets/slider.h"
#include "widgets/switch.h"

#include <snow/image/service.h>

#include <QAbstractButton>
#include <QCloseEvent>
#include <QCursor>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#if defined(Q_OS_WIN) || defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <qt_windows.h>
#endif

#include <algorithm>
#include <cmath>

namespace snow::image_viewer {
namespace {

using adqt::widgets::AdAlert;
using adqt::widgets::AdButton;
using adqt::widgets::AdInputNumber;
using adqt::widgets::AdModal;
using adqt::widgets::AdSelect;
using adqt::widgets::AdSlider;
using adqt::widgets::AdSwitch;
namespace outlined = adqt::icons::antd::outlined;

QVector<AdSelect::Option> options(std::initializer_list<std::pair<int, QString>> values) {
    QVector<AdSelect::Option> result;
    for (const auto& [value, label] : values)
        result.push_back({value, label});
    return result;
}

bool formatAvailable(const snow::image::Service& service, snow::image::Format format) {
    return service.encoder_info(format) != nullptr;
}

EditExportSettings defaultEditSettings(const QString& sourcePath, const QSize& sourceSize,
                                       bool animated) {
    snow::image::Service service;
    EditExportSettings settings;
    settings.sourceSize = sourceSize;
    settings.width = sourceSize.width();
    settings.height = sourceSize.height();
    settings.format =
        snow::image::format_from_extension(QFileInfo(sourcePath).suffix().toStdString());
    if (!formatAvailable(service, settings.format)) {
        settings.format = animated && formatAvailable(service, snow::image::Format::webp)
                              ? snow::image::Format::webp
                              : snow::image::Format::png;
    }
    settings.encode.format = settings.format;
    settings.encode.preserve_metadata = false;
    settings.encode.compression_level = 6;
    settings.encode.quality = 0;
    settings.encode.effort = 0;
    if (const snow::image::EncoderInfo* encoder = service.encoder_info(settings.format)) {
        if (snow::image::has_feature(encoder->features, snow::image::EncoderFeature::quality))
            settings.encode.quality = encoder->quality.default_value;
        if (snow::image::has_feature(encoder->features, snow::image::EncoderFeature::effort))
            settings.encode.effort = encoder->effort.default_value;
        if (snow::image::has_feature(encoder->features, snow::image::EncoderFeature::lossless))
            settings.encode.lossless_effort = encoder->lossless_effort.default_value;
        if (snow::image::has_feature(encoder->features,
                                     snow::image::EncoderFeature::compression_level))
            settings.encode.compression_level = encoder->compression_level.default_value;
        settings.encode.preserve_metadata =
            snow::image::has_feature(encoder->features, snow::image::EncoderFeature::metadata);
    }
    if (settings.format == snow::image::Format::jpeg ||
        settings.format == snow::image::Format::jxl) {
        settings.encode.progressive = true;
    }
    return settings;
}

constexpr int kEditorWindowHeight = 800;
constexpr int kEditorControlHeight = 32;
constexpr int kNativeDragFallbackHeight = 48;

QWidget* deepestChildAt(QWidget* root, const QPoint& rootLocalPos) {
    QWidget* current = root;
    QPoint currentLocalPos = rootLocalPos;
    while (current && current->childAt(currentLocalPos)) {
        current = current->childAt(currentLocalPos);
        currentLocalPos = current->mapFrom(root, rootLocalPos);
    }
    return current;
}

bool blocksWindowDrag(QWidget* widget, const QWidget* boundary) {
    for (QWidget* current = widget; current && current != boundary;
         current = current->parentWidget()) {
        if (current->testAttribute(Qt::WA_TransparentForMouseEvents) || !current->isEnabled()) {
            continue;
        }
        if (qobject_cast<QAbstractButton*>(current)) {
            return true;
        }
        switch (current->focusPolicy()) {
        case Qt::ClickFocus:
        case Qt::StrongFocus:
        case Qt::WheelFocus:
            return true;
        default:
            break;
        }
    }
    return false;
}

} // namespace

EditSizeFormatWindow::EditSizeFormatWindow(const QString& sourcePath, const QSize& sourceSize,
                                           bool animated)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint), sourcePath_(sourcePath),
      sourceSize_(sourceSize), animated_(animated) {
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("editSizeFormatWindow"));
    setWindowTitle(QStringLiteral("Edit size and format"));
    setMinimumWidth(340);
    resize(380, kEditorWindowHeight);

    sourceInfo_ = new QLabel(QStringLiteral("%1  ·  %2 × %3")
                                 .arg(QFileInfo(sourcePath_).fileName())
                                 .arg(sourceSize_.width())
                                 .arg(sourceSize_.height()),
                             this);
    sourceInfo_->setObjectName(QStringLiteral("editorSubtle"));

    auto* content = new QWidget(this);
    contentWidget_ = content;
    content->setObjectName(QStringLiteral("editorContent"));
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 16, 0, 24);
    contentLayout->setSpacing(18);
    buildControls();

    auto* footer = new QWidget(this);
    footerWidget_ = footer;
    footer->setObjectName(QStringLiteral("editorFooter"));
    auto* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(8);
    errorAlert_ = new AdAlert(footer);
    errorAlert_->setSeverity(AdAlert::Severity::Error);
    errorAlert_->setDisplayMode(AdAlert::DisplayMode::Banner);
    errorAlert_->setClosable(false);
    errorAlert_->setAnimated(false);
    errorAlert_->hide();
    warningLabel_ = new QLabel(footer);
    warningLabel_->setObjectName(QStringLiteral("editorWarning"));
    warningLabel_->setWordWrap(true);
    warningLabel_->hide();
    auto* outputRow = new QHBoxLayout();
    outputInfo_ = new QLabel(QStringLiteral("Preparing preview…"), footer);
    outputInfo_->setObjectName(QStringLiteral("editorSubtle"));
    saveButton_ = new AdButton(QStringLiteral("Save As"), footer);
    saveButton_->setIconRef(outlined::Download());
    saveButton_->setButtonStyle(AdButton::ButtonStyle::Solid);
    saveButton_->setAccentRole(AdButton::AccentRole::Primary);
    saveButton_->setShape(AdButton::Shape::Rounded);
    saveButton_->setEnabled(false);
    outputRow->addWidget(outputInfo_, 1);
    outputRow->addWidget(saveButton_);
    footerLayout->addWidget(errorAlert_);
    footerLayout->addWidget(warningLabel_);
    footerLayout->addLayout(outputRow);

    modal_ = new AdModal(this);
    modal_->setOwnerWindow(this);
    modal_->setRenderContainer(this);
    modal_->setMode(AdModal::Mode::Overlay);
    modal_->setWindowTitle(windowTitle());
    modal_->setPreferredWidth(380);
    modal_->setCentered(true);
    modal_->setMaskVisible(false);
    modal_->setCloseOnMaskClick(false);
    modal_->setStandardButtons(AdModal::StandardButton::NoButton);
    modal_->setContentWidget(content);
    modal_->setFooterWidget(footer);
    connect(modal_, &AdModal::rejected, this, &EditSizeFormatWindow::editorClosed);
    modal_->open();
    customizeModalHeader();
    if (QWidget* modalHeader = modal_->closeButton()->parentWidget()) {
        if (QWidget* panel = modalHeader->parentWidget()) {
            panel->installEventFilter(this);
        }
    }
    contentWidget_->installEventFilter(this);
    footerWidget_->installEventFilter(this);
    syncWindowHeightToContent();
    scheduleContentHeightSync();
    applyNativeWindowChrome();

    if (QWidget* modalHeader = modal_->closeButton()->parentWidget()) {
        modalHeader->installEventFilter(this);
        const auto headerLabels = modalHeader->findChildren<QLabel*>();
        for (QLabel* label : headerLabels)
            label->installEventFilter(this);
    }

    refreshTheme();
    connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged, this,
            &EditSizeFormatWindow::refreshTheme);
}

void EditSizeFormatWindow::buildControls() {
    if (!contentWidget_)
        return;
    auto* contentLayout = qobject_cast<QVBoxLayout*>(contentWidget_->layout());
    if (!contentLayout)
        return;
    const EditExportSettings initialSettings =
        defaultEditSettings(sourcePath_, sourceSize_, animated_);

    QWidget* sizeSection = makeSection(QStringLiteral("Size"), contentWidget_);
    auto* sizeLayout = qobject_cast<QVBoxLayout*>(sizeSection->layout());
    presetSelect_ = new AdSelect(sizeSection);
    presetSelect_->setPopupLayerMode(AdSelect::PopupLayerMode::QtTool);
    presetSelect_->setOptions(options({{25, QStringLiteral("25%")},
                                       {33, QStringLiteral("33%")},
                                       {50, QStringLiteral("50%")},
                                       {67, QStringLiteral("67%")},
                                       {75, QStringLiteral("75%")},
                                       {100, QStringLiteral("100%")},
                                       {125, QStringLiteral("125%")},
                                       {150, QStringLiteral("150%")},
                                       {200, QStringLiteral("200%")},
                                       {-1, QStringLiteral("Custom")}}));
    presetSelect_->setCurrentData(100);
    sizeLayout->addWidget(makeControlRow(QStringLiteral("Scale"), presetSelect_, sizeSection));
    widthInput_ = new AdInputNumber(sizeSection);
    widthInput_->setRange(1, 1048576);
    widthInput_->setDecimals(0);
    widthInput_->setValue(initialSettings.width);
    widthInput_->setSuffixText(QStringLiteral(" px"));
    heightInput_ = new AdInputNumber(sizeSection);
    heightInput_->setRange(1, 1048576);
    heightInput_->setDecimals(0);
    heightInput_->setValue(initialSettings.height);
    heightInput_->setSuffixText(QStringLiteral(" px"));
    sizeLayout->addWidget(makeControlRow(QStringLiteral("Width"), widthInput_, sizeSection));
    sizeLayout->addWidget(makeControlRow(QStringLiteral("Height"), heightInput_, sizeSection));
    resamplingSelect_ = new AdSelect(sizeSection);
    resamplingSelect_->setPopupLayerMode(AdSelect::PopupLayerMode::QtTool);
    resamplingSelect_->setOptions(options(
        {{static_cast<int>(snow::image::ResamplingMethod::lanczos3), QStringLiteral("Lanczos3")},
         {static_cast<int>(snow::image::ResamplingMethod::linear), QStringLiteral("Linear")},
         {static_cast<int>(snow::image::ResamplingMethod::nearest), QStringLiteral("Nearest")}}));
    resamplingSelect_->setCurrentData(static_cast<int>(initialSettings.resampling));
    sizeLayout->addWidget(
        makeControlRow(QStringLiteral("Resampling"), resamplingSelect_, sizeSection));
    aspectSwitch_ = new AdSwitch(sizeSection);
    aspectSwitch_->setChecked(initialSettings.maintainAspectRatio);
    premultiplySwitch_ = new AdSwitch(sizeSection);
    premultiplySwitch_->setChecked(initialSettings.premultiplyAlpha);
    linearRgbSwitch_ = new AdSwitch(sizeSection);
    linearRgbSwitch_->setChecked(initialSettings.linearRgb);
    sizeLayout->addWidget(
        makeSwitchRow(QStringLiteral("Maintain aspect ratio"), aspectSwitch_, sizeSection));
    sizeLayout->addWidget(
        makeSwitchRow(QStringLiteral("Premultiply alpha"), premultiplySwitch_, sizeSection));
    sizeLayout->addWidget(
        makeSwitchRow(QStringLiteral("Linear RGB"), linearRgbSwitch_, sizeSection));
    contentLayout->addWidget(sizeSection);

    QWidget* paletteSection = makeSection(QStringLiteral("Reduce palette"), contentWidget_);
    auto* paletteLayout = qobject_cast<QVBoxLayout*>(paletteSection->layout());
    paletteSwitch_ = new AdSwitch(paletteSection);
    paletteLayout->addWidget(
        makeSwitchRow(QStringLiteral("Limit colors"), paletteSwitch_, paletteSection));
    colorsInput_ = new AdInputNumber(paletteSection);
    colorsInput_->setRange(2, 256);
    colorsInput_->setDecimals(0);
    colorsInput_->setValue(initialSettings.paletteColors);
    colorsRow_ = makeControlRow(QStringLiteral("Colors"), colorsInput_, paletteSection);
    paletteLayout->addWidget(colorsRow_);
    ditheringSlider_ = new AdSlider(paletteSection);
    ditheringSlider_->setRange(0, 100);
    ditheringSlider_->setValue(initialSettings.ditheringPercent);
    ditheringSlider_->setTooltipFormatter(
        [](double value) { return QStringLiteral("%1%").arg(std::lround(value)); });
    ditheringRow_ = makeControlRow(QStringLiteral("Dithering"), ditheringSlider_, paletteSection);
    paletteLayout->addWidget(ditheringRow_);
    colorsRow_->setEnabled(false);
    ditheringRow_->setEnabled(false);
    contentLayout->addWidget(paletteSection);

    QWidget* formatSection = makeSection(QStringLiteral("Format"), contentWidget_);
    auto* formatLayout = qobject_cast<QVBoxLayout*>(formatSection->layout());
    formatSelect_ = new AdSelect(formatSection);
    formatSelect_->setPopupLayerMode(AdSelect::PopupLayerMode::QtTool);
    snow::image::Service service;
    QVector<AdSelect::Option> formatOptions;
    for (snow::image::Format format :
         {snow::image::Format::png, snow::image::Format::jpeg, snow::image::Format::webp,
          snow::image::Format::avif, snow::image::Format::heif, snow::image::Format::jxl}) {
        if (formatAvailable(service, format)) {
            formatOptions.push_back(
                {static_cast<int>(format),
                 QString::fromUtf8(
                     snow::image::format_name(format).data(),
                     static_cast<qsizetype>(snow::image::format_name(format).size()))});
        }
    }
    formatSelect_->setOptions(formatOptions);
    formatSelect_->setCurrentData(static_cast<int>(initialSettings.format));
    formatLayout->addWidget(
        makeControlRow(QStringLiteral("Convert to"), formatSelect_, formatSection));
    losslessSwitch_ = new AdSwitch(formatSection);
    qualityInput_ = new AdInputNumber(formatSection);
    qualityInput_->setRange(0, 100);
    qualityInput_->setDecimals(0);
    qualityInput_->setSuffixText(QStringLiteral("%"));
    effortInput_ = new AdInputNumber(formatSection);
    effortInput_->setDecimals(0);
    compressionInput_ = new AdInputNumber(formatSection);
    compressionInput_->setRange(0, 9);
    compressionInput_->setDecimals(0);
    chromaSubsamplingSelect_ = new AdSelect(formatSection);
    chromaSubsamplingSelect_->setPopupLayerMode(AdSelect::PopupLayerMode::QtTool);
    chromaSubsamplingSelect_->setOptions(options({
        {-1, QStringLiteral("Auto")},
        {static_cast<int>(snow::image::ChromaSubsampling::yuv444), QStringLiteral("4:4:4")},
        {static_cast<int>(snow::image::ChromaSubsampling::yuv422), QStringLiteral("4:2:2")},
        {static_cast<int>(snow::image::ChromaSubsampling::yuv420), QStringLiteral("4:2:0")},
    }));
    chromaSubsamplingSelect_->setCurrentData(-1);
    progressiveSwitch_ = new AdSwitch(formatSection);
    interlacedSwitch_ = new AdSwitch(formatSection);
    preserveMetadataSwitch_ = new AdSwitch(formatSection);
    losslessRow_ = makeSwitchRow(QStringLiteral("Lossless"), losslessSwitch_, formatSection);
    qualityRow_ = makeControlRow(QStringLiteral("Quality"), qualityInput_, formatSection);
    effortRow_ = makeControlRow(QStringLiteral("Effort"), effortInput_, formatSection);
    compressionRow_ =
        makeControlRow(QStringLiteral("Compression"), compressionInput_, formatSection);
    chromaSubsamplingRow_ = makeControlRow(QStringLiteral("Chroma subsampling"),
                                           chromaSubsamplingSelect_, formatSection);
    progressiveRow_ =
        makeSwitchRow(QStringLiteral("Progressive"), progressiveSwitch_, formatSection);
    interlacedRow_ = makeSwitchRow(QStringLiteral("Interlaced"), interlacedSwitch_, formatSection);
    preserveMetadataRow_ =
        makeSwitchRow(QStringLiteral("Preserve metadata"), preserveMetadataSwitch_, formatSection);
    for (QWidget* row :
         {losslessRow_, qualityRow_, effortRow_, compressionRow_, chromaSubsamplingRow_,
          progressiveRow_, interlacedRow_, preserveMetadataRow_})
        formatLayout->addWidget(row);
    contentLayout->addWidget(formatSection);

    resetEncoderDefaults(initialSettings.format);
    connectControls();
    updateFormatControls();
    scheduleContentHeightSync();
}

EditExportSettings EditSizeFormatWindow::settings() const {
    EditExportSettings result;
    result.sourceSize = sourceSize_;
    result.width = static_cast<int>(std::lround(widthInput_->value()));
    result.height = static_cast<int>(std::lround(heightInput_->value()));
    result.resampling =
        static_cast<snow::image::ResamplingMethod>(resamplingSelect_->currentData().toInt());
    result.premultiplyAlpha = premultiplySwitch_->isChecked();
    result.linearRgb = linearRgbSwitch_->isChecked();
    result.maintainAspectRatio = aspectSwitch_->isChecked();
    result.reducePalette = paletteSwitch_->isChecked();
    result.paletteColors = static_cast<int>(std::lround(colorsInput_->value()));
    result.ditheringPercent = static_cast<int>(std::lround(ditheringSlider_->value()));
    result.format = static_cast<snow::image::Format>(formatSelect_->currentData().toInt());
    result.encode.format = result.format;
    result.encode.lossless = losslessSwitch_->isChecked();
    result.encode.quality = static_cast<int>(std::lround(qualityInput_->value()));
    result.encode.effort = static_cast<int>(std::lround(effortInput_->value()));
    result.encode.lossless_effort = static_cast<int>(std::lround(compressionInput_->value()));
    result.encode.compression_level = static_cast<int>(std::lround(compressionInput_->value()));
    const int chromaSubsampling = chromaSubsamplingSelect_->currentData().toInt();
    if (chromaSubsampling >= 0) {
        result.encode.chroma_subsampling =
            static_cast<snow::image::ChromaSubsampling>(chromaSubsampling);
    }
    result.encode.progressive = progressiveSwitch_->isChecked();
    result.encode.interlaced = interlacedSwitch_->isChecked();
    result.encode.preserve_metadata = preserveMetadataSwitch_->isChecked();
    return result;
}

void EditSizeFormatWindow::setBusy(bool busy) {
    saveButton_->setBusy(busy);
    saveButton_->setEnabled(!busy && previewBytes_ > 0);
    if (busy)
        outputInfo_->setText(QStringLiteral("Updating preview…"));
}

void EditSizeFormatWindow::setPreviewInfo(qint64 bytes, qint64 sourceBytes,
                                          const QString& warning) {
    previewBytes_ = bytes;
    const double change =
        sourceBytes > 0
            ? (static_cast<double>(bytes - sourceBytes) / static_cast<double>(sourceBytes)) * 100.0
            : 0.0;
    outputInfo_->setText(QStringLiteral("%1  ·  %2%3%")
                             .arg(QLocale().formattedDataSize(bytes))
                             .arg(change >= 0.0 ? QStringLiteral("+") : QString())
                             .arg(change, 0, 'f', 1));
    warningLabel_->setText(warning);
    warningLabel_->setVisible(!warning.isEmpty());
    if (validateFormatLimits()) {
        saveButton_->setEnabled(settings().isValid());
        clearError();
    }
    scheduleContentHeightSync();
}

void EditSizeFormatWindow::setError(const QString& message) {
    previewBytes_ = 0;
    errorAlert_->setText(QStringLiteral("Preview could not be generated"));
    errorAlert_->setInformativeText(message);
    errorAlert_->show();
    outputInfo_->setText(QStringLiteral("Adjust the settings and try again"));
    saveButton_->setEnabled(false);
    scheduleContentHeightSync();
}

void EditSizeFormatWindow::clearError() {
    if (errorAlert_->isVisible()) {
        errorAlert_->hide();
        scheduleContentHeightSync();
    }
}

QWidget* EditSizeFormatWindow::makeSection(const QString& title, QWidget* parent) {
    auto* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("editorSection"));
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto* label = new QLabel(title, section);
    label->setObjectName(QStringLiteral("editorSectionTitle"));
    layout->addWidget(label);
    return section;
}

QWidget* EditSizeFormatWindow::makeControlRow(const QString& label, QWidget* control,
                                              QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("editorControlRow"));
    row->setFixedHeight(kEditorControlHeight);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* text = new QLabel(label, row);
    text->setMinimumWidth(104);
    control->setMinimumWidth(150);
    control->setFixedHeight(kEditorControlHeight);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(text, 0, Qt::AlignVCenter);
    layout->addWidget(control, 1, Qt::AlignVCenter);
    return row;
}

QWidget* EditSizeFormatWindow::makeSwitchRow(const QString& label, AdSwitch* control,
                                             QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("editorControlRow"));
    row->setFixedHeight(kEditorControlHeight);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* text = new QLabel(label, row);
    control->setFixedHeight(kEditorControlHeight);
    control->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(text, 1, Qt::AlignVCenter);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    return row;
}

void EditSizeFormatWindow::connectControls() {
    connect(presetSelect_, &AdSelect::currentValueChanged, this, [this](const QVariant& value) {
        if (!syncing_ && value.toInt() > 0)
            applyPreset(value.toInt());
    });
    connect(widthInput_, &AdInputNumber::valueChanged, this, [this](double) {
        if (syncing_)
            return;
        lastEditedWidth_ = true;
        updateLinkedDimension(true);
        presetSelect_->setCurrentData(-1);
        scheduleChange(EditChangeKind::dimension_typing);
    });
    connect(heightInput_, &AdInputNumber::valueChanged, this, [this](double) {
        if (syncing_)
            return;
        lastEditedWidth_ = false;
        updateLinkedDimension(false);
        presetSelect_->setCurrentData(-1);
        scheduleChange(EditChangeKind::dimension_typing);
    });
    connect(aspectSwitch_, &QAbstractButton::toggled, this, [this](bool checked) {
        if (checked)
            updateLinkedDimension(lastEditedWidth_);
        scheduleChange();
    });
    connect(paletteSwitch_, &QAbstractButton::toggled, this, [this](bool checked) {
        colorsRow_->setEnabled(checked);
        ditheringRow_->setEnabled(checked);
        scheduleChange();
    });
    connect(formatSelect_, &AdSelect::currentValueChanged, this, [this](const QVariant& value) {
        if (syncing_)
            return;
        resetEncoderDefaults(static_cast<snow::image::Format>(value.toInt()));
        updateFormatControls();
        scheduleChange();
    });
    connect(losslessSwitch_, &QAbstractButton::toggled, this, [this](bool) {
        updateFormatControls();
        scheduleChange();
    });
    const auto schedule = [this]() { scheduleChange(EditChangeKind::discrete); };
    const auto scheduleContinuous = [this]() { scheduleChange(EditChangeKind::continuous); };
    connect(resamplingSelect_, &AdSelect::currentValueChanged, this, schedule);
    connect(premultiplySwitch_, &QAbstractButton::toggled, this, schedule);
    connect(linearRgbSwitch_, &QAbstractButton::toggled, this, schedule);
    connect(colorsInput_, &AdInputNumber::valueChanged, this, scheduleContinuous);
    connect(ditheringSlider_, &AdSlider::valueChanged, this, scheduleContinuous);
    connect(qualityInput_, &AdInputNumber::valueChanged, this, scheduleContinuous);
    connect(effortInput_, &AdInputNumber::valueChanged, this, scheduleContinuous);
    connect(compressionInput_, &AdInputNumber::valueChanged, this, scheduleContinuous);
    connect(chromaSubsamplingSelect_, &AdSelect::currentValueChanged, this, schedule);
    connect(progressiveSwitch_, &QAbstractButton::toggled, this, schedule);
    connect(interlacedSwitch_, &QAbstractButton::toggled, this, schedule);
    connect(preserveMetadataSwitch_, &QAbstractButton::toggled, this, schedule);
    connect(saveButton_, &AdButton::clicked, this, &EditSizeFormatWindow::saveRequested);
}

void EditSizeFormatWindow::scheduleChange(EditChangeKind kind) {
    if (syncing_)
        return;
    if (!validateFormatLimits())
        return;
    clearError();
    previewBytes_ = 0;
    saveButton_->setEnabled(true);
    outputInfo_->setText(QStringLiteral("Calculating exact size..."));
    emit settingsChanged(settings(), kind);
}

void EditSizeFormatWindow::applyPreset(int percent) {
    syncing_ = true;
    widthInput_->setValue(std::max(1, qRound(sourceSize_.width() * percent / 100.0)));
    heightInput_->setValue(std::max(1, qRound(sourceSize_.height() * percent / 100.0)));
    syncing_ = false;
    scheduleChange();
}

void EditSizeFormatWindow::updateLinkedDimension(bool widthChanged) {
    if (!aspectSwitch_->isChecked() || sourceSize_.width() <= 0 || sourceSize_.height() <= 0)
        return;
    syncing_ = true;
    if (widthChanged) {
        heightInput_->setValue(
            std::max(1, qRound(widthInput_->value() * sourceSize_.height() / sourceSize_.width())));
    } else {
        widthInput_->setValue(std::max(
            1, qRound(heightInput_->value() * sourceSize_.width() / sourceSize_.height())));
    }
    syncing_ = false;
}

void EditSizeFormatWindow::resetEncoderDefaults(snow::image::Format format) {
    syncing_ = true;
    losslessSwitch_->setChecked(false);
    progressiveSwitch_->setChecked(false);
    interlacedSwitch_->setChecked(false);
    compressionInput_->setValue(6);
    effortInput_->setRange(0, 10);
    chromaSubsamplingSelect_->setCurrentData(-1);
    preserveMetadataSwitch_->setChecked(false);
    snow::image::Service service;
    if (const snow::image::EncoderInfo* encoder = service.encoder_info(format)) {
        if (snow::image::has_feature(encoder->features, snow::image::EncoderFeature::quality)) {
            qualityInput_->setRange(encoder->quality.minimum, encoder->quality.maximum);
            qualityInput_->setValue(encoder->quality.default_value);
        }
        if (snow::image::has_feature(encoder->features, snow::image::EncoderFeature::effort)) {
            effortInput_->setRange(encoder->effort.minimum, encoder->effort.maximum);
            effortInput_->setValue(encoder->effort.default_value);
        }
        if (snow::image::has_feature(encoder->features,
                                     snow::image::EncoderFeature::compression_level)) {
            compressionInput_->setRange(encoder->compression_level.minimum,
                                        encoder->compression_level.maximum);
            compressionInput_->setValue(encoder->compression_level.default_value);
        }
        if (snow::image::has_feature(encoder->features, snow::image::EncoderFeature::lossless)) {
            compressionInput_->setRange(encoder->lossless_effort.minimum,
                                        encoder->lossless_effort.maximum);
            compressionInput_->setValue(encoder->lossless_effort.default_value);
        }
        preserveMetadataSwitch_->setChecked(
            snow::image::has_feature(encoder->features, snow::image::EncoderFeature::metadata));
    }
    if (format == snow::image::Format::jpeg || format == snow::image::Format::jxl) {
        progressiveSwitch_->setChecked(true);
    }
    syncing_ = false;
}

void EditSizeFormatWindow::updateFormatControls() {
    const snow::image::Format format =
        static_cast<snow::image::Format>(formatSelect_->currentData().toInt());
    const bool png = format == snow::image::Format::png;
    const bool jpeg = format == snow::image::Format::jpeg;
    const bool webp = format == snow::image::Format::webp;
    const bool avif = format == snow::image::Format::avif;
    const bool jxl = format == snow::image::Format::jxl;
    losslessRow_->setVisible(webp || avif || jxl);
    qualityRow_->setVisible((jpeg || webp || avif || jxl) && !losslessSwitch_->isChecked());
    effortRow_->setVisible((webp && !losslessSwitch_->isChecked()) || avif || jxl);
    compressionRow_->setVisible(png || (webp && losslessSwitch_->isChecked()));
    chromaSubsamplingRow_->setVisible(jpeg);
    progressiveRow_->setVisible(jpeg || jxl);
    interlacedRow_->setVisible(png);
    snow::image::Service service;
    const snow::image::EncoderInfo* encoder = service.encoder_info(format);
    preserveMetadataRow_->setVisible(
        encoder &&
        snow::image::has_feature(encoder->features, snow::image::EncoderFeature::metadata));
    scheduleContentHeightSync();
}

bool EditSizeFormatWindow::validateFormatLimits() {
    const EditExportSettings current = settings();
    snow::image::Service service;
    const snow::image::EncoderInfo* encoder = service.encoder_info(current.format);
    if (!encoder || current.width <= 0 || current.height <= 0)
        return true;
    if (static_cast<std::uint32_t>(current.width) <= encoder->limits.maximum_width &&
        static_cast<std::uint32_t>(current.height) <= encoder->limits.maximum_height)
        return true;
    const std::string_view name = snow::image::format_name(current.format);
    setError(QStringLiteral("%1 supports dimensions up to %2 x %3. The requested "
                            "size is %4 x %5.")
                 .arg(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())))
                 .arg(encoder->limits.maximum_width)
                 .arg(encoder->limits.maximum_height)
                 .arg(current.width)
                 .arg(current.height));
    return false;
}

void EditSizeFormatWindow::refreshTheme() {
    const auto& manager = adqt::theme::ThemeManager::instance();
    const auto& colors = manager.theme().palette;

    QPalette errorAlertPalette = manager.globalPalette();
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        errorAlertPalette.setColor(group, QPalette::Window, colors.colorErrorBg);
        errorAlertPalette.setColor(group, QPalette::Mid, colors.colorErrorBorder);
        errorAlertPalette.setColor(group, QPalette::WindowText, colors.colorErrorText);
        errorAlertPalette.setColor(group, QPalette::Text, colors.colorText);
        errorAlertPalette.setColor(group, QPalette::ButtonText, colors.colorTextTertiary);
        errorAlertPalette.setColor(group, QPalette::AlternateBase, colors.colorErrorBgHover);
        errorAlertPalette.setColor(group, QPalette::Button, colors.colorErrorBgActive);
        errorAlertPalette.setColor(group, QPalette::Highlight, colors.colorErrorBorderHover);
    }
    errorAlert_->setPalette(errorAlertPalette);

    setStyleSheet(
        QStringLiteral("#editSizeFormatWindow { background: %1; }"
                       "#editorContent, #editorFooter { background: transparent; }"
                       "#editorContent, #editorFooter { color: %2; }"
                       "#editorTitle { color: %2; font-size: 17px; font-weight: 600; }"
                       "#editorSectionTitle { color: %2; font-size: 13px; font-weight: 600; }"
                       "#editorSubtle { color: %3; font-size: 12px; }"
                       "#editorWarning { color: %4; font-size: 12px; }"
                       "QLabel { color: %2; }")
            .arg(colors.colorBgContainer.name(QColor::HexArgb),
                 colors.colorText.name(QColor::HexArgb),
                 colors.colorTextSecondary.name(QColor::HexArgb),
                 colors.colorWarningText.name(QColor::HexArgb)));
}

void EditSizeFormatWindow::customizeModalHeader() {
    if (!modal_ || !modal_->closeButton()) {
        return;
    }

    QWidget* header = modal_->closeButton()->parentWidget();
    auto* headerLayout = header ? qobject_cast<QHBoxLayout*>(header->layout()) : nullptr;
    auto* title = header ? header->findChild<QLabel*>(QStringLiteral("ad-modal-title"),
                                                      Qt::FindDirectChildrenOnly)
                         : nullptr;
    if (!headerLayout || !title) {
        return;
    }

    auto* titleBlock = new QWidget(header);
    titleBlock->setObjectName(QStringLiteral("editorHeaderContent"));
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(3);

    headerLayout->removeWidget(title);
    title->setParent(titleBlock);
    title->setObjectName(QStringLiteral("editorTitle"));
    sourceInfo_->setParent(titleBlock);
    titleLayout->addWidget(title);
    titleLayout->addWidget(sourceInfo_);
    headerLayout->insertWidget(0, titleBlock, 1, Qt::AlignTop);
}

void EditSizeFormatWindow::closeEvent(QCloseEvent* event) {
    if (modal_ && modal_->isOpen()) {
        modal_->reject();
    } else {
        emit editorClosed();
    }
    QWidget::closeEvent(event);
}

void EditSizeFormatWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    scheduleContentHeightSync();
}

bool EditSizeFormatWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event && event->type() == QEvent::LayoutRequest) {
        QWidget* panel = nullptr;
        if (modal_ && modal_->closeButton()) {
            QWidget* header = modal_->closeButton()->parentWidget();
            panel = header ? header->parentWidget() : nullptr;
        }
        if (watched == contentWidget_ || watched == footerWidget_ || watched == panel) {
            scheduleContentHeightSync();
        }
    }
    if (event && event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton &&
            isNativeDragAreaAt(mouseEvent->globalPosition().toPoint())) {
            startNativeWindowDrag();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EditSizeFormatWindow::scheduleContentHeightSync() {
    if (contentHeightSyncPending_) {
        return;
    }
    contentHeightSyncPending_ = true;
    QTimer::singleShot(0, this, [this]() {
        contentHeightSyncPending_ = false;
        syncWindowHeightToContent();
    });
}

void EditSizeFormatWindow::syncWindowHeightToContent() {
    if (!modal_ || !modal_->closeButton()) {
        return;
    }
    QWidget* header = modal_->closeButton()->parentWidget();
    QWidget* panel = header ? header->parentWidget() : nullptr;
    if (!panel) {
        return;
    }

    panel->setMinimumHeight(0);
    panel->setMaximumHeight(QWIDGETSIZE_MAX);
    panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (contentWidget_ && contentWidget_->layout()) {
        contentWidget_->layout()->activate();
    }
    if (footerWidget_ && footerWidget_->layout()) {
        footerWidget_->layout()->activate();
    }
    if (panel->layout()) {
        panel->layout()->activate();
    }

    const int contentHeight = panel->sizeHint().height();
    if (contentHeight > 0 && height() != contentHeight) {
        resize(width(), contentHeight);
    }
    if (QLayout* overlayLayout =
            panel->parentWidget() ? panel->parentWidget()->layout() : nullptr) {
        overlayLayout->setAlignment(panel, Qt::AlignHCenter);
    }
}

void EditSizeFormatWindow::applyNativeWindowChrome() {
#if defined(Q_OS_WIN) || defined(_WIN32)
    // Qt transports the native HWND through its integer-valued WId type.
    const HWND hwnd = reinterpret_cast<HWND>(winId()); // NOLINT(performance-no-int-to-ptr)
    if (!hwnd) {
        return;
    }

    setAttribute(Qt::WA_TranslucentBackground, false);

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style |= WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    style &= ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_LAYERED;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);

    const DWMNCRENDERINGPOLICY renderingPolicy = DWMNCRP_ENABLED;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &renderingPolicy,
                          sizeof(renderingPolicy));
    const MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

bool EditSizeFormatWindow::isNativeDragAreaAt(const QPoint& globalPos) const {
    if (!modal_ || !modal_->closeButton()) {
        return false;
    }
    QWidget* header = modal_->closeButton()->parentWidget();
    QWidget* panel = header ? header->parentWidget() : nullptr;
    if (!header || !panel || !header->isVisible() || !panel->isVisible()) {
        return false;
    }

    const QPoint panelLocalPos = panel->mapFromGlobal(globalPos);
    if (!panel->rect().contains(panelLocalPos)) {
        return false;
    }
    const QRect headerRect(header->mapTo(panel, QPoint(0, 0)), header->size());
    const int dragHeight = headerRect.isValid()
                               ? std::clamp(headerRect.bottom() + 1, 0, panel->height())
                               : std::min(kNativeDragFallbackHeight, panel->height());
    if (panelLocalPos.y() >= dragHeight) {
        return false;
    }
    return !blocksWindowDrag(deepestChildAt(panel, panelLocalPos), panel);
}

bool EditSizeFormatWindow::startNativeWindowDrag() {
    if (QWindow* handle = windowHandle(); handle && handle->startSystemMove()) {
        return true;
    }
#if defined(Q_OS_WIN) || defined(_WIN32)
    // Qt transports the native HWND through its integer-valued WId type.
    const HWND hwnd = reinterpret_cast<HWND>(winId()); // NOLINT(performance-no-int-to-ptr)
    if (!hwnd) {
        return false;
    }
    POINT cursorPos{};
    GetCursorPos(&cursorPos);
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursorPos.x, cursorPos.y));
    return true;
#else
    return false;
#endif
}

bool EditSizeFormatWindow::nativeEvent(const QByteArray& eventType, void* message,
                                       qintptr* result) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    Q_UNUSED(eventType)
    if (message && result) {
        const auto* msg = static_cast<const MSG*>(message);
        switch (msg->message) {
        case WM_NCCALCSIZE:
            *result = 0;
            return true;
        case WM_NCHITTEST: {
            LRESULT dwmResult = 0;
            if (DwmDefWindowProc(msg->hwnd, msg->message, msg->wParam, msg->lParam, &dwmResult) !=
                0) {
                *result = dwmResult;
                return true;
            }
            *result = isNativeDragAreaAt(QCursor::pos()) ? HTCAPTION : HTCLIENT;
            return true;
        }
        default:
            break;
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

} // namespace snow::image_viewer
