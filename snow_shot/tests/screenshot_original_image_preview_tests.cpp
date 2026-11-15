#include "snow_shot/presentation/screenshotoriginalimagepreviewwindow.h"
#include "snow_shot/presentation/canvasstatusreadout.h"

#include <QApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QtMath>
#include <QWindow>
#include <QWheelEvent>
#include <QTimer>
#include <QTranslator>
#include <QScopeGuard>

#include <cmath>
#include <array>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include <QtGui/qscreen_platform.h>
#include <qt_windows.h>
#endif

#ifdef Q_OS_MACOS
#include "snow_shot/platform/screenshotnative.h"
#include <QElapsedTimer>
#import <AppKit/AppKit.h>
#endif

class ScreenshotOriginalImagePreviewWindowTestAccess final {
  public:
    static const QImage& raster(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_viewportImage;
    }
    static const QImage& source(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_sourceImage;
    }
    static quint64 generation(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_rasterGeneration;
    }
    static QWidget* aboveSibling(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_aboveSibling;
    }
    static bool beginDrag(ScreenshotOriginalImagePreviewWindow& window, const QPointF& cursor) {
        return window.beginDrag(cursor);
    }
    static void moveDrag(ScreenshotOriginalImagePreviewWindow& window, const QPointF& cursor) {
        window.moveDrag(cursor);
    }
    static void endDrag(ScreenshotOriginalImagePreviewWindow& window) {
        window.endDrag();
    }
    static qreal scalePercent(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_scalePercent;
    }
    static CanvasStatusReadout* scaleLabel(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_scaleLabel;
    }
    static QTimer* scaleLabelTimer(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_scaleLabel->dismissalTimer();
    }
    static bool hasHintSurface(const ScreenshotOriginalImagePreviewWindow& window) {
        return bool(window.m_hintSurface);
    }
    static QRect workArea(const ScreenshotOriginalImagePreviewWindow& window) {
        return window.m_placementWorkArea;
    }
    static void invalidateWorkArea(ScreenshotOriginalImagePreviewWindow& window) {
        window.m_placementWorkArea.translate(1, 0);
    }
    static bool scaleSteps(ScreenshotOriginalImagePreviewWindow& window, int steps,
                           const QPointF& cursor) {
        return window.applyWheelScaleSteps(steps, cursor);
    }
};

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void settle() {
    for (int pass = 0; pass < 4; ++pass)
        QApplication::processEvents(QEventLoop::AllEvents);
}

QImage sourceImage(const QSize& size = QSize(16, 12)) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            row[x] = qRgb(x % 251, y % 251, (x + y) % 251);
    }
    return image;
}

ScreenshotOriginalImagePreviewState stateFor(QWidget* owner = nullptr) {
    ScreenshotOriginalImagePreviewState state;
    state.image = sourceImage();
    state.imageRectInViewport = QRectF(0, 0, 16, 12);
    state.resultRect = QRect(140, 140, 16, 12);
    state.transientOwner = owner;
    return state;
}

void originalImageStateDefaultsDisableFormulaPresentation() {
    const ScreenshotOriginalImagePreviewState state{
        sourceImage(), QRectF(0, 0, 16, 12), QRect(140, 140, 16, 12), nullptr, true, false};
    require(state.pinned && !state.staysOnTop && state.aboveSibling == nullptr && !state.formula &&
                !state.dimmed && !state.background.isValid() && !state.statusColor.isValid() &&
                state.status.isEmpty(),
            "original-image state must default to an undimmed preview without formula status");
}

class ResizeCounter final : public QObject {
  public:
    int count = 0;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize)
            ++count;
        return QObject::eventFilter(watched, event);
    }
};

bool sendMouseEvent(QWidget& widget, QEvent::Type type, const QPointF& globalPosition,
                    Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF localPosition = globalPosition - QPointF(widget.mapToGlobal(QPoint()));
    QMouseEvent event(type, localPosition, globalPosition, button, buttons, Qt::NoModifier);
    event.ignore();
    QApplication::sendEvent(&widget, &event);
    return event.isAccepted();
}

void placementHonorsTheResultMonitorAndPriority() {
    using Preview = ScreenshotOriginalImagePreviewWindow;
    const QRect work(0, 0, 1000, 800);
    const auto check = [](const QRect& result, const QRect& workArea, int gap,
                          const QRect& expected, qreal scale, const char* message) {
        const auto actual = Preview::placement(result, workArea, gap);
        require(actual.geometry == expected && qAbs(actual.scale - scale) < 0.000001, message);
    };
    check(QRect(400, 300, 200, 100), work, 4, QRect(196, 300, 200, 100), 1.0,
          "left placement must have priority and align to the result top");
    check(QRect(20, 300, 200, 100), work, 4, QRect(20, 196, 200, 100), 1.0,
          "above placement must follow insufficient space on the left");
    check(QRect(20, 20, 200, 100), work, 4, QRect(224, 20, 200, 100), 1.0,
          "right placement must follow insufficient space on the left and above");
    check(QRect(20, 20, 200, 100), QRect(0, 0, 230, 120), 4, QRect(20, -84, 200, 100), 1.0,
          "a fit below fifty percent must retain full size in the best direction");
    check(QRect(204, 0, 200, 100), work, 4, QRect(0, 0, 200, 100), 1.0,
          "an exact fit on the work-area edge must be accepted");
    check(QRect(20, 20, 200, 100), QRect(0, 0, 1000, 80), 4, QRect(224, 0, 160, 80), 0.8,
          "automatic fitting must include both dimensions and clamp the vertical position");
    check(QRect(-1200, -200, 200, 100), QRect(-1600, -300, 1600, 900), 4,
          QRect(-1404, -200, 200, 100), 1.0,
          "negative monitor coordinates must retain the same placement priority");
    check(QRect(20, 20, 200, 100), work, -10, QRect(220, 20, 200, 100), 1.0,
          "negative gaps must be normalized to zero");
    check(QRect(400, 300, 500, 400), work, 4, QRect(0, 300, 396, 317), 0.792,
          "the left side must use its greatest fitting scale");
    check(QRect(304, 84, 600, 100), QRect(0, 0, 1000, 1000), 4, QRect(304, 0, 480, 80), 0.8,
          "the best fit must win even when another direction has greater free area");
    check(QRect(20, 20, 400, 200), QRect(0, 0, 624, 400), 4, QRect(424, 20, 200, 100), 0.5,
          "an exact fifty-percent fit must be applied automatically");
    check(QRect(20, 20, 400, 200), QRect(0, 0, 623, 400), 4, QRect(424, 20, 400, 200), 1.0,
          "a fit just below fifty percent must retain full-size overflow on the winning side");
    check(QRect(204, 104, 400, 200), QRect(0, 0, 604, 304), 4, QRect(0, 104, 200, 100), 0.5,
          "equal fitting scales must use left, above, right priority");
    check(QRect(400, 750, 200, 100), work, 4, QRect(196, 700, 200, 100), 1.0,
          "a full-size side fit must clamp the orthogonal position near the bottom edge");
    const QRect leftFitsHalf(304, 84, 600, 100);
    const QRect squareWork(0, 0, 1000, 1000);
    const auto preferredLeft = Preview::placement(leftFitsHalf, squareWork, 4, true);
    require(preferredLeft.geometry == QRect(0, 84, 300, 50) && preferredLeft.scale == 0.5,
            "table placement prefers the left at fifty percent even with more space above");
    const QRect leftFitsLess(303, 84, 600, 100);
    const auto fallback = Preview::placement(leftFitsLess, squareWork, 4, true);
    require(fallback.geometry == Preview::placement(leftFitsLess, squareWork, 4).geometry &&
                fallback.geometry.bottom() < leftFitsLess.top(),
            "table placement uses the existing fallback below fifty percent");
    require(Preview::placement({}, work, 4).geometry.isEmpty(),
            "empty results must not produce a preview");
}

void viewportRasterPreservesClippingAndLetterboxing() {
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect.setSize(QSize(24, 20));
    state.imageRectInViewport = QRectF(4, 3, 16, 12);
    require(preview.present(state), "letterboxed image presentation failed");
    settle();
    const QImage& raster = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    const qreal dpr = preview.devicePixelRatioF();
    const qreal scale = ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry() ? 1.0 : dpr;
    require(raster.size() == QSize(qRound(24 * scale), qRound(20 * scale)) &&
                qFuzzyCompare(raster.devicePixelRatio(), dpr),
            "the raster must have viewport dimensions and the preview's actual output DPR");
    require(raster.pixelColor(0, 0).alpha() == 0 &&
                raster.pixelColor(raster.width() - 1, raster.height() - 1).alpha() == 0,
            "letterbox pixels must remain transparent");
    require(raster.pixelColor(qRound(6 * scale), qRound(5 * scale)).alpha() == 255,
            "image pixels must remain visible within the fitted image rectangle");
    require(ScreenshotOriginalImagePreviewWindowTestAccess::source(preview).constBits() ==
                state.image.constBits(),
            "the preview must retain a shared source image without copying its pixels");

    state.imageRectInViewport = QRectF(-4, -3, 16, 12);
    require(preview.present(state), "panned image presentation failed");
    settle();
    const QImage& panned = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    require(panned.pixelColor(0, 0).alpha() == 255 &&
                panned.pixelColor(qRound(15 * scale), qRound(15 * scale)).alpha() == 0,
            "panned images must clip to the viewport rather than fit the source anew");
    if (qFuzzyCompare(scale, qreal(1)))
        require(panned.pixel(0, 0) == state.image.pixel(4, 3),
                "panning must preserve the source-to-viewport pixel correspondence");
}

void largeSourceCoordinatesRetainVisiblePixels() {
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.image = sourceImage(QSize(40000, 2));
    state.resultRect.setSize(QSize(160, 40));
    state.imageRectInViewport = QRectF(-35000, 10, 40000, 2);
    require(preview.present(state), "large-source preview failed");
    settle();
    const QImage& raster = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    const qreal scale = ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry()
                            ? 1.0
                            : preview.devicePixelRatioF();
    require(raster.pixelColor(qRound(20 * scale), qRound(10 * scale)).alpha() == 255,
            "visible image slices beyond the raster coordinate limit must not disappear");
    if (qFuzzyCompare(scale, qreal(1))) {
        require(raster.pixel(20, 10) == state.image.pixel(35020, 0) &&
                    raster.pixel(100, 11) == state.image.pixel(35100, 1),
                "large-source clipping must retain the original pixels");
    }
    require(ScreenshotOriginalImagePreviewWindowTestAccess::source(preview).constBits() ==
                state.image.constBits(),
            "large-source slicing must keep source storage shared");
}

void sourceDprMetadataDoesNotAlterViewportMapping() {
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    require(preview.present(state), "untagged source preview failed");
    settle();
    const QImage expected = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    state.image.setDevicePixelRatio(2);
    require(preview.present(state), "DPR-tagged source preview failed");
    settle();
    require(ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview) == expected,
            "source DPR metadata must not scale the explicit source-to-viewport mapping");
}

void movementReusesTheRasterAndContentChangesInvalidateIt() {
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    require(preview.present(state), "initial cached preview failed");
    settle();
    const quint64 firstGeneration =
        ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
    const uchar* pixels =
        ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).constBits();
    for (int iteration = 0; iteration < 30; ++iteration) {
        state.resultRect.translate(1, 0);
        require(preview.present(state), "moving cached preview failed");
    }
    settle();
    require(
        ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) == firstGeneration &&
            ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).constBits() == pixels,
        "position changes must retain the same viewport raster");
    preview.hide();
    require(preview.present(state), "restoring cached preview failed");
    settle();
    require(ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) == firstGeneration,
            "hide and restore must reuse the existing viewport raster");
    QEvent backingChange(QEvent::DevicePixelRatioChange);
    QApplication::sendEvent(&preview, &backingChange);
    preview.hide();
    settle();
    require(!preview.isVisible() && ScreenshotOriginalImagePreviewWindowTestAccess::generation(
                                        preview) == firstGeneration,
            "a queued backing update must not rebuild or resurrect a drag-hidden preview");
    require(preview.present(state), "restoring after a queued backing update failed");
    state.imageRectInViewport.translate(1, 0);
    require(preview.present(state), "changing the viewport mapping failed");
    require(ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                firstGeneration + 1,
            "panning must invalidate the raster exactly once");
    state.resultRect.setSize(QSize(18, 14));
    require(preview.present(state), "resizing the preview failed");
    require(ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                firstGeneration + 2,
            "viewport size changes must invalidate the raster");
    state.image.setPixelColor(1, 1, Qt::red);
    require(preview.present(state), "changing source pixels failed");
    require(ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                firstGeneration + 3,
            "source revision changes must invalidate the raster");
}

void formulaStateSurvivesQueuedBackingRefreshes() {
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect.setSize(QSize(160, 100));
    state.image = QImage(24, 20, QImage::Format_ARGB32_Premultiplied);
    state.image.fill(QColor(210, 30, 40));
    state.imageRectInViewport = QRectF(16, 16, 24, 20);
    state.formula = true;
    state.dimmed = true;
    state.background = QColor(20, 50, 90);
    state.statusColor = QColor(245, 235, 220);
    state.status = QStringLiteral("Invalid formula source");
    require(preview.present(state), "presenting a dimmed formula preview failed");
    settle();
    const QImage initial = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    const quint64 generation = ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
    const qreal scale = ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry()
                            ? 1.0
                            : preview.devicePixelRatioF();
    require(initial.pixelColor(0, 0) == state.background &&
                initial.pixelColor(qRound(20 * scale), qRound(20 * scale)) !=
                    state.image.pixelColor(4, 4) &&
                preview.accessibleDescription() == state.status,
            "formula errors must retain an opaque themed background and dim the prior image");
    for (const QEvent::Type eventType :
         {QEvent::DevicePixelRatioChange, QEvent::ScreenChangeInternal, QEvent::WinIdChange}) {
        QEvent backingChange(eventType);
        QApplication::sendEvent(&preview, &backingChange);
        settle();
        require(preview.isVisible() && preview.accessibleDescription() == state.status &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview) == initial &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                        generation,
                "queued backing refreshes must preserve formula pixels, error status and cache");
    }
    state.background = QColor(90, 35, 20);
    state.statusColor = QColor(225, 245, 235);
    state.status = QStringLiteral("Renderer unavailable");
    require(preview.present(state), "changing the formula preview theme failed");
    settle();
    require(ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).pixelColor(0, 0) ==
                    state.background &&
                preview.accessibleDescription() == state.status &&
                ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                    generation + 1,
            "formula theme and status changes must invalidate the raster once");
}

void formulaPlaceholdersRetainNullImagesAndExactSmallExtents() {
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.image = {};
    state.resultRect.setSize(QSize(97, 65));
    state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
    state.formula = true;
    state.background = QColor(35, 75, 115);
    state.statusColor = QColor(245, 245, 245);
    state.status = QStringLiteral("Rendering formula...");
    require(preview.present(state), "presenting an image-free formula placeholder failed");
    settle();
    const QImage initial = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    const quint64 generation = ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
    require(!initial.isNull() && initial.pixelColor(0, 0) == state.background &&
                ScreenshotOriginalImagePreviewWindowTestAccess::source(preview).isNull() &&
                preview.accessibleDescription() == state.status,
            "formula loading must remain visible without a rendered source image");
    for (const QEvent::Type eventType :
         {QEvent::DevicePixelRatioChange, QEvent::ScreenChangeInternal}) {
        QEvent backingChange(eventType);
        QApplication::sendEvent(&preview, &backingChange);
        settle();
        require(preview.isVisible() && preview.accessibleDescription() == state.status &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview) == initial &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                        generation,
                "queued backing refreshes must preserve image-free formula placeholders");
    }
    state.resultRect.setSize(QSize(3, 2));
    state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
    state.status.clear();
    require(preview.present(state), "presenting a tiny formula placeholder failed");
    settle();
    const QImage& tiny = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    const qreal scale = ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry()
                            ? 1.0
                            : preview.devicePixelRatioF();
    require(tiny.size() == QSize(qRound(3 * scale), qRound(2 * scale)) &&
                tiny.pixelColor(0, 0) == state.background && preview.size() == QSize(3, 2),
            "placeholder rendering must retain exact tiny viewport dimensions");
}

void formulaErrorsFitTheCompleteImageAboveTheFooter() {
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect.setSize(QSize(200, 100));
    state.image = QImage(120, 80, QImage::Format_ARGB32_Premultiplied);
    state.image.fill(QColor(180, 180, 180));
    {
        QPainter painter(&state.image);
        painter.fillRect(QRect(0, 0, 12, 12), Qt::red);
        painter.fillRect(QRect(108, 0, 12, 12), Qt::green);
        painter.fillRect(QRect(0, 68, 12, 12), Qt::blue);
        painter.fillRect(QRect(108, 68, 12, 12), Qt::yellow);
    }
    state.imageRectInViewport = QRectF(40, 10, 120, 80);
    state.formula = true;
    state.background = QColor(20, 20, 20);
    state.statusColor = QColor(220, 220, 220);
    require(preview.present(state), "presenting the formula marker fixture failed");
    settle();
    const QImage valid = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    const qreal scale = ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry()
                            ? 1.0
                            : preview.devicePixelRatioF();
    require(valid.pixelColor(qRound(46 * scale), qRound(16 * scale)) == QColor(Qt::red) &&
                valid.pixelColor(qRound(154 * scale), qRound(84 * scale)) == QColor(Qt::yellow),
            "a valid formula retains its supplied source mapping");
    state.dimmed = true;
    state.status = QStringLiteral("Invalid formula source");
    require(preview.present(state), "presenting the retained formula and error footer failed");
    settle();
    const QImage error = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    std::array<int, 4> markerPixels{};
    QRect markerBounds;
    int brightestMarker = 0;
    for (int y = 0; y < error.height(); ++y) {
        for (int x = 0; x < error.width(); ++x) {
            const QColor color = error.pixelColor(x, y);
            int marker = -1;
            if (color.red() > color.green() + 35 && color.red() > color.blue() + 35)
                marker = 0;
            else if (color.green() > color.red() + 35 && color.green() > color.blue() + 35)
                marker = 1;
            else if (color.blue() > color.red() + 35 && color.blue() > color.green() + 35)
                marker = 2;
            else if (color.red() > color.blue() + 35 && color.green() > color.blue() + 35)
                marker = 3;
            if (marker >= 0) {
                ++markerPixels[static_cast<size_t>(marker)];
                markerBounds |= QRect(x, y, 1, 1);
                brightestMarker =
                    std::max({brightestMarker, color.red(), color.green(), color.blue()});
            }
        }
    }
    require(std::all_of(markerPixels.begin(), markerPixels.end(),
                        [](int count) { return count > 4; }) &&
                markerBounds.bottom() < error.height() * 0.6 &&
                std::abs(qreal(markerBounds.width()) / markerBounds.height() - 1.5) < 0.1 &&
                brightestMarker < 180 && preview.accessibleDescription() == state.status,
            "an error footer preserves all dimmed formula corners and their complete aspect ratio");
    const quint64 generation = ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
    state.resultRect.translate(20, 10);
    require(preview.present(state), "moving the formula marker fixture failed");
    require(ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) == generation &&
                ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview) == error,
            "moving a formula footer must reuse its composed raster");
    state.resultRect.setSize(QSize(31, 13));
    state.image = QImage(31, 13, QImage::Format_ARGB32_Premultiplied);
    state.image.fill(QColor(240, 80, 40));
    state.imageRectInViewport = QRectF(0, 0, 31, 13);
    state.statusColor = Qt::blue;
    require(preview.present(state), "presenting a tiny retained formula failed");
    const QImage tiny = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
    bool formulaVisible = false;
    for (int y = 0; y < tiny.height(); ++y) {
        for (int x = 0; x < tiny.width(); ++x) {
            const QColor color = tiny.pixelColor(x, y);
            require(color.blue() <= color.red(),
                    "a tiny formula viewport must omit its unreadable visual footer");
            formulaVisible = formulaVisible || color.red() > color.blue() + 35;
        }
    }
    require(
        formulaVisible && tiny.size() == QSize(qRound(31 * scale), qRound(13 * scale)) &&
            preview.accessibleDescription() == state.status,
        "tiny selections contract padding while retaining formula pixels and accessible errors");
}

bool sendWheelEvent(QWidget& widget, const QPointF& position, const QPoint& angleDelta,
                    const QPoint& pixelDelta = {}, Qt::ScrollPhase phase = Qt::NoScrollPhase,
                    quint64 timestamp = 0) {
    QWheelEvent event(position, QPointF(widget.mapToGlobal(position.toPoint())), pixelDelta,
                      angleDelta, Qt::NoButton, Qt::NoModifier, phase, false);
    event.setTimestamp(timestamp);
    event.ignore();
    QApplication::sendEvent(&widget, &event);
    return event.isAccepted();
}

void wheelZoomUsesPinnedLevelsAndShowsAnEphemeralHint() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 100, 80);
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(preview.present(state), "preparing the wheel input fixture failed");
    settle();
    const QPoint topLeft = preview.geometry().topLeft();
    const QPointF position(25, 20);
    require(sendWheelEvent(preview, position, QPoint(0, 120)) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(110)) &&
                preview.size() == QSize(110, 88) && preview.geometry().topLeft() == topLeft,
            "one wheel notch must resize the preview to the next ten-percent level");
    require(sendWheelEvent(preview, position, QPoint(0, 60)) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)),
            "the first partial wheel delta after a complete notch must react immediately");
    require(sendWheelEvent(preview, position, QPoint(0, 60)) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)),
            "remaining partial angle movement must accumulate without duplicating a step");
    require(sendWheelEvent(preview, position, QPoint(0, -240)) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(100)),
            "multiple wheel notches must apply every ten-percent step");
    require(sendWheelEvent(preview, position, QPoint(), QPoint(0, 1)) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(110)) &&
                sendWheelEvent(preview, position, QPoint(), QPoint(0, -1)) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(100)),
            "vertical pixel deltas must zoom one level in either direction");
    sendWheelEvent(preview, position, QPoint(120, 0));
    require(qFuzzyCompare(Access::scalePercent(preview), qreal(100)),
            "horizontal-only wheel input must not change the preview scale");
    auto* label = Access::scaleLabel(preview);
    auto* timer = Access::scaleLabelTimer(preview);
    require(label && timer && label->isVisible() && timer->isActive() &&
                label->accessibleName() == QStringLiteral("Scale: 100%") &&
                preview.rect().contains(label->geometry()) && label->geometry().left() <= 8 &&
                preview.height() - label->geometry().bottom() <= 9 &&
                label->testAttribute(Qt::WA_TransparentForMouseEvents),
            "zooming must show a passive percentage hint inside the bottom-left corner");
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    preview.repaint();
    settle();
    require(Access::generation(preview) == generation &&
                Access::raster(preview).constBits() == pixels,
            "drawing the zoom hint must reuse the cached preview raster");
    timer->stop();
    require(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection),
            "dispatching the deterministic zoom hint timeout failed");
    require(label->isHidden() && Access::generation(preview) == generation &&
                Access::raster(preview).constBits() == pixels,
            "the percentage hint timeout must hide the hint without rebuilding preview pixels");
    require(Access::scaleSteps(preview, 100, QPointF(preview.geometry().center())) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(500)),
            "manual zoom must clamp at five hundred percent");
    require(Access::scaleSteps(preview, -100, QPointF(preview.geometry().center())) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(10)),
            "manual zoom must clamp at ten percent");
}

void visibleScaleHintRetranslatesWithoutChangingZoom() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    class ScaleHintTranslator final : public QTranslator {
      public:
        bool isEmpty() const override {
            return false;
        }
        QString translate(const char* context, const char* source, const char*,
                          int) const override {
            if (qstrcmp(context, "CanvasStatusReadout") == 0 && qstrcmp(source, "Scale: %1%") == 0)
                return QStringLiteral("Translated scale: %1%");
            return {};
        }
    } translator;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 100, 80);
    require(preview.present(state) &&
                Access::scaleSteps(preview, 1, QPointF(preview.geometry().center())),
            "preparing the live language zoom hint fixture failed");
    const QRect geometry = preview.geometry();
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    require(QCoreApplication::installTranslator(&translator),
            "installing the zoom hint translator failed");
    const auto restore = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
    settle();
    require(
        Access::scaleLabel(preview)->isVisible() &&
            Access::scaleLabel(preview)->accessibleName() ==
                QStringLiteral("Translated scale: 110%") &&
            preview.geometry() == geometry && Access::generation(preview) == generation &&
            Access::raster(preview).constBits() == pixels,
        "language changes must retranslate a visible scale hint while preserving zoom and pixels");
    QCoreApplication::removeTranslator(&translator);
    settle();
    require(Access::scaleLabel(preview)->accessibleName() == QStringLiteral("Scale: 110%"),
            "removing the translator must restore the source-language hint");
}

void wheelGesturesRespectBoundariesAndIgnoreMomentum() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 100, 80);
    require(preview.present(state), "preparing the wheel gesture fixture failed");
    const QPointF position(25, 20);
    require(sendWheelEvent(preview, position, QPoint(0, 60), {}, Qt::ScrollBegin) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(110)),
            "a gesture's first partial wheel delta must react immediately");
    require(sendWheelEvent(preview, position, QPoint(0, 60), {}, Qt::ScrollUpdate) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(110)) &&
                sendWheelEvent(preview, position, QPoint(0, 60), {}, Qt::ScrollUpdate) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)),
            "a wheel gesture must retain the coalesced partial angle movement");
    require(sendWheelEvent(preview, position, QPoint(0, 120), {}, Qt::ScrollMomentum) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)) &&
                sendWheelEvent(preview, position, QPoint(0, 120), {}, Qt::ScrollEnd) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)),
            "momentum and scroll-end deltas must be consumed without changing scale");
    require(sendWheelEvent(preview, position, {}, QPoint(0, 1), Qt::ScrollBegin) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(130)) &&
                sendWheelEvent(preview, position, {}, QPoint(0, 50), Qt::ScrollUpdate) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(130)),
            "precise wheel gestures must react immediately and retain their pixel remainder");
    sendWheelEvent(preview, position, {}, {}, Qt::ScrollEnd);
    require(sendWheelEvent(preview, position, QPoint(0, 1), {}, Qt::NoScrollPhase, 100) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(140)) &&
                sendWheelEvent(preview, position, QPoint(0, 1), {}, Qt::NoScrollPhase, 400) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(150)),
            "separate wheel bursts must each respond to their first partial delta");
}

void scaleHintFontChangesPreservePixelsAndDeadline() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 320, 240);
    require(preview.present(state) &&
                Access::scaleSteps(preview, 1, QPointF(preview.geometry().center())),
            "preparing the font-change hint fixture failed");
    auto* label = Access::scaleLabel(preview);
    const auto hint = label->captureState();
    const QSize size = label->size();
    const QRect geometry = preview.geometry();
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    QFont larger = label->font();
    larger.setPixelSize(label->fontMetrics().height() + 8);
    label->setFont(larger);
    settle();
    require(label->isVisible() && label->height() > size.height() &&
                label->captureState().deadline == hint.deadline && preview.geometry() == geometry &&
                Access::generation(preview) == generation &&
                Access::raster(preview).constBits() == pixels,
            "hint font updates must relayout without changing zoom, pixels, or dismissal time");
}

void automaticScaleAdvancesToPinnedWheelLevels() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    QScreen* screen = QGuiApplication::primaryScreen();
    require(screen != nullptr, "automatic scale fixture requires a screen");
    const QRect work = screen->availableGeometry();
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect =
        QRect(work.topLeft() + QPoint(4, 4),
              QSize(qRound((work.width() - 8) / 1.83), std::min(200, work.height() - 8)));
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(preview.present(state), "preparing the arbitrary auto-fit scale failed");
    const qreal automaticPercent = Access::scalePercent(preview);
    require(automaticPercent >= 50 && automaticPercent < 100 && work.contains(preview.geometry()),
            "automatic fitting must use the available right-side space");
    const int displayed = qRound(automaticPercent);
    const int nextUp = (displayed / 10 + 1) * 10;
    require(Access::scaleSteps(preview, 1, QPointF(preview.geometry().center())) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(nextUp)),
            "an arbitrary automatic scale must advance to the next pinned zoom level");
    preview.clear();
    require(preview.present(state), "restoring the arbitrary auto-fit scale failed");
    const int nextDown = ((displayed + 9) / 10 - 1) * 10;
    require(Access::scaleSteps(preview, -1, QPointF(preview.geometry().center())) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(nextDown)),
            "an arbitrary automatic scale must decrease to the preceding pinned zoom level");
}

void wheelZoomUsesConfiguredAnchors() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    for (const QString& mode : {QStringLiteral("top_left"), QStringLiteral("top_right"),
                                QStringLiteral("bottom_left"), QStringLiteral("bottom_right"),
                                QStringLiteral("center"), QStringLiteral("mouse_position")}) {
        ScreenshotOriginalImagePreviewWindow preview;
        auto state = stateFor();
        state.resultRect = QRect(300, 180, 100, 80);
        state.mouseWheelZoomMode = mode;
        require(preview.present(state), "preparing the configured zoom anchor failed");
        const QRect initial = preview.geometry();
        const QPointF cursor(initial.topLeft() + QPoint(25, 20));
        require(Access::scaleSteps(preview, 2, cursor) && preview.size() == QSize(120, 96),
                "configured anchor zoom failed");
        if (mode == QStringLiteral("top_left")) {
            require(preview.geometry().topLeft() == initial.topLeft(),
                    "top-left zoom must preserve the window's top-left corner");
        } else if (mode == QStringLiteral("top_right")) {
            require(preview.geometry().topRight() == initial.topRight(),
                    "top-right zoom must preserve the window's top-right corner");
        } else if (mode == QStringLiteral("bottom_left")) {
            require(preview.geometry().bottomLeft() == initial.bottomLeft(),
                    "bottom-left zoom must preserve the window's bottom-left corner");
        } else if (mode == QStringLiteral("bottom_right")) {
            require(preview.geometry().bottomRight() == initial.bottomRight(),
                    "bottom-right zoom must preserve the window's bottom-right corner");
        } else if (mode == QStringLiteral("center")) {
            require(preview.geometry().topLeft() == initial.topLeft() - QPoint(10, 8),
                    "center zoom must preserve the window's center");
        } else {
            require(preview.geometry().topLeft() == initial.topLeft() - QPoint(5, 4),
                    "mouse-position zoom must preserve the source point below the cursor");
        }
    }
}

void zoomScalesTheViewportMappingAndFormulaPlaceholders() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 80, 60);
    state.image.fill(QColor(210, 30, 40));
    state.imageRectInViewport = QRectF(4, 3, 16, 12);
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(preview.present(state) &&
                Access::scaleSteps(preview, 5, QPointF(preview.geometry().center())),
            "zooming the mapped original image failed");
    const qreal dpr = preview.devicePixelRatioF();
    const QImage& raster = Access::raster(preview);
    require(preview.size() == QSize(120, 90) &&
                raster.size() == QSize(qRound(120 * dpr), qRound(90 * dpr)) &&
                raster.pixelColor(qRound(10 * dpr), qRound(10 * dpr)) == QColor(210, 30, 40) &&
                raster.pixelColor(qRound(2 * dpr), qRound(2 * dpr)).alpha() == 0 &&
                raster.pixelColor(qRound(35 * dpr), qRound(10 * dpr)).alpha() == 0,
            "zoom must resize the complete clipped and letterboxed viewport mapping");
    const quint64 generation = Access::generation(preview);
    require(preview.present(state) && preview.size() == QSize(120, 90) &&
                Access::generation(preview) == generation,
            "content refresh must not compound the stored source mapping or zoom");
    state.formula = true;
    state.image = {};
    state.background = QColor(35, 75, 115);
    state.statusColor = Qt::white;
    state.status = QStringLiteral("Rendering formula...");
    require(preview.present(state) && preview.size() == QSize(120, 90) &&
                Access::source(preview).isNull() &&
                Access::raster(preview).pixelColor(0, 0) == state.background &&
                preview.accessibleDescription() == state.status,
            "zoomed formula placeholders must remain visible without a source image");
    state.image = sourceImage();
    state.dimmed = true;
    state.status = QStringLiteral("Invalid formula source");
    require(preview.present(state) && preview.size() == QSize(120, 90) &&
                preview.accessibleDescription() == state.status &&
                Access::source(preview).constBits() == state.image.constBits(),
            "zoomed formula errors must retain the source and status footer");
}

void rejectedZoomPreservesGeometryRasterAndScale() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(0, 0, 1000, 1000);
    state.formula = true;
    state.image = {};
    state.imageRectInViewport = QRectF(0, 0, 1000, 1000);
    state.background = QColor(35, 75, 115);
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(preview.present(state) &&
                Access::scaleSteps(preview, -1, QPointF(preview.geometry().center())),
            "preparing the formula allocation limit fixture and prior zoom hint failed");
    const QRect geometry = preview.geometry();
    const qreal scale = Access::scalePercent(preview);
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    const auto hint = Access::scaleLabel(preview)->captureState();
    require(
        !Access::scaleSteps(preview, 100, QPointF(geometry.center())) && preview.isVisible() &&
            preview.geometry() == geometry && qFuzzyCompare(Access::scalePercent(preview), scale) &&
            Access::generation(preview) == generation &&
            Access::raster(preview).constBits() == pixels &&
            Access::scaleLabel(preview)->isVisible() &&
            Access::scaleLabelTimer(preview)->isActive() &&
            Access::scaleLabel(preview)->accessibleName() == QStringLiteral("Scale: 90%") &&
            preview.present(state) && preview.geometry() == geometry &&
            qFuzzyCompare(Access::scalePercent(preview), scale),
        "a zoom exceeding the raster limit must preserve the visible geometry and cached state");
    require(Access::scaleLabel(preview)->captureState().deadline == hint.deadline,
            "a rejected zoom must preserve the hint's original dismissal deadline");
}

void rejectedAutomaticPresentationPreservesThePriorValidPreview() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 100, 80);
    state.formula = true;
    state.background = QColor(35, 75, 115);
    require(preview.present(state), "preparing the automatic presentation rollback fixture failed");
    const QRect geometry = preview.geometry();
    const qreal scale = Access::scalePercent(preview);
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    const uchar* const sourcePixels = Access::source(preview).constBits();
    auto oversized = state;
    oversized.resultRect.setSize(QSize(20000, 20000));
    oversized.imageRectInViewport = QRectF(0, 0, 20000, 20000);
    oversized.image.fill(Qt::blue);
    oversized.status = QStringLiteral("Replacement status");
    require(
        preview.present(oversized) && preview.isVisible() && preview.geometry() == geometry &&
            qFuzzyCompare(Access::scalePercent(preview), scale) &&
            Access::generation(preview) == generation &&
            Access::raster(preview).constBits() == pixels &&
            Access::source(preview).constBits() == sourcePixels &&
            preview.accessibleDescription().isEmpty(),
        "a failed automatic raster must preserve the prior geometry, source, pixels and status");
    require(preview.present(state) && preview.geometry() == geometry &&
                Access::generation(preview) == generation,
            "a rejected replacement must leave the prior presentation context reusable");
}

QScreen* mixedDpiScreen(const QString& name, qreal dpr) {
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == name) {
            require(qFuzzyCompare(screen->devicePixelRatio(), dpr),
                    "the mixed-DPI fixture must expose the requested output DPR");
            return screen;
        }
    }
    throw std::runtime_error("the mixed-DPI fixture screen is missing");
}

void mixedDpiPresentationUsesTheDestinationRasterLimit() {
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    QScreen* const lowDpi = mixedDpiScreen(QStringLiteral("external"), 1);
    QScreen* const highDpi = mixedDpiScreen(QStringLiteral("retina"), 2);
    QWidget owner;
    owner.setScreen(lowDpi);
    owner.setGeometry(lowDpi->geometry().x() + 1000, 100, 100, 80);
    owner.show();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    state.resultRect = owner.geometry();
    state.formula = true;
    state.background = QColor(35, 75, 115);
    require(preview.present(state), "preparing the owned mixed-DPI preview failed");
    settle();
    const QPointF cursor(preview.geometry().center());
    require(Access::beginDrag(preview, cursor), "starting the mixed-DPI preview drag failed");
    Access::moveDrag(preview, QPointF(highDpi->geometry().center()));
    Access::endDrag(preview);
    settle();
    require(preview.screen() == highDpi && qFuzzyCompare(preview.devicePixelRatioF(), qreal(2)),
            "dragging must move the owned preview onto the high-DPI display");
    const quint64 generation = Access::generation(preview);
    state.resultRect = QRect(lowDpi->geometry().x() + 20, 1200, 3800, 1100);
    state.image = sourceImage(QSize(20, 18));
    state.imageRectInViewport = QRectF(0, 0, 3800, 1100);
    const QRect target = ScreenshotOriginalImagePreviewWindow::placement(
                             state.resultRect, lowDpi->availableGeometry(), 4)
                             .geometry;
    require(target.size() == QSize(3800, 1100),
            "the mixed-DPI regression must retain the complete result viewport");
    require(preview.present(state), "presenting the valid low-DPI replacement failed");
    settle();
    require(
        preview.isVisible() && preview.geometry() == target && preview.screen() == lowDpi &&
            qFuzzyCompare(preview.devicePixelRatioF(), qreal(1)) &&
            Access::raster(preview).size() == target.size() &&
            qFuzzyCompare(Access::raster(preview).devicePixelRatio(), qreal(1)) &&
            Access::source(preview).constBits() == state.image.constBits() &&
            Access::generation(preview) == generation + 1,
        "a valid 1x replacement must use its destination DPR after a preview was dragged to 2x");
}

void mixedDpiRejectedPresentationPreservesTheWindowAndCache() {
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    QScreen* const lowDpi = mixedDpiScreen(QStringLiteral("external"), 1);
    QScreen* const highDpi = mixedDpiScreen(QStringLiteral("retina"), 2);
    QWidget owner;
    owner.setScreen(lowDpi);
    owner.setGeometry(lowDpi->geometry().x() + 1000, 100, 100, 80);
    owner.show();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    state.resultRect = owner.geometry();
    state.formula = true;
    state.background = QColor(35, 75, 115);
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(preview.present(state) &&
                Access::scaleSteps(preview, -1, QPointF(preview.geometry().center())),
            "preparing the low-DPI rollback fixture and zoom hint failed");
    settle();
    const QRect geometry = preview.geometry();
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    const uchar* const sourcePixels = Access::source(preview).constBits();
    ResizeCounter resizeCounter;
    preview.installEventFilter(&resizeCounter);
    int screenChanges = 0;
    const QMetaObject::Connection connection =
        QObject::connect(preview.windowHandle(), &QWindow::screenChanged, &preview,
                         [&screenChanges] { ++screenChanges; });
    const auto disconnect = qScopeGuard([connection] { QObject::disconnect(connection); });
    auto oversized = state;
    // A full-size fallback can overflow a display; this target overlaps only the 2x screen.
    oversized.resultRect = QRect(-3000, 600, 3800, 1100);
    oversized.image = sourceImage(QSize(20, 18));
    oversized.imageRectInViewport = QRectF(0, 0, 3800, 1100);
    oversized.background = QColor(180, 30, 40);
    oversized.status = QStringLiteral("Replacement status");
    const QRect target = ScreenshotOriginalImagePreviewWindow::placement(
                             oversized.resultRect, highDpi->availableGeometry(), 4)
                             .geometry;
    require(target.size() == QSize(3800, 1100) &&
                !target.intersected(highDpi->geometry()).isEmpty() &&
                target.intersected(lowDpi->geometry()).isEmpty(),
            "the rejected mixed-DPI target must select the 2x display and exceed its raster limit");
    require(preview.present(oversized), "rejecting a replacement must retain the valid preview");
    settle();
    require(preview.isVisible() && preview.geometry() == geometry && preview.screen() == lowDpi &&
                qFuzzyCompare(preview.devicePixelRatioF(), qreal(1)) &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(90)) &&
                Access::generation(preview) == generation &&
                Access::raster(preview).constBits() == pixels &&
                Access::source(preview).constBits() == sourcePixels &&
                preview.accessibleDescription().isEmpty() &&
                Access::scaleLabel(preview)->isVisible() &&
                Access::scaleLabelTimer(preview)->isActive() && resizeCounter.count == 0 &&
                screenChanges == 0,
            "an over-limit 2x replacement must be rejected before changing the valid 1x window");
    require(preview.present(state) && preview.geometry() == geometry &&
                Access::generation(preview) == generation,
            "a rejected cross-DPI replacement must preserve the reusable presentation context");
}

void mixedDpiWheelZoomPreparesOneRasterForTheDestination() {
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    QScreen* const lowDpi = mixedDpiScreen(QStringLiteral("external"), 1);
    QScreen* const highDpi = mixedDpiScreen(QStringLiteral("retina"), 2);
    QWidget owner;
    owner.setScreen(highDpi);
    owner.setGeometry(50, 50, 100, 80);
    owner.show();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    // Both logical and native geometry centers stay on the intended fixture display.
    state.resultRect = QRect(0, 0, 1000, 333);
    state.imageRectInViewport = QRectF(0, 0, 1000, 333);
    state.formula = true;
    state.background = QColor(35, 75, 115);
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(preview.present(state), "preparing the high-DPI wheel fixture failed");
    settle();
    const QPointF cursor(preview.geometry().center());
    require(Access::beginDrag(preview, cursor), "starting the wheel fixture placement drag failed");
    Access::moveDrag(preview, cursor + QPointF(QPoint(200, 0) - preview.geometry().topLeft()));
    Access::endDrag(preview);
    settle();
    require(preview.screen() == highDpi && preview.geometry() == QRect(200, 0, 1000, 333) &&
                Access::raster(preview).size() == QSize(2000, 666),
            "the initial wheel fixture must use a valid high-DPI raster");
    const quint64 generation = Access::generation(preview);
    require(Access::scaleSteps(preview, 26, QPointF(preview.geometry().center())),
            "a wheel target valid at its low-DPI destination must not use the old high-DPI limit");
    settle();
    require(preview.geometry() == QRect(200, 0, 3600, 1199) && preview.screen() == lowDpi &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(360)) &&
                Access::raster(preview).size() == QSize(3600, 1199) &&
                qFuzzyCompare(Access::raster(preview).devicePixelRatio(), qreal(1)) &&
                Access::source(preview).constBits() == state.image.constBits() &&
                Access::generation(preview) == generation + 1,
            "cross-screen wheel growth must compose exactly one raster at the destination DPR");
    const uchar* const pixels = Access::raster(preview).constBits();
    require(preview.present(state) && preview.geometry() == QRect(200, 0, 3600, 1199) &&
                Access::raster(preview).constBits() == pixels &&
                Access::generation(preview) == generation + 1,
            "content refresh after cross-DPI zoom must reuse the retained viewport raster");
    require(Access::scaleSteps(preview, -26, QPointF(preview.geometry().center())),
            "returning the zoomed preview to the high-DPI display failed");
    settle();
    require(preview.geometry() == QRect(200, 0, 1000, 333) && preview.screen() == highDpi &&
                Access::raster(preview).size() == QSize(2000, 666) &&
                qFuzzyCompare(Access::raster(preview).devicePixelRatio(), qreal(2)) &&
                Access::generation(preview) == generation + 2,
            "cross-screen wheel shrinkage must compose one raster at the new high-DPI extent");
}

void mixedDpiImagePresentationReusesTheDestinationRaster() {
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    QScreen* const lowDpi = mixedDpiScreen(QStringLiteral("external"), 1);
    QScreen* const highDpi = mixedDpiScreen(QStringLiteral("retina"), 2);
    QWidget owner;
    owner.setScreen(highDpi);
    owner.setGeometry(300, 180, 100, 80);
    owner.show();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    state.resultRect = owner.geometry();
    require(preview.present(state), "preparing the ordinary high-DPI image fixture failed");
    settle();
    const quint64 generation = Access::generation(preview);
    state.resultRect.translate(lowDpi->geometry().x(), 0);
    require(preview.present(state), "moving the ordinary preview to the low-DPI screen failed");
    settle();
    require(preview.screen() == lowDpi && Access::raster(preview).size() == QSize(100, 80) &&
                qFuzzyCompare(Access::raster(preview).devicePixelRatio(), qreal(1)) &&
                Access::generation(preview) == generation + 1,
            "ordinary image presentation must compose only the destination raster");
    const uchar* const pixels = Access::raster(preview).constBits();
    state.resultRect.translate(80, 20);
    require(preview.present(state) && Access::raster(preview).constBits() == pixels &&
                Access::generation(preview) == generation + 1,
            "movement within the destination display must reuse the ordinary image raster");
    state.resultRect = owner.geometry();
    require(preview.present(state), "moving the ordinary image back to the high-DPI screen failed");
    settle();
    require(preview.screen() == highDpi && Access::raster(preview).size() == QSize(200, 160) &&
                qFuzzyCompare(Access::raster(preview).devicePixelRatio(), qreal(2)) &&
                Access::generation(preview) == generation + 2,
            "returning an image to high DPI must rebuild once using the correct output extent");
}

void mouseDraggingPreservesTheViewportAndOwner() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    QWidget owner;
    owner.setGeometry(100, 100, 200, 120);
    owner.show();
    owner.activateWindow();
    owner.setFocus();
    settle();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    state.resultRect.setSize(QSize(97, 65));
    require(preview.present(state), "preparing the mouse drag fixture failed");
    settle();
    const QRect initial = preview.geometry();
    const QRect ownerGeometry = owner.geometry();
    QWidget* const focused = QApplication::focusWidget();
    const quint64 generation = ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
    const uchar* const pixels =
        ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).constBits();
    ResizeCounter resizeCounter;
    preview.installEventFilter(&resizeCounter);
    const QPointF cursor(initial.topLeft() + QPoint(7, 9));
    require(preview.cursor().shape() == Qt::OpenHandCursor,
            "a draggable preview must show the open hand cursor before dragging");
    sendMouseEvent(preview, QEvent::MouseButtonPress, cursor, Qt::RightButton, Qt::RightButton);
    sendMouseEvent(preview, QEvent::MouseMove, cursor + QPointF(30, 20), Qt::NoButton,
                   Qt::RightButton);
    sendMouseEvent(preview, QEvent::MouseButtonRelease, cursor + QPointF(30, 20), Qt::RightButton,
                   Qt::NoButton);
    require(preview.geometry() == initial && preview.cursor().shape() == Qt::OpenHandCursor,
            "right-button mouse input must not begin or move a preview drag");
    require(
        sendMouseEvent(preview, QEvent::MouseButtonPress, cursor, Qt::LeftButton, Qt::LeftButton) &&
            preview.cursor().shape() == Qt::ClosedHandCursor,
        "left-button input must start dragging and show the closed hand cursor");
    for (int step = 1; step <= 20; ++step) {
        const QPoint delta(3 * step, 2 * step);
        require(sendMouseEvent(preview, QEvent::MouseMove, cursor + delta, Qt::NoButton,
                               Qt::LeftButton),
                "left-button drag movement must be accepted");
        require(preview.geometry() == initial.translated(delta),
                "drag movement must preserve the pressed point's global cursor offset");
    }
    require(sendMouseEvent(preview, QEvent::MouseButtonRelease, cursor + QPointF(60, 40),
                           Qt::LeftButton, Qt::NoButton) &&
                preview.cursor().shape() == Qt::OpenHandCursor,
            "releasing the left button must end the drag and restore its open hand cursor");
    settle();
    const QRect dragged = initial.translated(60, 40);
    sendMouseEvent(preview, QEvent::MouseMove, cursor + QPointF(80, 50), Qt::NoButton,
                   Qt::NoButton);
    require(preview.geometry() == dragged && resizeCounter.count == 0 &&
                ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) == generation &&
                ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).constBits() ==
                    pixels,
            "dragging must only move the preview and reuse its unchanged viewport raster");
    require(owner.geometry() == ownerGeometry && QApplication::focusWidget() == focused &&
                preview.windowHandle()->transientParent() == owner.windowHandle(),
            "dragging must preserve the recognition owner's geometry, focus, and ownership");
}

void clickingDoesNotDetachAutomaticPlacement() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    require(preview.present(state), "preparing click-only placement failed");
    settle();
    const QRect initial = preview.geometry();
    const QPointF cursor(initial.topLeft() + QPoint(5, 4));
    const quint64 generation = ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
    require(
        sendMouseEvent(preview, QEvent::MouseButtonPress, cursor, Qt::LeftButton, Qt::LeftButton),
        "pressing a preview without moving failed");
    sendMouseEvent(preview, QEvent::MouseMove, cursor, Qt::NoButton, Qt::LeftButton);
    sendMouseEvent(preview, QEvent::MouseButtonRelease, cursor, Qt::LeftButton, Qt::NoButton);
    state.resultRect.translate(30, 20);
    ScreenshotOriginalImagePreviewWindow automaticPreview;
    require(automaticPreview.present(state), "preparing the click-only automatic fixture failed");
    require(preview.present(state) && preview.geometry() == automaticPreview.geometry() &&
                preview.geometry() != initial &&
                ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) == generation,
            "clicking without moving must preserve automatic placement relative to the result");
}

void draggedPlacementSurvivesContentAndBackingRefreshes() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 100, 80);
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(preview.present(state), "preparing the retained placement fixture failed");
    settle();
    require(Access::scaleSteps(preview, 2, QPointF(preview.geometry().center())),
            "zooming the retained placement fixture failed");
    const QPointF cursor(preview.geometry().topLeft() + QPoint(5, 4));
    require(Access::beginDrag(preview, cursor), "starting the retained placement drag failed");
    Access::moveDrag(preview, cursor + QPointF(93, 67));
    Access::endDrag(preview);
    const QRect adjustedGeometry = preview.geometry();
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    require(preview.present(state) && preview.geometry() == adjustedGeometry &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)) &&
                Access::generation(preview) == generation &&
                Access::raster(preview).constBits() == pixels,
            "ordinary content refreshes must retain manual placement, zoom and cached pixels");
    QEvent screenChange(QEvent::ScreenChangeInternal);
    QApplication::sendEvent(&preview, &screenChange);
    QEvent backingChange(QEvent::DevicePixelRatioChange);
    QApplication::sendEvent(&preview, &backingChange);
    settle();
    require(preview.geometry() == adjustedGeometry && Access::generation(preview) == generation &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)),
            "backing events with unchanged result context must retain the manual adjustment");
    state.imageRectInViewport.translate(1, 0);
    require(preview.present(state) && preview.geometry() == adjustedGeometry &&
                Access::generation(preview) == generation + 1,
            "source mapping changes must refresh pixels without resetting manual adjustment");
    state.image.setPixelColor(1, 1, Qt::red);
    require(preview.present(state) && preview.geometry() == adjustedGeometry &&
                Access::generation(preview) == generation + 2,
            "source pixels must update without resetting the chosen geometry");
    state.resultRect.translate(130, 90);
    ScreenshotOriginalImagePreviewWindow automaticPreview;
    require(automaticPreview.present(state), "preparing the geometry reset fixture failed");
    require(preview.present(state) && preview.geometry() == automaticPreview.geometry() &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(100)),
            "moving the result must resume automatic placement and scale");
    require(Access::scaleSteps(preview, -1, QPointF(preview.geometry().center())),
            "zooming before the result resize failed");
    state.resultRect.setSize(QSize(90, 70));
    require(automaticPreview.present(state) && preview.present(state) &&
                preview.geometry() == automaticPreview.geometry() &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(100)),
            "resizing the result must resume automatic placement and scale");
    require(Access::scaleSteps(preview, 1, QPointF(preview.geometry().center())),
            "zooming before the clear reset failed");
    preview.clear();
    require(preview.present(state) && preview.geometry() == automaticPreview.geometry() &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(100)),
            "clearing the preview must reset manual placement and zoom");
}

void availableSpaceChangesResumeAutomaticPlacementAfterDragging() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect = QRect(300, 180, 100, 80);
    require(preview.present(state), "preparing the work-area change fixture failed");
    const QRect initial = preview.geometry();
    require(Access::scaleSteps(preview, 2, QPointF(initial.center())),
            "zooming before the work-area change failed");
    const QPointF cursor(preview.geometry().topLeft() + QPoint(5, 4));
    require(Access::beginDrag(preview, cursor), "starting the work-area change drag failed");
    Access::moveDrag(preview, cursor + QPointF(40, 30));
    const QRect dragged = preview.geometry();
    // Simulate the prior cached work area differing from the current display bounds.
    Access::invalidateWorkArea(preview);
    require(preview.present(state) && preview.geometry() == dragged &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(120)),
            "changed available space must defer automatic fitting while dragging");
    Access::endDrag(preview);
    settle();
    require(preview.geometry() == initial &&
                qFuzzyCompare(Access::scalePercent(preview), qreal(100)),
            "ending a drag must resume automatic fitting for changed available space");
    const QPointF nextCursor(initial.topLeft() + QPoint(5, 4));
    require(Access::beginDrag(preview, nextCursor), "starting the result change drag failed");
    Access::moveDrag(preview, nextCursor + QPointF(20, 15));
    const QRect beforeResultChange = preview.geometry();
    state.resultRect.translate(50, 30);
    state.resultRect.setSize(QSize(110, 90));
    require(preview.present(state) && preview.geometry() == beforeResultChange,
            "result geometry changes must defer resizing and placement until drag release");
    Access::endDrag(preview);
    settle();
    ScreenshotOriginalImagePreviewWindow automaticPreview;
    require(automaticPreview.present(state) && preview.geometry() == automaticPreview.geometry(),
            "drag release must apply deferred automatic fitting for changed result geometry");
}

void hidingAndLosingMouseGrabCancelDragging() {
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry())
        return;
    ScreenshotOriginalImagePreviewWindow preview;
    const auto state = stateFor();
    require(preview.present(state), "preparing drag cancellation failed");
    settle();
    const QPointF cursor(preview.geometry().topLeft() + QPoint(5, 4));
    require(
        sendMouseEvent(preview, QEvent::MouseButtonPress, cursor, Qt::LeftButton, Qt::LeftButton),
        "starting the hide cancellation drag failed");
    sendMouseEvent(preview, QEvent::MouseMove, cursor + QPointF(30, 20), Qt::NoButton,
                   Qt::LeftButton);
    const QRect dragged = preview.geometry();
    preview.hide();
    require(preview.cursor().shape() == Qt::OpenHandCursor,
            "hiding the preview must cancel its active drag");
    ScreenshotOriginalImagePreviewWindowTestAccess::moveDrag(preview, cursor + QPointF(60, 40));
    require(preview.present(state) && preview.geometry() == dragged,
            "restoring a hidden preview must retain placement without resuming a stale drag");
    const QPointF nextCursor(dragged.topLeft() + QPoint(5, 4));
    require(sendMouseEvent(preview, QEvent::MouseButtonPress, nextCursor, Qt::LeftButton,
                           Qt::LeftButton),
            "starting the lost-grab cancellation drag failed");
    QEvent lostGrab(QEvent::UngrabMouse);
    QApplication::sendEvent(&preview, &lostGrab);
    sendMouseEvent(preview, QEvent::MouseMove, nextCursor + QPointF(20, 10), Qt::NoButton,
                   Qt::LeftButton);
    require(preview.geometry() == dragged && preview.cursor().shape() == Qt::OpenHandCursor,
            "losing the mouse grab must cancel dragging before later move events arrive");
    require(sendMouseEvent(preview, QEvent::MouseButtonPress, nextCursor, Qt::LeftButton,
                           Qt::LeftButton),
            "starting the missing-button cancellation drag failed");
    sendMouseEvent(preview, QEvent::MouseMove, nextCursor + QPointF(20, 10), Qt::NoButton,
                   Qt::NoButton);
    require(preview.geometry() == dragged && preview.cursor().shape() == Qt::OpenHandCursor,
            "a move without the left button must cancel dragging without moving the preview");
    require(ScreenshotOriginalImagePreviewWindowTestAccess::beginDrag(preview, nextCursor),
            "starting the clear cancellation drag failed");
    preview.clear();
    require(!preview.isVisible() && preview.cursor().shape() == Qt::OpenHandCursor,
            "clearing the preview must cancel its active drag");
}

void passiveWindowKeepsOwnershipAndClearsInvalidState() {
    auto owner = std::make_unique<QWidget>();
    owner->setGeometry(100, 100, 200, 100);
    owner->show();
    owner->activateWindow();
    owner->setFocus();
    settle();
    QPointer<ScreenshotOriginalImagePreviewWindow> preview =
        new ScreenshotOriginalImagePreviewWindow(owner.get());
    auto state = stateFor(owner.get());
    require(preview->present(state), "owned preview presentation failed");
    settle();
    require(preview->isWindow() && preview->focusPolicy() == Qt::NoFocus &&
                preview->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus) &&
                preview->testAttribute(Qt::WA_ShowWithoutActivating) &&
                !preview->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                !preview->windowFlags().testFlag(Qt::WindowTransparentForInput),
            "the preview must accept dragging without accepting focus or activation");
    require(QApplication::focusWidget() != preview &&
                preview->windowHandle()->transientParent() == owner->windowHandle(),
            "presenting the preview must retain its owner without taking focus");
    auto sibling = std::make_unique<QWidget>(owner.get(), Qt::Tool | Qt::FramelessWindowHint);
    state.aboveSibling = sibling.get();
    require(preview->present(state), "retaining the optional toolbar sibling failed");
    QEvent siblingBackingChange(QEvent::DevicePixelRatioChange);
    QApplication::sendEvent(preview, &siblingBackingChange);
    settle();
    require(ScreenshotOriginalImagePreviewWindowTestAccess::aboveSibling(*preview) == sibling.get(),
            "scheduled backing refresh must preserve the optional toolbar sibling");
    sibling.reset();
    require(ScreenshotOriginalImagePreviewWindowTestAccess::aboveSibling(*preview) == nullptr,
            "destroying the optional sibling must clear its guarded pointer");
    state.aboveSibling = nullptr;
    state.staysOnTop = false;
    require(preview->present(state), "changing preview stacking failed");
    require(!preview->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
            "logical-platform stacking must follow the state");
    state.imageRectInViewport.setX(std::numeric_limits<qreal>::quiet_NaN());
    require(!preview->present(state) && !preview->isVisible() &&
                ScreenshotOriginalImagePreviewWindowTestAccess::raster(*preview).isNull(),
            "invalid source geometry must hide and release the cached raster");
    state.imageRectInViewport = QRectF(1e308, 0, 1e308, 1);
    require(!preview->present(state) && !preview->isVisible(),
            "finite coordinates whose combined extent overflows must be rejected");
    state = stateFor(owner.get());
    require(preview->present(state), "reopening after invalid state failed");
    QEvent backingChange(QEvent::DevicePixelRatioChange);
    QApplication::sendEvent(preview, &backingChange);
    preview->clear();
    settle();
    require(!preview->isVisible() &&
                ScreenshotOriginalImagePreviewWindowTestAccess::source(*preview).isNull() &&
                ScreenshotOriginalImagePreviewWindowTestAccess::aboveSibling(*preview) == nullptr &&
                preview->windowHandle()->transientParent() == nullptr,
            "clearing must release source storage and transient ownership");
    require(preview->present(state), "reopening after clear failed");
    owner.reset();
    require(preview.isNull(), "destroying the owner must destroy the preview safely");
}

#if defined(Q_OS_WIN) || defined(_WIN32)
bool nativeWindowIsAbove(HWND higher, HWND lower) {
    for (HWND window = GetWindow(lower, GW_HWNDPREV); window;
         window = GetWindow(window, GW_HWNDPREV)) {
        if (window == higher)
            return true;
    }
    return false;
}

void nativePreviewPreservesAlienRecognitionAndCanvasChildren() {
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.resize(360, 220);
    QWidget canvas(&owner);
    canvas.setGeometry(0, 0, 360, 220);
    QWidget recognition(&owner);
    recognition.setGeometry(20, 20, 180, 100);
    owner.show();
    settle();
    require(owner.internalWinId() && !canvas.internalWinId() && !recognition.internalWinId(),
            "the native-child fixture must start with alien recognition and canvas widgets");
    ScreenshotOriginalImagePreviewWindow preview(&recognition);
    auto state = stateFor(&owner);
    state.resultRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
    state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
    require(preview.present(state), "presenting the alien-owned native preview failed");
    preview.repaint();
    settle();
    require(preview.internalWinId() && !preview.testAttribute(Qt::WA_NativeWindow) &&
                !canvas.internalWinId() && !recognition.internalWinId() &&
                !canvas.testAttribute(Qt::WA_NativeWindow) &&
                !recognition.testAttribute(Qt::WA_NativeWindow),
            "creating the preview must preserve the owner's alien recognition/canvas siblings");
}

void nativeOverlappingSiblingRemainsAboveWithoutActivation() {
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.setGeometry(100, 100, 220, 120);
    owner.show();
    settle();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    state.resultRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
    state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
    require(preview.present(state), "preparing the overlapping sibling fixture failed");
    settle();
    const QRect resultRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
    const QRect previewRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview);
    const HWND previewHwnd = reinterpret_cast<HWND>(preview.internalWinId());
    QWidget sibling(&owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                                Qt::WindowDoesNotAcceptFocus);
    sibling.setAttribute(Qt::WA_ShowWithoutActivating);
    sibling.resize(60, 24);
    sibling.show();
    settle();
    const HWND siblingHwnd = reinterpret_cast<HWND>(sibling.internalWinId());
    require(siblingHwnd != nullptr &&
                SetWindowPos(siblingHwnd, HWND_TOPMOST, previewRect.x(), previewRect.y(), 60, 24,
                             SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING) != FALSE,
            "positioning the overlapping passive sibling failed");
    settle();
    state.aboveSibling = &sibling;
    for (const bool staysOnTop : {true, false}) {
        state.staysOnTop = staysOnTop;
        require(SetWindowPos(siblingHwnd, staysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE,
                "setting the sibling's native stacking band failed");
        require(preview.present(state), "presenting with an overlapping sibling failed");
        settle();
        const QRect siblingRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&sibling);
        const HWND foreground = GetForegroundWindow();
        require(SetWindowPos(previewHwnd, staysOnTop ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE,
                "preparing the reversed overlap stacking failed");
        require(nativeWindowIsAbove(previewHwnd, siblingHwnd),
                "the overlap fixture must place the preview above the sibling first");
        preview.refreshStacking();
        settle();
        require(nativeWindowIsAbove(siblingHwnd, previewHwnd),
                "the overlapping same-band sibling must remain above the preview");
        require(GetForegroundWindow() == foreground &&
                    ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner) == resultRect &&
                    ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) ==
                        previewRect &&
                    ScreenshotOriginalImagePreviewWindow::nativeClientRect(&sibling) == siblingRect,
                "repairing overlap stacking must preserve activation and all window rectangles");
    }
    sibling.hide();
    preview.refreshStacking();
    settle();
    require(!sibling.isVisible(), "refreshing stacking must not restore a hidden sibling");
}

void nativeWindowMatchesOddResultExtentsAfterRepaints() {
    require(ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry(),
            "native geometry tests require the Windows platform");
    QScreen* screen = QGuiApplication::primaryScreen();
    require(screen != nullptr, "native geometry tests require a monitor");
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.setScreen(screen);
    owner.setGeometry(QRect(screen->geometry().topLeft() + QPoint(100, 100), QSize(421, 239)));
    owner.show();
    settle();
    std::cout << "native monitors=" << QGuiApplication::screens().size()
              << "; effective Qt DPR=" << owner.devicePixelRatioF() << '\n';
    if (qEnvironmentVariableIsSet("SNOW_PREVIEW_TEST_DPR")) {
        bool validFactor = false;
        const qreal factor = qEnvironmentVariable("SNOW_PREVIEW_TEST_DPR").toDouble(&validFactor);
        require(validFactor && qFuzzyCompare(owner.devicePixelRatioF(), factor),
                "the deterministic matrix must exercise the requested Qt DPR");
    }
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    for (const QSize size : {QSize(421, 239), QSize(503, 277), QSize(97, 25)}) {
        const auto native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        require(native && GetMonitorInfoW(native->handle(), &info) != FALSE,
                "native monitor geometry unavailable");
        const HWND hwnd = reinterpret_cast<HWND>(owner.winId());
        require(SetWindowPos(hwnd, nullptr, info.rcWork.left + 101, info.rcWork.top + 113,
                             size.width(), size.height(), SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "setting native result geometry failed");
        settle();
        if (ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner).size() != size) {
            require(SetWindowPos(hwnd, nullptr, info.rcWork.left + 101, info.rcWork.top + 113,
                                 size.width(), size.height(),
                                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING) != FALSE,
                    "setting the exact odd native result extent failed");
            settle();
        }
        require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner).size() == size,
                "the fixture result must expose the requested odd physical extent");
        auto state = stateFor(&owner);
        state.resultRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
        state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
        require(preview.present(state), "native preview presentation failed");
        for (int pass = 0; pass < 8; ++pass) {
            owner.repaint();
            preview.repaint();
            settle();
            require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner).size() == size,
                    "repainting must preserve the fixture's odd result extent");
            const QRect observed = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview);
            if (observed.size() != state.resultRect.size()) {
                std::cerr << "expected " << state.resultRect.width() << 'x'
                          << state.resultRect.height() << "; observed " << observed.width() << 'x'
                          << observed.height() << "; DPR " << preview.devicePixelRatioF()
                          << "; widget " << preview.width() << 'x' << preview.height() << "; pass "
                          << pass << '\n';
            }
            require(observed.size() == state.resultRect.size(),
                    "preview physical dimensions must match the result after every repaint");
        }
        const QImage painted = preview.grab().toImage();
        const QImage& raster = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
        if (painted.width() < size.width() || painted.height() < size.height()) {
            std::cerr << "painted " << painted.width() << 'x' << painted.height() << "; target "
                      << size.width() << 'x' << size.height() << "; DPR "
                      << preview.devicePixelRatioF() << "; widget " << preview.width() << 'x'
                      << preview.height() << '\n';
        }
        require(painted.width() >= size.width() && painted.height() >= size.height() &&
                    painted.pixel(0, 0) == raster.pixel(0, 0) &&
                    painted.pixel(size.width() - 1, size.height() - 1) ==
                        raster.pixel(size.width() - 1, size.height() - 1),
                "the native paint surface must retain the first and final visible image pixels");
        const QImage desktop = preview.windowHandle()->screen()->grabWindow(0).toImage();
        const QRect visible =
            ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview).translated(
                -QPoint(info.rcMonitor.left, info.rcMonitor.top));
        const QImage displayed = desktop.copy(visible);
        if (displayed.size() != size ||
            displayed.pixelColor(0, 0).rgb() != raster.pixelColor(0, 0).rgb() ||
            displayed.pixelColor(size.width() - 1, size.height() - 1).rgb() !=
                raster.pixelColor(size.width() - 1, size.height() - 1).rgb()) {
            std::cerr << "displayed " << displayed.width() << 'x' << displayed.height()
                      << "; first " << std::hex << displayed.pixelColor(0, 0).rgb() << '/'
                      << raster.pixelColor(0, 0).rgb() << "; last "
                      << displayed.pixelColor(size.width() - 1, size.height() - 1).rgb() << '/'
                      << raster.pixelColor(size.width() - 1, size.height() - 1).rgb() << std::dec
                      << '\n';
        }
        require(displayed.size() == size &&
                    displayed.pixelColor(0, 0).rgb() == raster.pixelColor(0, 0).rgb() &&
                    displayed.pixelColor(size.width() - 1, size.height() - 1).rgb() ==
                        raster.pixelColor(size.width() - 1, size.height() - 1).rgb(),
                "the native visible surface must retain the full physical image raster");
        require(ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).size() ==
                    state.resultRect.size(),
                "native raster size must match the exact visible result extent");
        preview.hide();
        require(preview.present(state), "restoring native preview failed");
        settle();
        require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview).size() ==
                    state.resultRect.size(),
                "restore must retain exact physical dimensions");
        const QImage retainedRaster =
            ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
        const QRgb retainedCorner = retainedRaster.pixel(size.width() - 1, size.height() - 1);
        preview.clear();
        require(!preview.isVisible() &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).isNull() &&
                    retainedRaster.pixel(size.width() - 1, size.height() - 1) == retainedCorner,
                "clearing native storage must preserve the lifetime of shared raster snapshots");
        require(preview.present(state), "reopening after clearing native storage failed");
        settle();
    }
    auto state = stateFor(&owner);
    state.resultRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
    state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
    require(preview.present(state), "preparing cross-monitor image mapping failed");
    const quint64 crossMonitorGeneration =
        ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
    for (QScreen* destination : QGuiApplication::screens()) {
        const auto native = destination->nativeInterface<QNativeInterface::QWindowsScreen>();
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!native || GetMonitorInfoW(native->handle(), &info) == FALSE)
            continue;
        state.resultRect.moveTopLeft(QPoint(info.rcWork.left + 150, info.rcWork.top + 100));
        require(preview.present(state), "cross-monitor preview placement failed");
        preview.repaint();
        settle();
        require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview).size() ==
                    state.resultRect.size(),
                "cross-monitor DPI changes must preserve result-sized physical extents");
        require(ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                    crossMonitorGeneration,
                "physical placement changes must reuse the raster across monitor DPI grids");
        const int gap = qRound(4 * destination->devicePixelRatio());
        const QRect work(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                         info.rcWork.bottom - info.rcWork.top);
        require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) ==
                    ScreenshotOriginalImagePreviewWindow::placement(state.resultRect, work, gap)
                        .geometry,
                "placement must use the result monitor's physical work area");
    }
    for (QScreen* destination : QGuiApplication::screens()) {
        const auto native = destination->nativeInterface<QNativeInterface::QWindowsScreen>();
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!native || GetMonitorInfoW(native->handle(), &info) == FALSE)
            continue;
        const QRect work(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                         info.rcWork.bottom - info.rcWork.top);
        const QSize largeOddExtent((work.width() / 2 + 1) | 1, (work.height() / 2 + 1) | 1);
        state.resultRect = QRect(work.topLeft() + QPoint(17, 19), largeOddExtent);
        state.imageRectInViewport = QRectF(QPointF(), QSizeF(largeOddExtent));
        const int gap = qRound(4 * destination->devicePixelRatio());
        const auto placement =
            ScreenshotOriginalImagePreviewWindow::placement(state.resultRect, work, gap);
        const QRect fallback = placement.geometry;
        require(placement.scale >= 0.5 && placement.scale <= 1.0 && work.contains(fallback) &&
                    fallback.left() >= state.resultRect.x() + state.resultRect.width() + gap,
                "an oversized corner preview must fit in the best available direction");
        require(preview.present(state), "native off-monitor fallback presentation failed");
        const quint64 fallbackGeneration =
            ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
        for (int pass = 0; pass < 4; ++pass) {
            preview.repaint();
            settle();
            require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) == fallback,
                    "automatically fitted previews must retain the exact native rectangle");
            require(ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                        fallbackGeneration,
                    "a fallback onto a different DPI grid must retain its viewport pixels");
        }
    }
}

void nativeZoomHintSurvivesRepaintsWithoutChangingCachedPixels() {
    using Access = ScreenshotOriginalImagePreviewWindowTestAccess;
    QScreen* screen = QGuiApplication::primaryScreen();
    require(screen != nullptr, "native zoom hint fixture requires a monitor");
    const auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    require(native && GetMonitorInfoW(native->handle(), &info) != FALSE,
            "native zoom hint monitor geometry unavailable");
    const QRect work(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                     info.rcWork.bottom - info.rcWork.top);
    ScreenshotOriginalImagePreviewWindow preview;
    auto state = stateFor();
    state.resultRect =
        QRect(work.topLeft() + QPoint(work.width() / 2, 113),
              QSize(std::min(421, work.width() / 3), std::min(239, work.height() / 3)));
    state.image.fill(QColor(210, 30, 40));
    state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
    state.mouseWheelZoomMode = QStringLiteral("top_left");
    require(
        preview.present(state) &&
            Access::scaleSteps(
                preview, 1,
                QPointF(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview).center())),
        "presenting and zooming the native hint fixture failed");
    const QRect target = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview);
    const quint64 generation = Access::generation(preview);
    const uchar* const pixels = Access::raster(preview).constBits();
    auto* label = Access::scaleLabel(preview);
    require(label && label->isVisible() && Access::hasHintSurface(preview),
            "native zooming must show and compose the scale hint");
    const qreal dpr = preview.devicePixelRatioF();
    const QRect hint(qRound(label->x() * dpr), qRound(label->y() * dpr),
                     qRound(label->width() * dpr), qRound(label->height() * dpr));
    const QRect content(0, 0, qFloor(target.width() / dpr), qFloor(target.height() / dpr));
    require(label->x() == content.left() + 8 &&
                label->y() + label->height() == content.height() - 8,
            "native hints must anchor inside the exact image extent on fractional DPI grids");
    const auto displayedImage = [&] {
        const QImage desktop = preview.windowHandle()->screen()->grabWindow(0).toImage();
        return desktop.copy(target.translated(-QPoint(info.rcMonitor.left, info.rcMonitor.top)));
    };
    for (int pass = 0; pass < 3; ++pass) {
        preview.repaint();
        QEvent update(QEvent::UpdateRequest);
        QApplication::sendEvent(&preview, &update);
        QEvent backingChange(QEvent::DevicePixelRatioChange);
        QApplication::sendEvent(&preview, &backingChange);
        settle();
        const QImage displayed = displayedImage();
        bool hintVisible = false;
        const QRect visibleHint = hint.intersected(displayed.rect());
        for (int y = visibleHint.top(); y <= visibleHint.bottom() && !hintVisible; ++y) {
            for (int x = visibleHint.left(); x <= visibleHint.right(); ++x) {
                if (displayed.pixelColor(x, y).rgb() !=
                    Access::raster(preview).pixelColor(x, y).rgb()) {
                    hintVisible = true;
                    break;
                }
            }
        }
        require(hintVisible && Access::hasHintSurface(preview) &&
                    ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) == target &&
                    Access::generation(preview) == generation &&
                    Access::raster(preview).constBits() == pixels,
                "native repaints must preserve the hint, exact extent and original cached pixels");
        const QPoint corner = hint.topLeft();
        const QPoint background(qRound((label->x() + label->width() / 2.0) * dpr),
                                qRound((label->y() + 1) * dpr));
        QImage expected(1, 1, QImage::Format_ARGB32_Premultiplied);
        expected.fill(Access::raster(preview).pixelColor(background));
        QPainter compositor(&expected);
        compositor.fillRect(expected.rect(), QColor(0, 0, 0, 150));
        compositor.end();
        require(displayed.pixelColor(corner).rgb() ==
                        Access::raster(preview).pixelColor(corner).rgb() &&
                    displayed.pixelColor(background).rgb() == expected.pixelColor(0, 0).rgb(),
                "native hints must preserve rounded corners and apply their background once");
    }
    const auto hintState = label->captureState();
    const int previousHeight = label->height();
    const QImage beforeFontChange = displayedImage();
    QFont larger = label->font();
    larger.setPixelSize(label->fontMetrics().height() + 8);
    label->setFont(larger);
    settle();
    require(label->height() > previousHeight && displayedImage() != beforeFontChange &&
                Access::hasHintSurface(preview) &&
                label->captureState().deadline == hintState.deadline &&
                Access::generation(preview) == generation &&
                Access::raster(preview).constBits() == pixels,
            "native font changes must refresh the hint without rebuilding image pixels or timer");
    auto* timer = Access::scaleLabelTimer(preview);
    require(timer != nullptr, "native zoom hint must own its timeout timer");
    timer->stop();
    require(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection),
            "dispatching the native hint timeout failed");
    preview.repaint();
    settle();
    const QImage displayed = displayedImage();
    const QPoint sample = hint.center();
    require(label->isHidden() && !Access::hasHintSurface(preview) &&
                displayed.rect().contains(sample) &&
                displayed.pixelColor(sample).rgb() ==
                    Access::raster(preview).pixelColor(sample).rgb() &&
                Access::generation(preview) == generation &&
                Access::raster(preview).constBits() == pixels,
            "the native timeout must restore the original pixels without invalidating the raster");
}

void nativeFormulaPlaceholderPreservesOddPhysicalExtents() {
    require(ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry(),
            "native formula placeholders require Windows physical geometry");
    QScreen* screen = QGuiApplication::primaryScreen();
    require(screen != nullptr, "native formula placeholders require a monitor");
    const auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    require(native && GetMonitorInfoW(native->handle(), &info) != FALSE,
            "native formula placeholder work area is available");
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.setScreen(screen);
    owner.setGeometry(
        QRect(screen->availableGeometry().topLeft() + QPoint(500, 113), QSize(200, 120)));
    owner.show();
    settle();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    state.image = {};
    state.formula = true;
    state.background = QColor(25, 75, 125);
    state.statusColor = QColor(245, 245, 245);
    for (const QSize size : {QSize(421, 239), QSize(97, 25), QSize(3, 2)}) {
        state.resultRect =
            QRect(info.rcWork.left + 500, info.rcWork.top + 113, size.width(), size.height());
        state.imageRectInViewport = QRectF(QPointF(), QSizeF(size));
        state.status = size.width() > 3 ? QStringLiteral("Rendering formula...") : QString{};
        require(preview.present(state), "presenting an odd native formula placeholder failed");
        settle();
        const quint64 generation =
            ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
        for (int pass = 0; pass < 4; ++pass) {
            QEvent backingChange(QEvent::DevicePixelRatioChange);
            QApplication::sendEvent(&preview, &backingChange);
            preview.repaint();
            settle();
            const QImage& raster = ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview);
            require(preview.isVisible() &&
                        ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview).size() ==
                            size &&
                        raster.size() == size && raster.pixelColor(0, 0) == state.background &&
                        raster.pixelColor(size.width() - 1, size.height() - 1) ==
                            state.background &&
                        preview.accessibleDescription() == state.status &&
                        ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                            generation,
                    "native placeholder refresh must preserve exact pixels, dimensions and status");
        }
    }
}

void nativeDraggingPreservesOddPhysicalExtentsAcrossMonitors() {
    require(ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry(),
            "native drag tests require Windows physical geometry");
    QScreen* primary = QGuiApplication::primaryScreen();
    require(primary != nullptr, "native drag tests require a monitor");
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.setScreen(primary);
    owner.setGeometry(
        QRect(primary->availableGeometry().topLeft() + QPoint(101, 113), QSize(180, 100)));
    owner.show();
    settle();
    const QRect ownerGeometry = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    for (const QSize size : {QSize(421, 239), QSize(503, 277), QSize(97, 25)}) {
        preview.clear();
        auto state = stateFor(&owner);
        state.image = sourceImage(size);
        state.resultRect = QRect(ownerGeometry.topLeft(), size);
        state.imageRectInViewport = QRectF(QPointF(), QSizeF(size));
        require(preview.present(state), "preparing native cross-monitor dragging failed");
        settle();
        const quint64 generation =
            ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
        const uchar* const pixels =
            ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).constBits();
        const QPoint offset(11, 13);
        const QPointF cursor(
            ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview).topLeft() + offset);
        require(ScreenshotOriginalImagePreviewWindowTestAccess::beginDrag(preview, cursor),
                "starting a physical preview drag failed");
        const HWND foreground = GetForegroundWindow();
        for (QScreen* destination : QGuiApplication::screens()) {
            const auto native = destination->nativeInterface<QNativeInterface::QWindowsScreen>();
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            require(native && GetMonitorInfoW(native->handle(), &info) != FALSE,
                    "native drag destination geometry unavailable");
            const QPoint requested(info.rcWork.left + 113, info.rcWork.top + 127);
            ScreenshotOriginalImagePreviewWindowTestAccess::moveDrag(preview, requested + offset);
            preview.repaint();
            settle();
            const QRect dragged = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview);
            const int gridTolerance = qCeil(destination->devicePixelRatio());
            require(qAbs(dragged.x() - requested.x()) <= gridTolerance &&
                        qAbs(dragged.y() - requested.y()) <= gridTolerance &&
                        MonitorFromWindow(reinterpret_cast<HWND>(preview.internalWinId()),
                                          MONITOR_DEFAULTTONEAREST) == native->handle(),
                    "physical dragging must move the preview itself onto its destination grid");
            for (int pass = 0; pass < 4; ++pass) {
                require(preview.present(state), "refreshing the native dragged preview failed");
                QEvent backingChange(QEvent::DevicePixelRatioChange);
                QApplication::sendEvent(&preview, &backingChange);
                preview.repaint();
                settle();
                require(
                    ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) == dragged &&
                        dragged.size() == size &&
                        ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).size() ==
                            size &&
                        ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                            generation &&
                        ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview)
                                .constBits() == pixels,
                    "cross-monitor dragging and refresh must retain exact odd physical extents "
                    "and the cached image raster");
                require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner) ==
                                ownerGeometry &&
                            GetForegroundWindow() == foreground,
                        "dragging a native preview must preserve its fixed recognition owner "
                        "and foreground window");
            }
        }
        ScreenshotOriginalImagePreviewWindowTestAccess::endDrag(preview);
        require(preview.cursor().shape() == Qt::OpenHandCursor,
                "ending a native drag must restore the open hand cursor");
        const QRect released = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview);
        QEvent backingChange(QEvent::DevicePixelRatioChange);
        QApplication::sendEvent(&preview, &backingChange);
        preview.repaint();
        settle();
        require(ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) == released &&
                    released.size() == size &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                        generation &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).constBits() ==
                        pixels,
                "backing refresh after release must retain the chosen physical rectangle, odd "
                "extent, and cached image raster");
    }
}
#endif

#ifdef Q_OS_MACOS
NSWindow* macNativeWindow(QWidget& widget) {
    if (!widget.internalWinId())
        return nil;
    return reinterpret_cast<NSView*>(widget.internalWinId()).window;
}

void settleMacNative() {
    QEventLoop loop;
    QTimer::singleShot(30, &loop, &QEventLoop::quit);
    loop.exec();
}

bool macWindowIsAbove(NSWindow* higher, NSWindow* lower) {
    NSArray<NSWindow*>* const windows = NSApp.orderedWindows;
    const NSUInteger higherIndex = [windows indexOfObjectIdenticalTo:higher];
    const NSUInteger lowerIndex = [windows indexOfObjectIdenticalTo:lower];
    return higherIndex != NSNotFound && lowerIndex != NSNotFound && higherIndex < lowerIndex;
}

void macStackingRejectsOffscreenSurfaces() {
    if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
        return;
    QWidget lower(nullptr, Qt::Tool);
    QWidget higher(nullptr, Qt::Tool);
    require(!snow_shot::platform::stackScreenshotWindowBelow(&lower, &higher) &&
                !lower.internalWinId() && !higher.internalWinId(),
            "offscreen relative stacking must decline without creating native surfaces");
    lower.show();
    higher.show();
    settle();
    require(!snow_shot::platform::stackScreenshotWindowBelow(&lower, &higher),
            "offscreen window handles must never be interpreted as Cocoa views");
}

void macPreviewPreservesLogicalGeometryAndAlienChildren() {
    require(QGuiApplication::platformName() == QStringLiteral("cocoa") &&
                !ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry(),
            "macOS native preview tests require Cocoa logical geometry");
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.resize(421, 239);
    QWidget canvas(&owner);
    canvas.setGeometry(owner.rect());
    QWidget recognition(&owner);
    recognition.setGeometry(20, 20, 180, 100);
    owner.show();
    settleMacNative();
    require(owner.internalWinId() && !canvas.internalWinId() && !recognition.internalWinId(),
            "the Cocoa fixture must start with alien recognition and canvas widgets");
    ScreenshotOriginalImagePreviewWindow preview(&recognition);
    auto state = stateFor(&owner);
    for (const bool pinned : {false, true}) {
        state.pinned = pinned;
        for (QScreen* destination : QGuiApplication::screens()) {
            for (const QSize size : {QSize(421, 239), QSize(503, 277), QSize(97, 25)}) {
                owner.setScreen(destination);
                owner.setGeometry(
                    QRect(destination->availableGeometry().topLeft() + QPoint(101, 113), size));
                canvas.setGeometry(owner.rect());
                settleMacNative();
                state.resultRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
                state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
                require(preview.present(state), "presenting the logical Cocoa preview failed");
                for (int pass = 0; pass < 3; ++pass) {
                    preview.repaint();
                    settleMacNative();
                    require(preview.size() == state.resultRect.size() &&
                                ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview)
                                        .size() == state.resultRect.size(),
                            "Cocoa preview dimensions must match the result in logical pixels");
                    require(!preview.testAttribute(Qt::WA_NativeWindow) &&
                                !canvas.internalWinId() && !recognition.internalWinId() &&
                                !canvas.testAttribute(Qt::WA_NativeWindow) &&
                                !recognition.testAttribute(Qt::WA_NativeWindow),
                            "creating and moving the Cocoa preview must preserve alien siblings");
                }
            }
        }
    }
}

void macDraggingPreservesLogicalExtentsAcrossMonitors() {
    require(QGuiApplication::platformName() == QStringLiteral("cocoa") &&
                !ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry(),
            "Cocoa drag tests require logical geometry");
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.setGeometry(100, 100, 180, 100);
    owner.show();
    settleMacNative();
    const QRect ownerGeometry = owner.geometry();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    for (const QSize size : {QSize(421, 239), QSize(503, 277), QSize(97, 25)}) {
        preview.clear();
        auto state = stateFor(&owner);
        state.resultRect = QRect(ownerGeometry.topLeft(), size);
        state.imageRectInViewport = QRectF(QPointF(), QSizeF(size));
        require(preview.present(state), "preparing Cocoa cross-monitor dragging failed");
        settleMacNative();
        const QPoint offset(11, 13);
        const QPointF cursor(preview.geometry().topLeft() + offset);
        require(ScreenshotOriginalImagePreviewWindowTestAccess::beginDrag(preview, cursor),
                "starting a logical preview drag failed");
        for (QScreen* destination : QGuiApplication::screens()) {
            const QPoint requested = destination->availableGeometry().topLeft() + QPoint(113, 127);
            ScreenshotOriginalImagePreviewWindowTestAccess::moveDrag(preview, requested + offset);
            settleMacNative();
            const QRect dragged(requested, size);
            const BOOL active = [NSApp isActive];
            NSWindow* keyWindow = NSApp.keyWindow;
            const quint64 generation =
                ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
            const uchar* const pixels =
                ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview).constBits();
            for (int pass = 0; pass < 3; ++pass) {
                require(preview.present(state), "refreshing the logical dragged preview failed");
                preview.repaint();
                settleMacNative();
                NSWindow* native = macNativeWindow(preview);
                require(preview.geometry() == dragged &&
                            ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) ==
                                dragged &&
                            qRound(native.frame.size.width) == size.width() &&
                            qRound(native.frame.size.height) == size.height() &&
                            !native.ignoresMouseEvents,
                        "Cocoa dragging must retain the same logical extent on every monitor "
                        "and accept mouse input");
                require(owner.geometry() == ownerGeometry && [NSApp isActive] == active &&
                            NSApp.keyWindow == keyWindow &&
                            ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                                generation &&
                            ScreenshotOriginalImagePreviewWindowTestAccess::raster(preview)
                                    .constBits() == pixels,
                        "Cocoa drag refresh must reuse stable backing pixels while preserving "
                        "owner geometry and activation");
            }
        }
        ScreenshotOriginalImagePreviewWindowTestAccess::endDrag(preview);
    }
}

void macOversizedPreviewUsesTheBestAvailableDirection() {
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    owner.resize(180, 100);
    owner.show();
    settleMacNative();
    for (QScreen* destination : QGuiApplication::screens()) {
        owner.setScreen(destination);
        owner.move(destination->availableGeometry().topLeft() + QPoint(11, 13));
        settleMacNative();
        const QRect ownerRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
        const NSRect ownerFrame = macNativeWindow(owner).frame;
        for (const bool pinned : {false, true}) {
            ScreenshotOriginalImagePreviewWindow preview(&owner);
            auto state = stateFor(&owner);
            state.pinned = pinned;
            // Model an oversized result without allocating another full-screen source image.
            state.resultRect =
                QRect(ownerRect.topLeft(), destination->geometry().size() + QSize(17, 17));
            state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
            require(preview.present(state), "presenting the oversized Cocoa fallback failed");
            const QRect fallback =
                ScreenshotOriginalImagePreviewWindow::placement(
                    state.resultRect,
                    ScreenshotOriginalImagePreviewWindowTestAccess::workArea(preview), 4)
                    .geometry;
            for (int pass = 0; pass < 3; ++pass) {
                preview.repaint();
                settleMacNative();
                NSWindow* native = macNativeWindow(preview);
                const NSRect frame = native.frame;
                require(preview.geometry() == fallback &&
                            ScreenshotOriginalImagePreviewWindow::nativeClientRect(&preview) ==
                                fallback &&
                            !(native.styleMask & NSWindowStyleMaskTitled) &&
                            qRound(frame.size.width) == fallback.width() &&
                            qRound(frame.size.height) == fallback.height() &&
                            qRound(frame.origin.x - ownerFrame.origin.x) ==
                                fallback.x() - ownerRect.x() &&
                            qRound(NSMaxY(frame) - NSMaxY(ownerFrame)) ==
                                ownerRect.y() - fallback.y(),
                        "Cocoa must preserve the selected oversized borderless placement and "
                        "native frame");
            }
        }
    }
}

void macSiblingStackingPreservesActivationAndVisibility() {
    QWidget owner(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    owner.setAttribute(Qt::WA_ShowWithoutActivating);
    QScreen* primary = QGuiApplication::primaryScreen();
    require(primary != nullptr, "the Cocoa stacking fixture requires a screen");
    const QRect available = primary->availableGeometry();
    owner.setGeometry(QRect(available.center() - QPoint(110, 60), QSize(220, 120)));
    owner.show();
    settleMacNative();
    ScreenshotOriginalImagePreviewWindow preview(&owner);
    auto state = stateFor(&owner);
    state.pinned = true;
    state.resultRect = ScreenshotOriginalImagePreviewWindow::nativeClientRect(&owner);
    state.imageRectInViewport = QRectF(QPointF(), QSizeF(state.resultRect.size()));
    require(preview.present(state), "preparing the Cocoa overlap fixture failed");
    settleMacNative();
    QWidget sibling(&owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    sibling.setAttribute(Qt::WA_ShowWithoutActivating);
    sibling.setGeometry(QRect(preview.geometry().topLeft(), QSize(60, 24)));
    sibling.show();
    settleMacNative();
    NSWindow* previewNative = macNativeWindow(preview);
    NSWindow* siblingNative = macNativeWindow(sibling);
    require(previewNative && siblingNative, "Cocoa overlap surfaces must have native windows");
    // Real pin surfaces stay visible while another application is active. Match that
    // behavior in this plain-widget fixture before testing inactive-app ordering.
    macNativeWindow(owner).hidesOnDeactivate = NO;
    siblingNative.hidesOnDeactivate = NO;
    QWidget uncreated(nullptr, Qt::Tool);
    require(!snow_shot::platform::stackScreenshotWindowBelow(&preview, &uncreated) &&
                !uncreated.internalWinId(),
            "relative ordering must not materialize an uncreated sibling");
    [NSApp deactivate];
    QElapsedTimer deactivation;
    deactivation.start();
    while ([NSApp isActive] && deactivation.elapsed() < 2000)
        settleMacNative();
    require(![NSApp isActive], "the Cocoa passivity fixture must deactivate before ordering");
    for (const bool staysOnTop : {true, false}) {
        state.staysOnTop = staysOnTop;
        state.aboveSibling = nullptr;
        require(preview.present(state), "preparing the preview's Cocoa stacking band failed");
        settleMacNative();
        siblingNative.level = previewNative.level;
        [previewNative orderWindow:NSWindowAbove relativeTo:siblingNative.windowNumber];
        require(macWindowIsAbove(previewNative, siblingNative),
                "the Cocoa overlap fixture must begin with reversed same-band order");
        const BOOL active = [NSApp isActive];
        NSWindow* keyWindow = NSApp.keyWindow;
        const NSInteger previewLevel = previewNative.level;
        const NSInteger siblingLevel = siblingNative.level;
        const QRect previewRect = preview.geometry();
        const QRect siblingRect = sibling.geometry();
        const QRect resultRect = owner.geometry();
        state.aboveSibling = &sibling;
        require(preview.present(state), "presenting with an overlapping Cocoa sibling failed");
        settleMacNative();
        require(macWindowIsAbove(siblingNative, previewNative) &&
                    previewNative.level == previewLevel && siblingNative.level == siblingLevel,
                "Cocoa relative stacking must preserve both levels and put the sibling above");
        require([NSApp isActive] == active && NSApp.keyWindow == keyWindow &&
                    owner.geometry() == resultRect && preview.geometry() == previewRect &&
                    sibling.geometry() == siblingRect && !previewNative.ignoresMouseEvents,
                "Cocoa preview stacking must preserve activation, key window, geometry and input");
        const quint64 generation =
            ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview);
        QEvent backingChange(QEvent::DevicePixelRatioChange);
        QApplication::sendEvent(&preview, &backingChange);
        preview.hide();
        settleMacNative();
        require(!preview.isVisible() && !previewNative.visible && [NSApp isActive] == active &&
                    NSApp.keyWindow == keyWindow,
                "queued Cocoa backing updates must not restore a suppressed preview");
        require(preview.present(state), "restoring the hidden Cocoa preview failed");
        settleMacNative();
        require(preview.isVisible() && previewNative.visible &&
                    macWindowIsAbove(siblingNative, previewNative) &&
                    ScreenshotOriginalImagePreviewWindowTestAccess::generation(preview) ==
                        generation &&
                    [NSApp isActive] == active && NSApp.keyWindow == keyWindow,
                "Cocoa restoration must reuse pixels and retain passive sibling ordering");
        sibling.hide();
        preview.refreshStacking();
        settleMacNative();
        require(!sibling.isVisible() && !siblingNative.visible &&
                    !snow_shot::platform::stackScreenshotWindowBelow(&preview, &sibling),
                "stacking refresh must not restore a hidden Cocoa sibling");
        sibling.show();
        siblingNative.hidesOnDeactivate = NO;
        settleMacNative();
    }
}
#endif
} // namespace

int main(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        if (QByteArray(argv[index]) == QByteArrayLiteral("--mixed-dpi-only")) {
            qputenv("QT_SCALE_FACTOR", "1");
            qputenv("QT_SCREEN_SCALE_FACTORS", "retina=2;external=1");
            qunsetenv("QT_SCALE_FACTOR_ROUNDING_POLICY");
            qunsetenv("QT_WIDGETS_HIGHDPI_DOWNSCALE");
            break;
        }
    }
    QApplication application(argc, argv);
    try {
        if (application.arguments().contains(QStringLiteral("--mixed-dpi-only"))) {
            mixedDpiPresentationUsesTheDestinationRasterLimit();
            mixedDpiImagePresentationReusesTheDestinationRaster();
            mixedDpiRejectedPresentationPreservesTheWindowAndCache();
            mixedDpiWheelZoomPreparesOneRasterForTheDestination();
        } else if (application.arguments().contains(QStringLiteral("--native-only"))) {
#if defined(Q_OS_WIN) || defined(_WIN32)
            nativePreviewPreservesAlienRecognitionAndCanvasChildren();
            nativeWindowMatchesOddResultExtentsAfterRepaints();
            nativeFormulaPlaceholderPreservesOddPhysicalExtents();
            nativeZoomHintSurvivesRepaintsWithoutChangingCachedPixels();
            nativeOverlappingSiblingRemainsAboveWithoutActivation();
            nativeDraggingPreservesOddPhysicalExtentsAcrossMonitors();
#elif defined(Q_OS_MACOS)
            macPreviewPreservesLogicalGeometryAndAlienChildren();
            macOversizedPreviewUsesTheBestAvailableDirection();
            macSiblingStackingPreservesActivationAndVisibility();
            macDraggingPreservesLogicalExtentsAcrossMonitors();
#endif
        } else {
            originalImageStateDefaultsDisableFormulaPresentation();
            placementHonorsTheResultMonitorAndPriority();
            viewportRasterPreservesClippingAndLetterboxing();
            largeSourceCoordinatesRetainVisiblePixels();
            sourceDprMetadataDoesNotAlterViewportMapping();
            movementReusesTheRasterAndContentChangesInvalidateIt();
            formulaStateSurvivesQueuedBackingRefreshes();
            formulaPlaceholdersRetainNullImagesAndExactSmallExtents();
            formulaErrorsFitTheCompleteImageAboveTheFooter();
            wheelZoomUsesPinnedLevelsAndShowsAnEphemeralHint();
            visibleScaleHintRetranslatesWithoutChangingZoom();
            scaleHintFontChangesPreservePixelsAndDeadline();
            wheelGesturesRespectBoundariesAndIgnoreMomentum();
            automaticScaleAdvancesToPinnedWheelLevels();
            wheelZoomUsesConfiguredAnchors();
            zoomScalesTheViewportMappingAndFormulaPlaceholders();
            rejectedZoomPreservesGeometryRasterAndScale();
            rejectedAutomaticPresentationPreservesThePriorValidPreview();
            mouseDraggingPreservesTheViewportAndOwner();
            clickingDoesNotDetachAutomaticPlacement();
            draggedPlacementSurvivesContentAndBackingRefreshes();
            availableSpaceChangesResumeAutomaticPlacementAfterDragging();
            hidingAndLosingMouseGrabCancelDragging();
            passiveWindowKeepsOwnershipAndClearsInvalidState();
#ifdef Q_OS_MACOS
            macStackingRejectsOffscreenSurfaces();
#endif
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
