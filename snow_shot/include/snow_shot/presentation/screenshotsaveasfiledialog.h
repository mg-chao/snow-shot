#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTSAVEASFILEDIALOG_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTSAVEASFILEDIALOG_H

#include "snow_shot/presentation/screenshotimagefileservice.h"

#include <QObject>
#include <QSize>
#include <functional>
#include <memory>

class QWidget;
class ScreenshotExportArtifact;

struct ScreenshotSaveExportOptions {
    QSize size;
    ScreenshotImageFileFormat format = ScreenshotImageFileFormat::Png;
    int quality = 100;
    ScreenshotCompressionLevel compressionLevel = ScreenshotCompressionLevel::Medium;
    ScreenshotPdfPageSize pdfPageSize = ScreenshotPdfPageSize::PortraitA4;
    QString pdfTitle{};
    friend bool operator==(const ScreenshotSaveExportOptions&,
                           const ScreenshotSaveExportOptions&) = default;
};

struct ScreenshotSaveDialogState {
    QString directory;
    // A literal stem, including any dots. The selected format supplies the extension.
    QString filename;
    QSize sourceSize;
    ScreenshotSaveExportOptions output;
    bool lockAspectRatio = true;
    [[nodiscard]] static ScreenshotSaveDialogState
    initial(QSize sourceSize, const QDateTime& timestamp = QDateTime::currentDateTime(),
            const snow_shot::storage::ScreenshotWindowNames& names = {});
    void setDimension(bool width, int value);
    [[nodiscard]] QString imageValidationError() const;
    [[nodiscard]] QString validationError() const;
    [[nodiscard]] QString outputPath() const;
    [[nodiscard]] bool lossless() const;
};

class ScreenshotSaveAsFileDialog final {
  public:
    // Saved and Finished run only when the dialog is dismissed, Finished before Saved. The result
    // view can save repeatedly; Saved receives the latest successful path. Finished reports
    // whether any save succeeded, even when the user subsequently cancels further editing.
    using Saved = std::function<void(const QString& path)>;
    // Invoked once for every successful write, before the result view or dismissal.
    using Written =
        std::function<void(const ScreenshotImageFileSaveResult&, ScreenshotImageFileFormat)>;
    // Captures per-write state after validation and overwrite confirmation.
    using BeforeWrite = std::function<Written()>;
    using Finished = std::function<void(bool saved)>;
    // owner selects the display/centering; stackingOwner optionally keeps a floating toolbar
    // below the dialog without changing the toolbar's visibility.
    [[nodiscard]] static bool open(QObject* lifetime, QWidget* owner,
                                   std::shared_ptr<ScreenshotExportArtifact> source,
                                   Saved saved = {}, Finished finished = {},
                                   QWidget* stackingOwner = nullptr,
                                   Qt::WindowModality modality = Qt::ApplicationModal,
                                   BeforeWrite beforeWrite = {}, bool retainFile = false);
    [[nodiscard]] static bool open(QObject* lifetime, QWidget* owner, const QImage& image,
                                   Saved saved = {}, Finished finished = {},
                                   QWidget* stackingOwner = nullptr,
                                   Qt::WindowModality modality = Qt::ApplicationModal,
                                   BeforeWrite beforeWrite = {}, bool retainFile = false);
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTSAVEASFILEDIALOG_H
