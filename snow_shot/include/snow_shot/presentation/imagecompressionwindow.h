// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/presentation/screenshotexportcoordinator.h"
#include <QStringList>
#include "snow_shot/platform/selectedfiles.h"
#include "snow_shot/presentation/screenshotpinnedimagesnapshot.h"

namespace snow_shot::storage {
struct CaptureHistoryRecord;
}
namespace snow_shot::presentation {
class ImageCompressionWindowCoordinator final : public QObject {
  public:
    explicit ImageCompressionWindowCoordinator(
        ScreenshotPinnedImageSnapshotRequest requestPin = {});
    void shutdown();
    void presentFiles(const QStringList& paths);
    void presentSelectedFiles(std::shared_ptr<platform::SelectedFileBackend> backend,
                              platform::SelectedFileTarget target);
    void presentHistory(const storage::CaptureHistoryRecord& record);
    void presentPinnedRecord(const QString& id);
    void presentSnapshot(const ScreenshotPinnedImageSnapshot& snapshot);

  private:
    ScreenshotPinnedImageSnapshotRequest m_requestPin;
    ScreenshotExportQueue m_fileImports{ScreenshotExportCoordinator::shared(), nullptr, 1};
};
} // namespace snow_shot::presentation
