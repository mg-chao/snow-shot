#pragma once

#include "snow_shot/presentation/screenshotsaveasfiledialog.h"
#include "snow_shot/presentation/screenshotexportcoordinator.h"
#include <QTemporaryDir>
#include <QFile>

namespace screenshot_save_export {
class MappedRaster final : public std::enable_shared_from_this<MappedRaster> {
  public:
    ~MappedRaster();
    [[nodiscard]] static std::shared_ptr<MappedRaster> create(QSize size, QString* error);
    [[nodiscard]] ScreenshotImageRowSource rows(std::function<bool()> cancelled = {}) const;
    [[nodiscard]] QImage image() const;
    QSize size;
    uchar* pixels = nullptr;

  private:
    QTemporaryDir m_directory;
    QFile m_file;
};
struct Source {
    ScreenshotImageRowSource rows;
    QImage preview;
};
struct Encoded {
    QTemporaryDir directory;
    QString path;
    ScreenshotSaveExportOptions options;
};
[[nodiscard]] Source prepare(const ScreenshotImageRowSource& rows,
                             const ScreenshotExportCancellation& cancellation, QString* error);
[[nodiscard]] ScreenshotSaveExportOptions normalizedOptions(ScreenshotSaveExportOptions options);
[[nodiscard]] std::shared_ptr<Encoded> render(const Source& source,
                                              const ScreenshotSaveExportOptions& options,
                                              const ScreenshotExportCancellation& cancellation,
                                              QString* error);
[[nodiscard]] QImage decode(const Encoded& encoded,
                            const ScreenshotExportCancellation& cancellation, QString* error);
[[nodiscard]] QSize encoderLimits(ScreenshotImageFileFormat format);
} // namespace screenshot_save_export
