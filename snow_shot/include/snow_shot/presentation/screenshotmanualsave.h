// SPDX-License-Identifier: GPL-3.0-only
#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTMANUALSAVE_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTMANUALSAVE_H

#include "snow_shot/presentation/screenshotimagefileservice.h"
#include "snow_shot/presentation/screenshotexportcoordinator.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/storage/exportfilename.h"
#include <QDir>
#include <QPointer>

class ScreenshotExportArtifact;
class ScreenshotManualSaveService;
class QWidget;

class ScreenshotManualSaveHandle final {
  public:
    ScreenshotManualSaveHandle() = default;
    void cancel() const;
    [[nodiscard]] bool isValid() const;

  private:
    friend class ScreenshotManualSaveService;
    explicit ScreenshotManualSaveHandle(QObject* session) : m_session(session) {}
    QPointer<QObject> m_session;
};

// Owns the native save dialog, destination policy, asynchronous writer and terminal completion.
// A request uses a dedicated artifact: cancellation cancels its work. Destroying lifetime
// suppresses completion; destroying owner cancels the request. A handle does not cancel on copy
// or destruction, so callers can retain it solely when explicit cancellation is needed.
// Completion is queued to lifetime, so it cannot destroy a caller while open() is returning.
class ScreenshotManualSaveService final {
  public:
    using Completion = std::function<void(ScreenshotExportTaskResult)>;
    using Saving = std::function<void()>;
    [[nodiscard]] static ScreenshotManualSaveHandle
    open(QObject* lifetime, QWidget* owner, std::shared_ptr<ScreenshotExportArtifact> source,
         Completion completion, Qt::WindowModality modality = Qt::WindowModal, Saving saving = {});
};

struct ScreenshotManualSaveDefaults final {
    QString initialPath;
    QString selectedFilter;
};

[[nodiscard]] inline ScreenshotManualSaveDefaults
screenshotManualSaveDefaults(const QDateTime& timestamp = QDateTime::currentDateTime(),
                             const snow_shot::storage::ScreenshotExportContext& names = {}) {
    const snow_shot::storage::ScreenshotSettings settings;
    const QString directory = ScreenshotImageFileService::saveDialogDirectory(
        settings.lastManualSaveDirectory(), settings.imageSaveDirectory(), timestamp, names);
    static_cast<void>(QDir().mkpath(directory));
    const auto format = ScreenshotImageFileService::formatForKey(settings.lastManualSaveFormat());
    return {QDir(directory).filePath(snow_shot::storage::composeExportFileName(
                ScreenshotImageFileService::suggestedBaseName(settings.manualSaveFilenameFormat(),
                                                              timestamp, names),
                QStringLiteral(".") + ScreenshotImageFileService::extension(format))),
            ScreenshotImageFileService::dialogFilter(format)};
}

struct ScreenshotManualSaveDestination final {
    QString path;
    ScreenshotImageFileFormat format = ScreenshotImageFileFormat::Png;
};

[[nodiscard]] inline ScreenshotManualSaveDestination
screenshotManualSaveDestination(const QString& path, const QString& selectedFilter) {
    const auto format = ScreenshotImageFileService::formatForDialogSelection(path, selectedFilter);
    static_cast<void>(snow_shot::storage::ScreenshotSettings().setLastManualSaveFormat(
        ScreenshotImageFileService::formatKey(format)));
    return {ScreenshotImageFileService::normalizedPath(path, format), format};
}

#endif
