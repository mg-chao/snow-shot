#pragma once

#include "editing/edit_export_settings.h"

#include <snow/image/document.h>
#include <snow/image/processing.h>
#include <snow/image/result.h>

#include <QString>
#include <QJsonObject>

#include <stop_token>
#include <chrono>
#include <functional>
#include <optional>

namespace snow::image_viewer::worker_core {

struct PreparationResult final {
    snow::image::Document document;
    bool previewEquivalentToBase = false;
    snow::image::AlphaContent alphaContent = snow::image::AlphaContent::non_opaque;
    std::chrono::nanoseconds alphaClassificationDuration{0};
};

[[nodiscard]] snow::image::Result<PreparationResult>
prepareForExport(snow::image::Document document, const EditExportSettings& settings,
                 const snow::image::EncoderInfo& encoder, QString* warning,
                 std::stop_token stop = {},
                 std::optional<snow::image::AlphaContent> verifiedAlphaContent = {});

using ArtifactReadyCallback = std::function<void(QJsonObject)>;

[[nodiscard]] QJsonObject executeEncodeJob(const QJsonObject& job,
                                           const EditExportSettings& settings,
                                           const ArtifactReadyCallback& artifactReady,
                                           std::stop_token stop, bool* artifactPublished);

[[nodiscard]] QJsonObject executePreviewJob(const QString& artifactPath, const QString& previewPath,
                                            snow::image::Format format, std::stop_token stop);

} // namespace snow::image_viewer::worker_core
