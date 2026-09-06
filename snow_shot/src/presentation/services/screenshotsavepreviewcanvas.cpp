#include "snow_shot/presentation/screenshotsavepreviewcanvas.h"
#include "theme/theme_manager.h"
#include "antd_icons.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <cmath>
#include <algorithm>

ScreenshotSavePreviewCanvas::ScreenshotSavePreviewCanvas(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("savePreviewCanvas"));
    setMinimumSize(180, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_checkerboard = QPixmap(24, 24);
    m_checkerboard.fill(Qt::white);
    QPainter tilePainter(&m_checkerboard);
    tilePainter.fillRect(0, 0, 12, 12, QColor(0, 0, 0, 28));
    tilePainter.fillRect(12, 12, 12, 12, QColor(0, 0, 0, 28));
    m_readout = new QLabel(this);
    m_readout->setObjectName(QStringLiteral("savePreviewZoom"));
    m_readout->setAlignment(Qt::AlignCenter);
    m_readout->setFixedSize(92, 28);
    m_readout->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_readout->setStyleSheet(
        QStringLiteral("color: white; background: rgba(0,0,0,160); border-radius: 4px;"));
    m_busy = new QLabel(this);
    m_busy->setObjectName(QStringLiteral("savePreviewStatus"));
    m_busy->setStyleSheet(QStringLiteral(
        "color: white; background: rgba(0,0,0,160); padding: 4px 8px; border-radius: 4px;"));
    m_busy->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_busy->hide();
    updateReadout();
}
void ScreenshotSavePreviewCanvas::setSource(QImage image, QSize pixels) {
    m_original = std::move(image);
    m_pixels = pixels;
    fitImage();
}
void ScreenshotSavePreviewCanvas::setOutput(QImage image, QSize pixels) {
    m_output = std::move(image);
    m_pixels = pixels;
    if (m_fit)
        fitImage();
    updateReadout();
    update();
}
void ScreenshotSavePreviewCanvas::setSplitRatio(double value) {
    m_split = std::clamp(value, 0.0, 1.0);
    setProperty("splitRatio", m_split);
    update();
}
void ScreenshotSavePreviewCanvas::fitImage() {
    if (m_pixels.isEmpty())
        return;
    m_fit = true;
    m_pan = {};
    m_zoom = std::min({1.0, std::max(1, width() - 40) / double(m_pixels.width()),
                       std::max(1, height() - 40) / double(m_pixels.height())});
    updateReadout();
    update();
}
void ScreenshotSavePreviewCanvas::updateReadout() {
    m_readout->setText(tr("%1%").arg(m_zoom * 100, 0, 'f', m_zoom < 0.1 ? 1 : 0));
    m_readout->move(12, std::max(0, height() - m_readout->height() - 12));
    m_readout->setAccessibleName(tr("Current zoom"));
    m_busy->setText(tr("Rendering preview"));
    m_busy->adjustSize();
    m_busy->move(12, 12);
    setAccessibleName(tr("Export comparison preview"));
    setProperty("zoom", m_zoom);
}
void ScreenshotSavePreviewCanvas::setBusy(bool busy) {
    m_busy->setVisible(busy);
}
void ScreenshotSavePreviewCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QBrush(m_checkerboard));
    if (m_pixels.isEmpty() || m_original.isNull())
        return;
    const QSizeF dimensions(m_pixels.width() * m_zoom, m_pixels.height() * m_zoom);
    const QPointF center = QRectF(rect()).center() + m_pan;
    const QRectF bounds(center.x() - dimensions.width() / 2, center.y() - dimensions.height() / 2,
                        dimensions.width(), dimensions.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 2.0);
    const double split = m_split * width();
    painter.save();
    painter.setClipRect(QRectF(0, 0, split, height()));
    painter.drawImage(bounds, m_original);
    painter.restore();
    painter.save();
    painter.setClipRect(QRectF(split, 0, width() - split, height()));
    painter.drawImage(bounds, m_output.isNull() ? m_original : m_output);
    painter.restore();
    const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
    painter.setPen(QPen(QColor(0, 0, 0, 150), 3));
    painter.drawLine(QPointF(split, 0), QPointF(split, height()));
    painter.setPen(QPen(theme.colorPrimary, 1));
    painter.drawLine(QPointF(split, 0), QPointF(split, height()));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(theme.colorBgElevated);
    const QRectF grip(split - 12, height() / 2.0 - 20, 24, 40);
    painter.drawRoundedRect(grip, 4, 4);
    const auto icon =
        adqt::icons::antd::outlined::Swap(adqt::icons::IconColors::primary(theme.colorPrimary));
    painter.drawPixmap(QPointF(split - 8, height() / 2.0 - 8),
                       adqt::icons::renderIconPixmap(icon, {QSize(16, 16), devicePixelRatioF()}));
}
void ScreenshotSavePreviewCanvas::resizeEvent(QResizeEvent*) {
    if (m_fit)
        fitImage();
    updateReadout();
}
void ScreenshotSavePreviewCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    m_last = event->position();
    m_dragging = true;
    m_splitDragging = std::abs(m_last.x() - width() * m_split) <= 12;
    setCursor(m_splitDragging ? Qt::SplitHCursor : Qt::ClosedHandCursor);
}
void ScreenshotSavePreviewCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) {
        setCursor(std::abs(event->position().x() - width() * m_split) <= 12 ? Qt::SplitHCursor
                                                                            : Qt::OpenHandCursor);
        return;
    }
    if (m_splitDragging)
        setSplitRatio(event->position().x() / std::max(1, width()));
    else {
        m_fit = false;
        m_pan += event->position() - m_last;
        update();
    }
    m_last = event->position();
}
void ScreenshotSavePreviewCanvas::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = false;
    m_splitDragging = false;
    setCursor(Qt::OpenHandCursor);
}
void ScreenshotSavePreviewCanvas::mouseDoubleClickEvent(QMouseEvent*) {
    fitImage();
}
void ScreenshotSavePreviewCanvas::zoomAt(double value, QPointF position) {
    const double zoom = std::clamp(value, 0.001, 8.0);
    const QPointF offset = position - QRectF(rect()).center();
    m_pan = offset - (offset - m_pan) * (zoom / m_zoom);
    m_zoom = zoom;
    m_fit = false;
    updateReadout();
    update();
}
void ScreenshotSavePreviewCanvas::wheelEvent(QWheelEvent* event) {
    const double steps = !event->pixelDelta().isNull() ? event->pixelDelta().y() / 100.0
                                                       : event->angleDelta().y() / 120.0;
    if (steps != 0)
        zoomAt(m_zoom * std::pow(1.15, steps), event->position());
    event->accept();
}
void ScreenshotSavePreviewCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Left)
        setSplitRatio(m_split - 0.02);
    else if (event->key() == Qt::Key_Right)
        setSplitRatio(m_split + 0.02);
    else if (event->key() == Qt::Key_Home)
        fitImage();
    else if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
        zoomAt(m_zoom * 1.15, QRectF(rect()).center());
    else if (event->key() == Qt::Key_Minus)
        zoomAt(m_zoom / 1.15, QRectF(rect()).center());
    else
        QWidget::keyPressEvent(event);
}
void ScreenshotSavePreviewCanvas::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange)
        updateReadout();
    QWidget::changeEvent(event);
}
