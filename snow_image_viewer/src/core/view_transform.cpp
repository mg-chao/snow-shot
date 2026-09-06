#include "core/view_transform.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace snow::image_viewer {
namespace {

int normalizedQuarterTurns(int turns) {
    const int value = turns % 4;
    return value < 0 ? value + 4 : value;
}

} // namespace

void ViewTransform::setViewportSize(const QSizeF& size) {
    viewportSize_ = size.expandedTo(QSizeF(0.0, 0.0));
    if (mode_ == Mode::Fit) {
        updateFitZoom();
    } else {
        zoom_ = std::max(zoom_, minimumZoom());
        if (isAtMinimumZoom()) {
            updateFitZoom();
            return;
        }
        clampPan();
    }
}

void ViewTransform::setImageSize(const QSizeF& size) {
    imageSize_ = size.expandedTo(QSizeF(0.0, 0.0));
    quarterTurns_ = 0;
    mode_ = Mode::Fit;
    pan_ = {};
    updateFitZoom();
}

void ViewTransform::setDevicePixelRatio(qreal ratio) {
    const qreal normalizedRatio = std::isfinite(ratio) && ratio > 0.0 ? ratio : 1.0;
    if (std::abs(devicePixelRatio_ - normalizedRatio) < 0.000001) {
        return;
    }

    const qreal oldRatio = devicePixelRatio_;
    devicePixelRatio_ = normalizedRatio;
    pan_ *= oldRatio / devicePixelRatio_;
    if (mode_ == Mode::Fit) {
        updateFitZoom();
    } else {
        zoom_ = std::max(zoom_, minimumZoom());
        if (isAtMinimumZoom()) {
            updateFitZoom();
            return;
        }
        clampPan();
    }
}

void ViewTransform::setActualSize() {
    zoom_ = std::clamp(1.0, minimumZoom(), kMaximumZoom);
    clampPan();
    mode_ = isAtMinimumZoom() ? Mode::Fit : Mode::Manual;
}

void ViewTransform::fitToWindow() {
    mode_ = Mode::Fit;
    updateFitZoom();
}

void ViewTransform::setZoom(qreal zoom, const QPointF& anchor) {
    if (!imageSize_.isValid() || !viewportSize_.isValid()) {
        return;
    }
    const QPointF imagePoint = mapViewportToImage(anchor);
    zoom_ = std::clamp(zoom, minimumZoom(), kMaximumZoom);
    if (isAtMinimumZoom()) {
        mode_ = Mode::Fit;
        pan_ = {};
        return;
    }
    mode_ = Mode::Manual;

    QTransform centered;
    centered.rotate(quarterTurns_ * 90.0);
    const QPointF imageDelta =
        imagePoint - QPointF(imageSize_.width() / 2.0, imageSize_.height() / 2.0);
    const QPointF rotatedDelta = centered.map(imageDelta) * (zoom_ / devicePixelRatio_);
    const QPointF viewportCenter(viewportSize_.width() / 2.0, viewportSize_.height() / 2.0);
    pan_ = anchor - viewportCenter - rotatedDelta;
    clampPan();
}

void ViewTransform::zoomBy(qreal factor, const QPointF& anchor) {
    if (factor > 0.0 && std::isfinite(factor)) {
        setZoom(zoom_ * factor, anchor);
    }
}

void ViewTransform::panBy(const QPointF& delta) {
    if (!imageSize_.isValid()) {
        return;
    }
    if (isAtMinimumZoom()) {
        mode_ = Mode::Fit;
        pan_ = {};
        return;
    }
    mode_ = Mode::Manual;
    pan_ += delta;
    clampPan();
}

void ViewTransform::rotateLeft() {
    quarterTurns_ = normalizedQuarterTurns(quarterTurns_ - 1);
    if (mode_ == Mode::Fit) {
        updateFitZoom();
    } else {
        zoom_ = std::max(zoom_, minimumZoom());
        if (isAtMinimumZoom()) {
            updateFitZoom();
            return;
        }
        clampPan();
    }
}

void ViewTransform::rotateRight() {
    quarterTurns_ = normalizedQuarterTurns(quarterTurns_ + 1);
    if (mode_ == Mode::Fit) {
        updateFitZoom();
    } else {
        zoom_ = std::max(zoom_, minimumZoom());
        if (isAtMinimumZoom()) {
            updateFitZoom();
            return;
        }
        clampPan();
    }
}

QSizeF ViewTransform::viewportSize() const {
    return viewportSize_;
}

QSizeF ViewTransform::imageSize() const {
    return imageSize_;
}

QSizeF ViewTransform::orientedImageSize() const {
    const QSizeF logicalImageSize = imageSize_ / devicePixelRatio_;
    return quarterTurns_ % 2 == 0 ? logicalImageSize
                                  : QSizeF(logicalImageSize.height(), logicalImageSize.width());
}

ViewTransform::Mode ViewTransform::mode() const {
    return mode_;
}

qreal ViewTransform::zoom() const {
    return zoom_;
}

qreal ViewTransform::minimumZoom() const {
    return fitZoom();
}

bool ViewTransform::isAtMinimumZoom() const {
    return zoom_ <= minimumZoom() + 0.000001;
}

bool ViewTransform::isAtMaximumZoom() const {
    return zoom_ >= kMaximumZoom - 0.000001;
}

QPointF ViewTransform::pan() const {
    return pan_;
}

int ViewTransform::quarterTurns() const {
    return quarterTurns_;
}

QRectF ViewTransform::displayedImageRect() const {
    const QSizeF displaySize = orientedImageSize() * zoom_;
    const QPointF center(viewportSize_.width() / 2.0 + pan_.x(),
                         viewportSize_.height() / 2.0 + pan_.y());
    return QRectF(center - QPointF(displaySize.width() / 2.0, displaySize.height() / 2.0),
                  displaySize);
}

QTransform ViewTransform::imageToViewportTransform() const {
    const QPointF viewportCenter(viewportSize_.width() / 2.0 + pan_.x(),
                                 viewportSize_.height() / 2.0 + pan_.y());
    QTransform transform;
    transform.translate(viewportCenter.x(), viewportCenter.y());
    transform.rotate(quarterTurns_ * 90.0);
    transform.scale(zoom_ / devicePixelRatio_, zoom_ / devicePixelRatio_);
    transform.translate(-imageSize_.width() / 2.0, -imageSize_.height() / 2.0);
    return transform;
}

QPointF ViewTransform::mapViewportToImage(const QPointF& point) const {
    bool invertible = false;
    const QTransform inverse = imageToViewportTransform().inverted(&invertible);
    return invertible ? inverse.map(point)
                      : QPointF(imageSize_.width() / 2.0, imageSize_.height() / 2.0);
}

qreal ViewTransform::fitZoom() const {
    if (!imageSize_.isValid() || !viewportSize_.isValid() || imageSize_.isEmpty() ||
        viewportSize_.isEmpty()) {
        return 1.0;
    }
    const QSizeF oriented = orientedImageSize();
    const qreal availableWidth = std::max<qreal>(1.0, viewportSize_.width());
    const qreal availableHeight = std::max<qreal>(1.0, viewportSize_.height());
    return std::clamp(
        std::min({availableWidth / oriented.width(), availableHeight / oriented.height(), 1.0}),
        kMinimumZoom, kMaximumZoom);
}

void ViewTransform::updateFitZoom() {
    zoom_ = fitZoom();
    pan_ = {};
}

void ViewTransform::clampPan() {
    if (!viewportSize_.isValid() || !imageSize_.isValid()) {
        pan_ = {};
        return;
    }
    const QSizeF displaySize = orientedImageSize() * zoom_;
    const qreal maxX = std::max<qreal>(0.0, (displaySize.width() - viewportSize_.width()) / 2.0);
    const qreal maxY = std::max<qreal>(0.0, (displaySize.height() - viewportSize_.height()) / 2.0);
    pan_.setX(std::clamp(pan_.x(), -maxX, maxX));
    pan_.setY(std::clamp(pan_.y(), -maxY, maxY));
}

} // namespace snow::image_viewer
