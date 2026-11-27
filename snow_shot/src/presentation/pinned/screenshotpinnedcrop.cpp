// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/screenshotpinnedwindow.h"
#include "snow_shot/presentation/screenshotpinnedrenderpreparation.h"
#include "../services/screenshotpinnedrenderbytes_p.h"
#include "screenshotpinnedcropgeometry.h"
#include "snow_draw_engine_qt/snow_canvas_overlay_style.h"
#include "screenshotpinnednativegeometrycontroller.h"
#include "pinnedwindowplatform.h"
#include "snow_shot/presentation/pinnedgeometry.h"
#include "snow_shot/presentation/pinnedwindowselectioncontroller.h"
#include "snow_shot/presentation/screenshotpinnededitcontroller.h"
#include "snow_shot/presentation/screenshotcanvasrenderer.h"
#include "snow_shot/presentation/screenshotrecognitionsessioncontroller.h"
#include "snow_shot/presentation/screenshotrecognitionwindow.h"
#include "snow_shot/presentation/screenshotocrpresentation.h"
#include "snow_shot/presentation/screenshotocrrecognitionservice.h"
#include "snow_shot/presentation/screenshotanimationplayback.h"
#include "snow_shot/presentation/screenshotselectionshadowrenderer.h"
#include "snow_draw_engine_qt/snow_canvas_image.h"
#include "snow_draw_engine_qt/snow_canvas_widget.h"
#include <QApplication>
#include <QPainter>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include "widgets/message.h"

namespace crop_geometry = screenshot_pinned_crop_geometry;

namespace {
QSize transformedRasterSize(QSize size, const QTransform& transform) {
    if (transform.isIdentity())
        return size;
    const auto matrix = QImage::trueMatrix(transform, size.width(), size.height());
    const auto bounds = matrix.mapRect(QRectF(QPointF(), QSizeF(size)));
    constexpr auto maximum = std::numeric_limits<int>::max() - 1;
    if (!std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) || bounds.isEmpty() ||
        bounds.width() > maximum || bounds.height() > maximum)
        return {};
    // Qt rotation can align fractional edges on both sides of the transformed raster.
    return {static_cast<int>(std::ceil(bounds.width())) + 1,
            static_cast<int>(std::ceil(bounds.height())) + 1};
}

std::optional<qint64> cropWorkingBytes(const QImage& image, QRect pixels,
                                       const QTransform& orientation,
                                       const ScreenshotResultStyle* bakedStyle, qreal canvasWidth,
                                       const ScreenshotAnimationSource* animation) {
    using screenshot_pinned_render::addBytes;
    using screenshot_pinned_render::rasterBytes;
    qint64 bytes = image.sizeInBytes();
    const auto addRasters = [&bytes](QSize size, int depth, int count) {
        const auto raster = rasterBytes(size, depth);
        if (!raster)
            return false;
        for (int rasterIndex = 0; rasterIndex < count; ++rasterIndex)
            if (!addBytes(bytes, *raster))
                return false;
        return true;
    };
    const int depth = std::max(32, image.depth());
    const QSize transformed = transformedRasterSize(pixels.size(), orientation);
    if (!addRasters(pixels.size(), depth, 1) || !addRasters(transformed, depth, 2))
        return std::nullopt;
    if (bakedStyle) {
        const QSize oriented = transformedRasterSize(image.size(), orientation);
        const auto scaled =
            ScreenshotResultCompositor::scaledStyle(*bakedStyle, oriented.width() / canvasWidth);
        const auto layout = ScreenshotResultCompositor::layoutForContent(oriented, scaled);
        // The full oriented source and its normalization can coexist with composition,
        // its shape mask and shadow scratch until the viewport has been copied.
        if (!layout.isValid() || !addRasters(oriented, depth, 2) ||
            !addRasters(layout.outputRect.size(), 32, 4))
            return std::nullopt;
    }
    if (animation) {
        if (!addBytes(bytes, animation->encoded.size()))
            return std::nullopt;
        for (const auto* frame : {&animation->firstFrame, &animation->firstDisplayFrame})
            if (!frame->isNull() && (!addBytes(bytes, frame->sizeInBytes()) ||
                                     !addRasters(pixels.size(), frame->depth(), 1)))
                return std::nullopt;
    }
    return bytes;
}
} // namespace

QRectF ScreenshotPinnedWindow::visibleCanvasRect() const {
    if (!m_cropRect || m_originalPixelSize.isEmpty())
        return m_resultSurfaceCanvasRect;
    return crop_geometry::sourceToCanvas(m_originalPixelSize, m_imageTransform,
                                         m_backgroundCanvasRect)
        .mapRect(*m_cropRect);
}

QRectF ScreenshotPinnedWindow::exportVisibleCanvasRect() const {
    if (!m_animationTransformSnapshot)
        return visibleCanvasRect();
    const auto& snapshot = *m_animationTransformSnapshot;
    return m_cropRect ? crop_geometry::sourceToCanvas(m_originalPixelSize, snapshot.transform,
                                                      snapshot.backgroundRect)
                            .mapRect(*m_cropRect)
                      : snapshot.surfaceRect;
}

QSizeF ScreenshotPinnedWindow::scaleReferenceSize() const {
    return screenshotPinnedScaleReferenceSize(m_initialWindowSize, m_exactInitialWindowSize,
                                              m_quarterTurns, m_cropRect, m_originalPixelSize,
                                              m_canvasSourceRect, m_resultStyle);
}

bool ScreenshotPinnedWindow::cropSelectionAllowed() const {
    return m_presented && m_firstContentFramePublished && !m_closing && !m_lockedMode &&
           !m_cropJob.isValid() && !m_contentReplacementJob.isValid() && !m_clickThroughActive &&
           !hideToTopActive() && !m_thumbnailMode && !m_ocrMode && !m_geometryAnimating &&
           !m_interactionPlacement && !m_selectionGeometryActive && !m_windowDragActive &&
           !animationTransformPending() && (!m_editController || !m_editController->editMode()) &&
           (!isLiveWindow() || liveFrameAvailable());
}

bool ScreenshotPinnedWindow::cropActionsAllowed() const {
    const bool panning =
        m_mouseStroke && m_mouseStroke->press.drag == snow_shot::mouse::Action::PanCrop;
    return m_cropRect && m_presented && m_firstContentFramePublished && !m_lockedMode &&
           !m_closing && !m_cropJob.isValid() && !m_geometryAnimating && !m_cropSelectionOrigin &&
           !panning && !m_interactionPlacement && !m_selectionGeometryActive &&
           !m_contentReplacementJob.isValid() && !animationTransformPending() &&
           !m_animationTransformSnapshot && (!isLiveWindow() || liveFrameAvailable());
}

bool ScreenshotPinnedWindow::beginCropSelection(const QPointF& position) {
    if (!cropSelectionAllowed())
        return false;
    const QRect geometry = authoritativeContentNativeGeometry();
    m_cropSelectionGeometry = geometry;
    m_cropSelectionCanvas = visibleCanvasRect();
    m_cropSelectionScale =
        m_scalePercent / 100.0 * scaleReferenceSize().width() / m_cropSelectionCanvas.width();
    const QRectF available = m_cropSelectionCanvas.intersected(m_backgroundCanvasRect);
    m_cropSelectionBounds =
        QRectF(QPointF(geometry.topLeft()) +
                   (available.topLeft() - m_cropSelectionCanvas.topLeft()) * m_cropSelectionScale,
               available.size() * m_cropSelectionScale)
            .intersected(QRectF(geometry));
    if (!m_cropSelectionBounds.contains(position))
        return false;
    m_cropSelectionOrigin = position;
    m_cropSelectionEnd = position;
    if (!m_mouseStroke) {
        // Crop can also be initiated directly by the presentation controller.
        MouseStroke stroke;
        stroke.press.drag = snow_shot::mouse::Action::Crop;
        stroke.press.button = stroke.press.reportedButton = Qt::RightButton;
        stroke.press.crossed = stroke.started = true;
        stroke.receiver = this;
        stroke.nativeOrigin = position;
        m_mouseStroke = stroke;
    }
    grabMouse();
    qApp->installEventFilter(this);
    return true;
}

void ScreenshotPinnedWindow::updateCropSelection(const QPointF& position) {
    if (!m_cropSelectionOrigin)
        return;
    const QRectF bounds(m_cropSelectionBounds);
    const QPointF end{std::clamp(position.x(), bounds.left(), bounds.right()),
                      std::clamp(position.y(), bounds.top(), bounds.bottom())};
    if (end == m_cropSelectionEnd)
        return;
    QWidget* target = m_liveNativePresentation ? static_cast<QWidget*>(this) : m_canvas;
    const QRectF previous = cropSelectionViewRect(target);
    m_cropSelectionEnd = end;
    if (target)
        target->update(crop_geometry::selectionRepaintRegion(
            previous, cropSelectionViewRect(target), target->rect()));
}

QRectF ScreenshotPinnedWindow::cropSelectionViewRect(const QWidget* target) const {
    if (!m_cropSelectionOrigin || !target)
        return {};
    const auto native = QRectF(*m_cropSelectionOrigin, m_cropSelectionEnd).normalized();
    const QRectF content = contentViewRect();
    const qreal sx = content.width() / m_cropSelectionGeometry.width();
    const qreal sy = content.height() / m_cropSelectionGeometry.height();
    const QPointF topLeft(content.x() + (native.x() - m_cropSelectionGeometry.x()) * sx,
                          content.y() + (native.y() - m_cropSelectionGeometry.y()) * sy);
    return QRectF(target->mapFrom(this, topLeft),
                  QSizeF(native.width() * sx, native.height() * sy));
}

void ScreenshotPinnedWindow::paintCropSelection(QPainter& painter, const QWidget* target) const {
    snowCanvasPaintSelectionMarquee(painter, cropSelectionViewRect(target));
}

void ScreenshotPinnedWindow::endCropSelection(bool cancel) {
    if (!m_cropSelectionOrigin)
        return;
    const QRectF selection = QRectF(*m_cropSelectionOrigin, m_cropSelectionEnd).normalized();
    QWidget* target = m_liveNativePresentation ? static_cast<QWidget*>(this) : m_canvas;
    const QRectF previous = cropSelectionViewRect(target);
    m_cropSelectionOrigin.reset();
    if (m_mouseStroke && m_mouseStroke->press.drag == snow_shot::mouse::Action::Crop)
        m_mouseStroke.reset();
    qApp->removeEventFilter(this);
    if (QWidget::mouseGrabber() == this)
        releaseMouse();
    if (target)
        target->update(crop_geometry::selectionRepaintRegion(previous, {}, target->rect()));
    if (cancel || !cropSelectionAllowed())
        return;
    if (selection.width() < 10 || selection.height() < 10)
        return;
    const qreal sx = 1.0 / m_cropSelectionScale;
    const qreal sy = sx;
    const QRectF canvas(
        m_cropSelectionCanvas.x() + (selection.x() - m_cropSelectionGeometry.x()) * sx,
        m_cropSelectionCanvas.y() + (selection.y() - m_cropSelectionGeometry.y()) * sy,
        selection.width() * sx, selection.height() * sy);
    if (setCropCanvasRect(canvas)) {
        if (m_selectionController)
            m_selectionController->deselectWindow(this);
        setWindowSelected(false);
        schedulePersistence();
    }
}

void ScreenshotPinnedWindow::cancelCropInteraction() {
    if (m_mouseStroke && (m_mouseStroke->press.drag == snow_shot::mouse::Action::PanCrop ||
                          m_mouseStroke->press.drag == snow_shot::mouse::Action::Crop))
        cancelMouseOperation();
    endCropSelection(true);
}

void ScreenshotPinnedWindow::updateCropPan(const QRectF& original, const QPointF& nativeDelta) {
    const auto content = authoritativeContentNativeGeometry();
    if (content.isEmpty())
        return;
    const auto mapping = crop_geometry::sourceToCanvas(m_originalPixelSize, m_imageTransform,
                                                       m_backgroundCanvasRect);
    const QSizeF visible = mapping.mapRect(original).size();
    const QPointF canvasDelta(nativeDelta.x() * visible.width() / content.width(),
                              nativeDelta.y() * visible.height() / content.height());
    const auto inverse = mapping.inverted();
    const auto desired = original.topLeft() + inverse.map(QPointF{}) - inverse.map(canvasDelta);
    const QRectF next(
        QPointF(std::clamp(desired.x(), 0.0,
                           std::max(0.0, m_originalPixelSize.width() - original.width())),
                std::clamp(desired.y(), 0.0,
                           std::max(0.0, m_originalPixelSize.height() - original.height()))),
        original.size());
    if (m_cropRect != next) {
        m_cropRect = next;
        updateCanvasViewport();
        scheduleLivePresentation();
    }
}

bool ScreenshotPinnedWindow::setCropCanvasRect(std::optional<QRectF> requested) {
    const QRectF viewport =
        requested ? requested->intersected(m_backgroundCanvasRect) : m_resultSurfaceCanvasRect;
    if (!crop_geometry::valid(viewport) || m_originalPixelSize.isEmpty())
        return false;
    const QRectF previous = visibleCanvasRect();
    const QRect native =
        m_thumbnailMode ? snow_shot::presentation::pinnedContentRect(m_preThumbnailNativeGeometry)
                        : authoritativeContentNativeGeometry();
    std::optional<QRectF> source;
    if (requested) {
        const auto mapping = crop_geometry::sourceToCanvas(m_originalPixelSize, m_imageTransform,
                                                           m_backgroundCanvasRect);
        source = mapping.inverted().mapRect(viewport);
    }
    const qreal scale = m_scalePercent / 100.0 * scaleReferenceSize().width() / previous.width();
    const QRect target(qRound(native.x() + (viewport.x() - previous.x()) * scale),
                       qRound(native.y() + (viewport.y() - previous.y()) * scale),
                       std::max(1, qRound(viewport.width() * scale)),
                       std::max(1, qRound(viewport.height() * scale)));
    bool applied = false;
    {
        QScopedValueRollback<std::optional<QRectF>> crop(m_cropRect, source);
        QScopedValueRollback<bool> preserve(m_preserveScaleForSettledGeometry, true);
        const QRect outer = snow_shot::presentation::pinnedOuterRect(target);
        if (m_thumbnailMode) {
            m_preThumbnailNativeGeometry = outer;
            if (screen())
                m_preThumbnailPlacement =
                    snow_shot::presentation::pinnedPlacement(outer, *screen());
            applied = true;
        } else {
            applied = applyWindowGeometry(outer, GeometryMutation::ContentReplacement);
        }
        if (applied) {
            crop.commit();
            preserve.commit();
        }
    }
    updateCropPresentation();
    return applied;
}

void ScreenshotPinnedWindow::updateCropPresentation() {
    cancelCropPreparation();
    ++m_contentReplacementGeneration;
    invalidatePendingCopy();
    cancelCloudUpload();
    if (m_quickSaveArtifact) {
        m_quickSaveArtifact->cancel();
        m_quickSaveArtifact.reset();
        m_quickSavePending = false;
    }
    if (auto cancel = std::exchange(m_cancelNativeImageRequest, {}))
        cancel();
    m_fileSaveJob.cancel();
    m_fileSaveJob = {};
    applyRuntimeBorderColor();
    updateCanvasViewport();
    updateControlsGeometry();
    refreshContextMenuIfVisible();
    if (m_editController)
        m_editController->updatePlacement();
    if (isLiveWindow())
        scheduleLivePresentation();
}

void ScreenshotPinnedWindow::cancelCrop() {
    if (cropActionsAllowed() && setCropCanvasRect(std::nullopt))
        schedulePersistence();
}

void ScreenshotPinnedWindow::cancelCropPreparation() {
    if (!m_cropJob.isValid())
        return;
    ++m_cropPreparationGeneration;
    std::exchange(m_cropJob, {}).cancel();
    if (!m_closing && !m_destroying) {
        updateAnimationSuspension();
        refreshContextMenuIfVisible();
    }
}

void ScreenshotPinnedWindow::applyCrop() {
    if (!cropActionsAllowed())
        return;
    if (m_originalImage.isNull()) {
        requestMaterializedImage([this, generation = m_contentReplacementGeneration](bool ready) {
            if (ready && generation == m_contentReplacementGeneration)
                applyCrop();
        });
        return;
    }
    const QRect pixels = crop_geometry::pixelBounds(*m_cropRect, m_originalPixelSize);
    if (pixels.isEmpty())
        return;
    struct Prepared {
        QImage original;
        QImage transformed;
        QTransform transform;
        std::shared_ptr<const ScreenshotAnimationSource> animation;
        bool bakedStyle = false;
    };
    const auto prepared = std::make_shared<Prepared>();
    const auto crop = m_cropRect;
    const auto generation = m_presentationGeneration;
    const auto replacement = m_contentReplacementGeneration;
    const auto preparation = m_cropPreparationGeneration;
    const auto finish = [this, prepared, pixels, crop, generation, replacement,
                         preparation](const ScreenshotExportTaskResult& result) {
        if (preparation != m_cropPreparationGeneration)
            return;
        m_cropJob = {};
        if (!m_closing && !m_lockedMode && generation == m_presentationGeneration &&
            replacement == m_contentReplacementGeneration && crop == m_cropRect &&
            result.failureStage != ScreenshotExportFailureStage::Cancelled) {
            const bool success =
                result.succeeded() && !prepared->original.isNull() &&
                !prepared->transformed.isNull() &&
                commitPreparedCrop(pixels, std::move(prepared->original),
                                   std::move(prepared->transformed), prepared->transform,
                                   std::move(prepared->animation), prepared->bakedStyle);
            if (!success) {
                adqt::widgets::AdMessage::Request message;
                message.key = QStringLiteral("screenshot-pinned-crop-status");
                message.content = tr("The pinned image could not be prepared");
                adqt::widgets::AdMessageService::error(std::move(message), this);
            }
        }
        updateAnimationSuspension();
        refreshContextMenu();
    };
    const QImage image = m_originalImage;
    const auto source = m_animationSource;
    const auto orientation = m_imageTransform;
    const auto style = m_resultStyle;
    const qreal canvasWidth = m_backgroundCanvasRect.width();
    const bool bake = !source && !isLiveWindow() &&
                      (style.region || style.cornerRadius > 0 || style.effectWidth() > 0);
    const auto workingBytes = cropWorkingBytes(image, pixels, orientation, bake ? &style : nullptr,
                                               canvasWidth, source.get());
    if (!workingBytes) {
        finish(ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::Render, {}));
        return;
    }
    m_cropJob = ScreenshotExportCoordinator::shared().submit(
        this, ScreenshotExportCoordinator::Priority::Foreground,
        [image, source, orientation, style, canvasWidth, pixels, bake,
         prepared](const ScreenshotExportCancellation& cancellation) {
            const auto cancelled = [] {
                return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::Cancelled,
                                                           {});
            };
            if (cancellation.isCancellationRequested())
                return cancelled();
            Prepared result;
            result.bakedStyle = bake;
            if (bake) {
                const auto releaseCaches = qScopeGuard([&style] {
                    ScreenshotSelectionShadowRenderer::resetCacheForCurrentThread();
                    if (style.region)
                        style.region->clearDerivedCache();
                });
                const auto oriented =
                    snowCanvasTransformImage(image, orientation, Qt::SmoothTransformation);
                if (cancellation.isCancellationRequested())
                    return cancelled();
                if (oriented.isNull())
                    return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::Render,
                                                               {});
                const auto scaled =
                    ScreenshotResultCompositor::scaledStyle(style, oriented.width() / canvasWidth);
                const auto composed = ScreenshotResultCompositor::compose(oriented, scaled);
                if (cancellation.isCancellationRequested())
                    return cancelled();
                const auto mapping = QImage::trueMatrix(orientation, image.width(), image.height());
                auto region = mapping.mapRect(QRectF(pixels)).toAlignedRect();
                region.translate(
                    -ScreenshotResultCompositor::layoutForContent(oriented.size(), scaled)
                         .outputRect.topLeft());
                result.original = snowCanvasCopyImage(composed, region);
            } else {
                result.original = snowCanvasCopyImage(image, pixels);
                result.transform = QImage::trueMatrix(orientation, pixels.width(), pixels.height());
            }
            if (cancellation.isCancellationRequested())
                return cancelled();
            {
                const auto transformed = snowCanvasTransformImage(result.original, result.transform,
                                                                  Qt::SmoothTransformation);
                if (cancellation.isCancellationRequested())
                    return cancelled();
                result.transformed = ScreenshotResultCompositor::normalizeImage(transformed);
            }
            if (cancellation.isCancellationRequested())
                return cancelled();
            if (source) {
                auto next = std::make_shared<ScreenshotAnimationSource>(*source);
                next->sourceCropRect = pixels.translated(next->sourceCropRect.topLeft());
                next->canvasSize = pixels.size();
                next->firstFrame = snowCanvasCopyImage(next->firstFrame, pixels);
                if (cancellation.isCancellationRequested())
                    return cancelled();
                next->firstDisplayFrame =
                    source->firstFrame.constBits() == source->firstDisplayFrame.constBits()
                        ? next->firstFrame
                        : snowCanvasCopyImage(next->firstDisplayFrame, pixels);
                if (next->firstFrame.isNull() || next->firstDisplayFrame.isNull())
                    return ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::Render,
                                                               {});
                result.animation = std::move(next);
            }
            if (cancellation.isCancellationRequested())
                return cancelled();
            *prepared = std::move(result);
            return ScreenshotExportTaskResult{};
        },
        finish, *workingBytes);
    if (!m_cropJob.isValid()) {
        finish(ScreenshotExportTaskResult::failure(ScreenshotExportFailureStage::Queue, {}));
        return;
    }
    updateAnimationSuspension();
    refreshContextMenu();
}

bool ScreenshotPinnedWindow::commitPreparedCrop(
    const QRect& pixels, QImage original, QImage transformed, const QTransform& transform,
    std::shared_ptr<const ScreenshotAnimationSource> animation, bool bakedStyle) {
    const qreal ux = m_canvasSourceRect.width() / m_originalPixelSize.width();
    const qreal uy = m_canvasSourceRect.height() / m_originalPixelSize.height();
    const auto mapping = crop_geometry::sourceToCanvas(m_originalPixelSize, m_imageTransform,
                                                       m_backgroundCanvasRect);
    const QRectF canvas = mapping.mapRect(QRectF(pixels));
    if (!setCropCanvasRect(canvas))
        return false;
    QSizeF baseline = scaleReferenceSize();
    if (!bakedStyle && m_quarterTurns % 2)
        baseline.transpose();
    const bool recognitionVisible = m_ocrMode;
    const auto recognitionMode = m_recognitionSession
                                     ? m_recognitionSession->mode()
                                     : ScreenshotRecognitionSessionController::Mode::Text;
    auto results = recognitionSnapshot();
    if (bakedStyle)
        results = transformedRecognitionSnapshot(std::move(results), m_canvasSourceRect,
                                                 m_originalPixelSize, m_imageTransform,
                                                 m_transformedImage.size(), m_backgroundCanvasRect);
    const QPointF offset = bakedStyle ? QPointF()
                                      : canvas.topLeft() - m_canvasSourceRect.topLeft() -
                                            QPointF(pixels.x() * ux, pixels.y() * uy);
    const auto translatePresentation =
        [&offset](std::shared_ptr<ScreenshotOcrPresentation>& value) {
            if (!value)
                return;
            value = std::make_shared<ScreenshotOcrPresentation>(*value);
            value->selection.translate(offset.toPoint());
            for (auto& line : value->lines) {
                for (auto& point : line.quad)
                    point += offset;
                for (auto& quad : line.sourceLineQuads)
                    for (auto& point : quad)
                        point += offset;
            }
            value->prepareForRendering();
        };
    if (results.text) {
        translatePresentation(results.text->presentation);
        results.text->filteredImage = {};
    }
    translatePresentation(results.translatedText);
    if (isLiveWindow()) {
        const QRectF previous =
            m_liveAppliedCrop.isEmpty() ? QRectF(0, 0, 1, 1) : m_liveAppliedCrop;
        m_liveAppliedCrop =
            QRectF(previous.x() + previous.width() * pixels.x() / m_originalPixelSize.width(),
                   previous.y() + previous.height() * pixels.y() / m_originalPixelSize.height(),
                   previous.width() * pixels.width() / m_originalPixelSize.width(),
                   previous.height() * pixels.height() / m_originalPixelSize.height());
        ++m_liveFrameGeneration;
    }
    m_cropRect.reset();
    clearTextAnnotations();
    ++m_contentReplacementGeneration;
    if (!isLiveWindow())
        ++m_persistenceSourceRevision;
    m_originalClipboardContent = {};
    m_formattedTextDocument.reset();
    m_formattedPlainText.clear();
    m_formattedTextAvailable = false;
    m_formattedTextDevicePixelRatio = 1;
    m_originalImage = original;
    m_originalPixelSize = original.size();
    m_ocrSupported = screenshotOcrImageWithinPixelLimit(m_originalPixelSize);
    m_transformedImage = isLiveWindow() && !transform.isIdentity() ? QImage() : transformed;
    m_imageTransform = transform;
    if (bakedStyle)
        m_quarterTurns = 0;
    m_canvasSourceRect =
        bakedStyle ? canvas
                   : QRectF(canvas.topLeft(), QSizeF(pixels.width() * ux, pixels.height() * uy));
    m_backgroundCanvasRect = canvas;
    m_resultSurfaceCanvasRect = canvas;
    m_resultStyle = {};
    m_borderAppearance.reset();
    m_exactInitialWindowSize = baseline;
    m_initialWindowSize = baseline.toSize().expandedTo(QSize(1, 1));
    m_imageSource = ScreenshotImageSource::fromImage(
        isLiveWindow() ? m_originalImage : m_transformedImage, canvas);
    m_recognitionResults = std::move(results);
    m_recognitionTargetReady = false;
    refreshColorSwatch();
    m_animationSource = std::move(animation);
    if (isLiveWindow())
        m_screenshotRenderer->setPinnedLiveFrame(m_imageSource, m_imageTransform);
    else
        m_screenshotRenderer->setImageSource(m_imageSource);
    m_screenshotRenderer->setPinnedResultSurface(canvas, canvas, m_resultStyle);
    if (m_animationPlayback) {
        static_cast<void>(m_animationPlayback->adoptCroppedSource(m_animationSource, original,
                                                                  transformed, transform));
    } else if (!isLiveWindow()) {
        configureRecognitionSession();
        configureRecognitionTarget();
        if (recognitionVisible)
            activateRecognitionMode(static_cast<int>(recognitionMode), std::nullopt);
    }
    updateCropPresentation();
    schedulePersistence();
    return true;
}
