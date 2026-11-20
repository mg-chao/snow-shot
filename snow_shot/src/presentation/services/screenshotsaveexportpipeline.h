#pragma once

#include "snow_shot/presentation/screenshotsaveasfiledialog.h"
#include "snow_shot/presentation/screenshotexportcoordinator.h"
#include "snowimageqtcodec.h"
#include <QTemporaryDir>
#include <QFile>

namespace screenshot_save_export {
// Retain common resized outputs for repeated encodes, independently of fit-preview dimensions.
inline constexpr qint64 kMaximumPreparedPixelCacheBytes = qint64{64} * 1024 * 1024;
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
    snow::image::AlphaContent alphaContent = snow::image::AlphaContent::opaque;
};
struct PreparedPixels {
    QSize size;
    ScreenshotImageRowSource rows;
    QImage exactImage;
    snow::image::AlphaContent alphaContent = snow::image::AlphaContent::opaque;
};
struct Encoded {
    QTemporaryDir directory;
    QString path;
    qint64 fileBytes = 0;
    ScreenshotSaveExportOptions options;
    std::shared_ptr<PreparedPixels> pixels;
    snow_shot::image_codec::EncodeResult codecResult;
    std::shared_ptr<screenshot_pdf::Payload> pdf;
};
[[nodiscard]] Source prepare(const ScreenshotImageRowSource& rows,
                             const ScreenshotExportCancellation& cancellation, QString* error);
[[nodiscard]] std::shared_ptr<PreparedPixels>
preparePixels(const Source& source, QSize size, const ScreenshotExportCancellation& cancellation,
              QString* error);
[[nodiscard]] ScreenshotSaveExportOptions normalizedOptions(ScreenshotSaveExportOptions options);
// A supplied PNG must match both the prepared pixels and the normalized encoding options.
[[nodiscard]] std::shared_ptr<Encoded>
render(std::shared_ptr<PreparedPixels> pixels, const ScreenshotSaveExportOptions& options,
       const ScreenshotExportCancellation& cancellation, QString* error,
       std::shared_ptr<screenshot_pdf::Payload> cachedPdf = {},
       snow_shot::storage::PreparedPngImage cachedPng = {});
[[nodiscard]] std::shared_ptr<Encoded> render(const Source& source,
                                              const ScreenshotSaveExportOptions& options,
                                              const ScreenshotExportCancellation& cancellation,
                                              QString* error);
[[nodiscard]] QImage decode(const Encoded& encoded,
                            const ScreenshotExportCancellation& cancellation, QString* error);
// Fit previews are bounded to 2048 per axis; viewport regions retain native pixels up to 16MiB.
[[nodiscard]] QImage preview(const Encoded& encoded, const Source& source,
                             const ScreenshotExportCancellation& cancellation, QString* error);
[[nodiscard]] QImage readRegion(const ScreenshotImageRowSource& rows, QRect region,
                                const ScreenshotExportCancellation& cancellation, QString* error);
[[nodiscard]] QImage decodeRegion(const Encoded& encoded, QRect region,
                                  const ScreenshotExportCancellation& cancellation, QString* error);
[[nodiscard]] QSize encoderLimits(ScreenshotImageFileFormat format);
} // namespace screenshot_save_export
