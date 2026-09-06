#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

class QLabel;

class ScreenshotSavePreviewCanvas final : public QWidget {
    Q_OBJECT
  public:
    explicit ScreenshotSavePreviewCanvas(QWidget* parent = nullptr);
    void setSource(QImage image, QSize pixels);
    void setOutput(QImage image, QSize pixels);
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
    void fitImage();
    void setBusy(bool busy);

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void changeEvent(QEvent*) override;

  private:
    void updateReadout();
    void zoomAt(double value, QPointF position);
    QImage m_original;
    QImage m_output;
    QPixmap m_checkerboard;
    QSize m_pixels;
    QPointF m_pan;
    QPointF m_last;
    double m_split = 0.5;
    double m_zoom = 1;
    bool m_dragging = false;
    bool m_splitDragging = false;
    bool m_fit = true;
    QLabel* m_readout = nullptr;
    QLabel* m_busy = nullptr;
};
