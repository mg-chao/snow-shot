#pragma once

#include "snow_shot/presentation/screenshotpdfexport.h"
#include "snow_shot/presentation/screenshotexportcoordinator.h"
#include <QImage>
#include <QWidget>
#include <QTimer>

class CanvasStatusReadout;
namespace adqt::widgets {
class AdButton;
}

class ScreenshotSavePreviewCanvas final : public QWidget {
    Q_OBJECT
  public:
    explicit ScreenshotSavePreviewCanvas(QWidget* parent = nullptr);
    ~ScreenshotSavePreviewCanvas() override;
    using DetailRenderer =
        std::function<QImage(QRect, const ScreenshotExportCancellation&, QString*)>;
    void setSource(QImage image, QSize pixels, DetailRenderer renderer = {});
    void setOutput(QImage image, QSize pixels = {}, DetailRenderer renderer = {});
    [[nodiscard]] const QImage& outputDetailImage() const {
        return m_outputDetail.image;
    }
    [[nodiscard]] QRect outputDetailRegion() const {
        return m_outputDetail.region;
    }
    void setPdfLayout(screenshot_pdf::Layout layout);
    [[nodiscard]] screenshot_pdf::Layout pdfLayout() const {
        return m_pdfLayout;
    }
    [[nodiscard]] const QImage& outputImage() const {
        return m_output;
    }
    void setSplitRatio(double value);
    [[nodiscard]] double splitRatio() const {
        return m_split;
    }
    [[nodiscard]] double zoom() const {
        return m_zoom;
    }
    [[nodiscard]] QPointF pan() const {
        return m_pan;
    }
    void fitImage(bool showReadout = false);
    void setBusy(bool busy);

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void changeEvent(QEvent*) override;

  private:
    struct Detail {
        QImage image;
        QRect region;
    };
    void scheduleDetails();
    void requestDetails();
    [[nodiscard]] QRectF imageBounds() const;
    [[nodiscard]] QRect detailRequest(const QImage& preview, QSize pixels,
                                      const DetailRenderer& renderer, const Detail& detail) const;
    void retranslate();
    void updateReadout(bool showOnChange = false);
    void zoomAt(double value, QPointF position);
    [[nodiscard]] bool splitHandleContains(QPointF position) const;
    screenshot_pdf::Layout m_pdfLayout;
    QImage m_original;
    QImage m_output;
    QSize m_sourcePixels;
    QSize m_outputPixels;
    DetailRenderer m_sourceRenderer;
    DetailRenderer m_outputRenderer;
    Detail m_sourceDetail;
    Detail m_outputDetail;
    QTimer m_detailTimer;
    ScreenshotExportJobHandle m_detailJob;
    quint64 m_detailSerial = 0;
    bool m_rendering = false;
    QPointF m_pan;
    QPointF m_last;
    double m_split = 0.5;
    double m_zoom = 1;
    bool m_dragging = false;
    bool m_splitDragging = false;
    bool m_splitHovered = false;
    bool m_fit = true;
    adqt::widgets::AdButton* m_fitButton = nullptr;
    adqt::widgets::AdButton* m_actualSizeButton = nullptr;
    CanvasStatusReadout* m_readout = nullptr;
    CanvasStatusReadout* m_busy = nullptr;
};
