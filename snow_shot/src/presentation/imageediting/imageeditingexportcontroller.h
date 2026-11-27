// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/presentation/screenshotclouduploadservice.h"
#include "snow_shot/presentation/screenshotexportartifact.h"
#include "snow_shot/presentation/screenshotrecognitionfileexport.h"
#include <QMimeData>
#include <QObject>
#include <functional>
#include <memory>
#include <variant>

class ScreenshotPrintService;
class ScreenshotAirDropService;
class QWidget;

namespace snow_shot::presentation {
// Owns output requests and all of their dialogs, jobs and cancellation state.
// Content selection and saved checkpoints remain the editor's responsibility.
class ImageEditingExportController final : public QObject {
  public:
    enum class Operation { Copy, Save, QuickSave, Print, Upload, AirDrop, Pin };
    using Content = std::variant<ScreenshotExportSource, ScreenshotRecognitionFileSnapshot>;
    using CopyPrepared = std::function<void(std::unique_ptr<QMimeData>, QString)>;
    using PrepareCopy = std::function<bool(QObject* request, CopyPrepared prepared)>;
    using CopyContent = std::variant<ScreenshotExportSource, PrepareCopy>;
    struct CopyOptions {
        ScreenshotClipboardService::PublicationId publication = 0;
        std::optional<ScreenshotCloudUploadOptions> file;
        QDateTime requestedAt;
    };
    using Completion = std::function<void(bool, QString)>;
    using Pin = std::function<bool(QImage, std::function<void(bool)>)>;

    explicit ImageEditingExportController(ScreenshotPrintService* printing, QObject* parent,
                                          Pin pin = {},
                                          ScreenshotAirDropService* airDrop = nullptr);
    ~ImageEditingExportController() override;
    bool busy() const;
    bool canStart(Operation operation) const;
    void start(Operation operation, Content content, QWidget* owner, QWidget* stackingOwner,
               Completion completion);
    bool startCopy(CopyContent content, CopyOptions options, QWidget* owner, Completion completion);
    void cancelCopy();
    // Retires the request before cancelling services so reentrant and late callbacks are ignored.
    void cancel();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace snow_shot::presentation
