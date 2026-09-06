#pragma once

#include "editing/edit_export_settings.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QByteArray;
class QCloseEvent;
class QEvent;
class QLabel;
class QPoint;
class QResizeEvent;
QT_END_NAMESPACE

namespace adqt::widgets {
class AdAlert;
class AdButton;
class AdInputNumber;
class AdModal;
class AdSelect;
class AdSlider;
class AdSwitch;
} // namespace adqt::widgets

namespace snow::image_viewer {

class EditSizeFormatWindow final : public QWidget {
    Q_OBJECT

  public:
    explicit EditSizeFormatWindow(const QString& sourcePath, const QSize& sourceSize,
                                  bool animated);

    EditExportSettings settings() const;
    void setBusy(bool busy);
    void setPreviewInfo(qint64 bytes, qint64 sourceBytes, const QString& warning);
    void setError(const QString& message);
    void clearError();

  signals:
    void settingsChanged(const snow::image_viewer::EditExportSettings& settings,
                         snow::image_viewer::EditChangeKind kind);
    void saveRequested();
    void editorClosed();

  protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

  private:
    QWidget* makeSection(const QString& title, QWidget* parent);
    QWidget* makeControlRow(const QString& label, QWidget* control, QWidget* parent);
    QWidget* makeSwitchRow(const QString& label, adqt::widgets::AdSwitch* control, QWidget* parent);
    void buildControls();
    void connectControls();
    void scheduleChange(EditChangeKind kind = EditChangeKind::discrete);
    void applyPreset(int percent);
    void updateLinkedDimension(bool widthChanged);
    void updateFormatControls();
    bool validateFormatLimits();
    void resetEncoderDefaults(snow::image::Format format);
    void refreshTheme();
    void customizeModalHeader();
    void scheduleContentHeightSync();
    void syncWindowHeightToContent();
    void applyNativeWindowChrome();
    bool isNativeDragAreaAt(const QPoint& globalPos) const;
    bool startNativeWindowDrag();

    QString sourcePath_;
    QSize sourceSize_;
    bool animated_ = false;
    bool syncing_ = false;
    bool lastEditedWidth_ = true;
    qint64 previewBytes_ = 0;
    bool contentHeightSyncPending_ = false;
    adqt::widgets::AdModal* modal_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QWidget* footerWidget_ = nullptr;

    QLabel* sourceInfo_ = nullptr;
    QLabel* outputInfo_ = nullptr;
    QLabel* warningLabel_ = nullptr;
    adqt::widgets::AdAlert* errorAlert_ = nullptr;
    adqt::widgets::AdButton* saveButton_ = nullptr;
    adqt::widgets::AdSelect* presetSelect_ = nullptr;
    adqt::widgets::AdInputNumber* widthInput_ = nullptr;
    adqt::widgets::AdInputNumber* heightInput_ = nullptr;
    adqt::widgets::AdSelect* resamplingSelect_ = nullptr;
    adqt::widgets::AdSwitch* aspectSwitch_ = nullptr;
    adqt::widgets::AdSwitch* premultiplySwitch_ = nullptr;
    adqt::widgets::AdSwitch* linearRgbSwitch_ = nullptr;
    adqt::widgets::AdSwitch* paletteSwitch_ = nullptr;
    adqt::widgets::AdInputNumber* colorsInput_ = nullptr;
    adqt::widgets::AdSlider* ditheringSlider_ = nullptr;
    QWidget* colorsRow_ = nullptr;
    QWidget* ditheringRow_ = nullptr;
    adqt::widgets::AdSelect* formatSelect_ = nullptr;
    adqt::widgets::AdSwitch* losslessSwitch_ = nullptr;
    adqt::widgets::AdInputNumber* qualityInput_ = nullptr;
    adqt::widgets::AdInputNumber* effortInput_ = nullptr;
    adqt::widgets::AdInputNumber* compressionInput_ = nullptr;
    adqt::widgets::AdSelect* chromaSubsamplingSelect_ = nullptr;
    adqt::widgets::AdSwitch* progressiveSwitch_ = nullptr;
    adqt::widgets::AdSwitch* interlacedSwitch_ = nullptr;
    adqt::widgets::AdSwitch* preserveMetadataSwitch_ = nullptr;
    QWidget* losslessRow_ = nullptr;
    QWidget* qualityRow_ = nullptr;
    QWidget* effortRow_ = nullptr;
    QWidget* compressionRow_ = nullptr;
    QWidget* chromaSubsamplingRow_ = nullptr;
    QWidget* progressiveRow_ = nullptr;
    QWidget* interlacedRow_ = nullptr;
    QWidget* preserveMetadataRow_ = nullptr;
};

} // namespace snow::image_viewer
