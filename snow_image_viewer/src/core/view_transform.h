#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

namespace snow::image_viewer {

class ViewTransform final {
  public:
    enum class Mode {
        Fit,
        Manual,
    };

    static constexpr qreal kMinimumZoom = 0.01;
    static constexpr qreal kMaximumZoom = 32.0;

    void setViewportSize(const QSizeF& size);
    void setImageSize(const QSizeF& size);
    void setDevicePixelRatio(qreal ratio);
    void setActualSize();
    void fitToWindow();
    void setZoom(qreal zoom, const QPointF& anchor);
    void zoomBy(qreal factor, const QPointF& anchor);
    void panBy(const QPointF& delta);
    void rotateLeft();
    void rotateRight();

    QSizeF viewportSize() const;
    QSizeF imageSize() const;
    QSizeF orientedImageSize() const;
    Mode mode() const;
    qreal zoom() const;
    qreal minimumZoom() const;
    bool isAtMinimumZoom() const;
    bool isAtMaximumZoom() const;
    QPointF pan() const;
    int quarterTurns() const;
    QRectF displayedImageRect() const;
    QTransform imageToViewportTransform() const;
    QPointF mapViewportToImage(const QPointF& point) const;

  private:
    qreal fitZoom() const;
    void updateFitZoom();
    void clampPan();

    QSizeF viewportSize_;
    QSizeF imageSize_;
    Mode mode_ = Mode::Fit;
    qreal zoom_ = 1.0;
    qreal devicePixelRatio_ = 1.0;
    QPointF pan_;
    int quarterTurns_ = 0;
};

} // namespace snow::image_viewer
