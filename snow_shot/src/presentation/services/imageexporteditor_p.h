// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "screenshotsaveexportpipeline.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QWidget>

class ScreenshotSavePreviewCanvas;
class QLabel;
class AspectRatioLockButton;
class QStackedWidget;
namespace adqt::widgets {
class AdForm;
class AdFormItem;
class AdInputNumber;
class AdSelect;
class AdSegmented;
class AdSlider;
} // namespace adqt::widgets

class ImageExportEditor : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ScreenshotSaveAsFileDialog)
  protected:
    enum class Phase { Editing, Saving, Result, Closed };
    ImageExportEditor(ScreenshotSaveDialogState state, bool includePdf);
    void initializeControls();
    void retranslateControls();
    void loadSource(std::shared_ptr<ScreenshotExportArtifact> artifact);
    void cancelRender(bool cancelSource);
    void sourceFailed(const QString& error);
    bool saving() const {
        return m_phase == Phase::Saving;
    }
    bool closed() const {
        return m_phase == Phase::Closed;
    }
    virtual void updatePresentation() = 0;
    virtual void validateFields() = 0;
    virtual void exportFailed(const QString& message) = 0;
    virtual void exportReady() = 0;
    adqt::widgets::AdFormItem* addField(const char* label, QWidget* editor, const char* key);
    void persistFormatOptions();
    void showError(const QString& message);
    void commitDimensionEdit();
    void updateControls();
    void changed();
    void startRender();
    void cancelDecode();
    void startDecode();
    ScreenshotSaveDialogState m_state;
    screenshot_save_export::Source m_source;
    std::shared_ptr<screenshot_save_export::Encoded> m_encoded;
    QTimer m_renderTimer;
    Phase m_phase = Phase::Editing;
    ScreenshotSavePreviewCanvas* m_preview = nullptr;
    QLabel* m_outputDescription = nullptr;
    adqt::widgets::AdForm* m_form = nullptr;
    adqt::widgets::AdInputNumber* m_width = nullptr;
    adqt::widgets::AdInputNumber* m_height = nullptr;
    QLabel* m_error = nullptr;
    QStackedWidget* m_rightPanel = nullptr;
    std::shared_ptr<ScreenshotExportArtifact> m_artifact;
    std::shared_ptr<ScreenshotExportArtifact> m_pendingArtifact;

  private:
    adqt::widgets::AdInputNumber* number(const char* name);
    void loadRememberedFormatOptions();
    void rememberCurrentFormatOptions();
    void applyRememberedFormatOptions();
    QJsonObject serializedFormatOptions() const;
    void updateSizeLabelTheme();
    void updateOutputDescriptionTheme();
    QString formattedOutputBytes(qint64 bytes) const;
    void updateOutputDescription();
    void syncDimensionInputs(bool preserveEmpty = false);
    void dimensionChanged(bool width, double value);
    void renderPrepared(quint64 generation, const ScreenshotSaveExportOptions& options,
                        snow_shot::storage::PreparedPngImage png = {});
    void publishPreview(QImage image, const ScreenshotSaveExportOptions& options);
    std::optional<ScreenshotSaveExportOptions> m_requestedOptions;
    std::optional<ScreenshotSaveExportOptions> m_renderedPreviewOptions;
    std::shared_ptr<screenshot_save_export::PreparedPixels> m_preparedPixels;
    ScreenshotExportJobHandle m_job;
    ScreenshotExportJobHandle m_decodeJob;
    quint64 m_generation = 0;
    quint64 m_decodeSerial = 0;
    std::optional<quint64> m_renderGeneration;
    adqt::widgets::AdSelect* m_format = nullptr;
    QLabel* m_sizeLabel = nullptr;
    adqt::widgets::AdSegmented* m_sizeUnit = nullptr;
    bool m_percentage = false;
    AspectRatioLockButton* m_lock = nullptr;
    adqt::widgets::AdSlider* m_quality = nullptr;
    adqt::widgets::AdFormItem* m_qualityItem = nullptr;
    adqt::widgets::AdSelect* m_compression = nullptr;
    adqt::widgets::AdFormItem* m_compressionItem = nullptr;
    QHash<int, ScreenshotImageEncodingOptions> m_formatOptions;
    QSet<int> m_modifiedFormats;
    ScreenshotExportJobHandle m_sourceJob;
    quint64 m_sourceRevision = 0;
    bool m_includePdf;
};
