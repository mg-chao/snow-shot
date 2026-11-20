// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/presentation/screenshotclouduploadservice.h"
#include "snow_shot/storage/exporttasks.h"

// Freeze preferences before an asynchronous primary action. The caller supplies only a
// confirmed result. No destination invoked here is a user-command entry point.
struct ScreenshotExportTasks {
    using Artifact = std::shared_ptr<ScreenshotExportArtifact>;
    using Trigger = snow_shot::storage::ExportTrigger;
    using Task = snow_shot::storage::ExportTask;

    static ScreenshotExportTasks capture(Trigger trigger,
                                         snow_shot::storage::ExportTasks excluded = {});
    void deferClipboardPublication();
    void run(QObject* owner, Artifact image, ScreenshotEncodedExport file = {},
             QImage submittedPrint = {}) const;
    snow_shot::storage::ExportTasks selected;
    Trigger trigger{};
    ScreenshotCloudUploadOptions options;
    QString directory;
    QDateTime requestedAt;
    snow_shot::storage::ScreenshotWindowNames windowNames;
    bool clipboardFile = false;
    ScreenshotClipboardService::PublicationId clipboardPublication = 0;
    std::optional<snow_shot::CloudUploadConfiguration> cloud;
    Artifact pinSource;
    std::function<void(Artifact)> pin;
    std::function<void(QString)> reportError;
};
