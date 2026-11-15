#include "snow_draw_engine_qt/snow_canvas_image.h"
#include "snow_shot/presentation/screenshotoriginalimagepreviewwindow.h"

#include "snow_shot/presentation/screenshotimagerendering.h"
#include "snow_shot/presentation/canvasstatusreadout.h"
#include "../pinned/screenshotpinnedresizegeometry.h"
#include "widgets/dpi_stable_window_controller.h"

#ifdef Q_OS_MACOS
#include "../pinned/pinnedwindowplatform.h"
#include "../../platform/macos/windowcursorcoordinator.h"
#include "snow_shot/platform/screenshotnative.h"
#endif

#include <QApplication>
#include <QEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QTimer>
#include <QWindow>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include <QtGui/qscreen_platform.h>
#include <qt_windows.h>
#endif

namespace {
constexpr int kPreviewGap = 4;
constexpr qreal kMinimumScalePercent = 10;
constexpr qreal kMaximumScalePercent = 500;
constexpr int kWheelScaleStep = 10;

std::optional<QPointF> wheelCursorPosition(const QWheelEvent& event) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry()) {
        POINT cursor{};
        if (GetCursorPos(&cursor))
            return QPointF(cursor.x, cursor.y);
        return std::nullopt;
    }
#endif
    return event.globalPosition();
}

std::optional<QPointF> dragCursorPosition(const QMouseEvent& event) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry()) {
        POINT cursor{};
        if (GetCursorPos(&cursor))
            return QPointF(cursor.x, cursor.y);
        return std::nullopt;
    }
#endif
    return event.globalPosition();
}

bool finiteRect(const QRectF& rect) {
    return rect.isValid() && !rect.isEmpty() && std::isfinite(rect.x()) &&
           std::isfinite(rect.y()) && std::isfinite(rect.width()) && std::isfinite(rect.height()) &&
           std::isfinite(rect.x() + rect.width()) && std::isfinite(rect.y() + rect.height());
}

struct PlacementContext final {
    QScreen* screen = nullptr;
    QRect workArea;
};

qreal distanceToRect(const QPointF& point, const QRect& rect) {
    const qreal dx =
        point.x() - std::clamp(point.x(), qreal(rect.left()), qreal(rect.x()) + rect.width());
    const qreal dy =
        point.y() - std::clamp(point.y(), qreal(rect.top()), qreal(rect.y()) + rect.height());
    return dx * dx + dy * dy;
}

PlacementContext placementContext(const QRect& result) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry()) {
        const RECT rectangle{result.x(), result.y(), result.x() + result.width(),
                             result.y() + result.height()};
        const HMONITOR monitor = MonitorFromRect(&rectangle, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!monitor || GetMonitorInfoW(monitor, &info) == FALSE)
            return {};
        QScreen* selected = nullptr;
        for (QScreen* screen : QGuiApplication::screens()) {
            const auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
            if (native && native->handle() == monitor) {
                selected = screen;
                break;
            }
        }
        return {selected,
                QRect(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                      info.rcWork.bottom - info.rcWork.top)};
    }
#endif
    QScreen* selected = nullptr;
    qint64 largestArea = -1;
    qreal shortestDistance = std::numeric_limits<qreal>::max();
    const QPointF center = QRectF(result).center();
    for (QScreen* screen : QGuiApplication::screens()) {
        const QRect intersection = result.intersected(screen->geometry());
        const qint64 area = qint64(intersection.width()) * intersection.height();
        const qreal distance = distanceToRect(center, screen->geometry());
        if (area > largestArea || (area == largestArea && distance < shortestDistance)) {
            selected = screen;
            largestArea = area;
            shortestDistance = distance;
        }
    }
    if (!selected)
        return {};
#ifdef Q_OS_MACOS
    return {selected,
            snow_shot::presentation::pinnedDisplayGeometry(*selected).usableBounds.toRect()};
#else
    return {selected, selected->availableGeometry()};
#endif
}

#if defined(Q_OS_WIN) || defined(_WIN32)
int coveringLogicalExtent(int pixels, qreal dpr) {
    int extent = std::max(1, qRound(pixels / dpr));
    while (qRound(extent * dpr) < pixels)
        ++extent;
    return extent;
}

HWND nativeWindow(WId id) {
    return reinterpret_cast<HWND>(id); // NOLINT(performance-no-int-to-ptr)
}

bool applyNativeGeometry(QWidget* window, const QRect& target) {
    const HWND hwnd = nativeWindow(window->internalWinId());
    if (!hwnd)
        return false;
    const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if (SetWindowPos(hwnd, nullptr, target.x(), target.y(), target.width(), target.height(),
                     flags) == FALSE)
        return false;
    if (ScreenshotOriginalImagePreviewWindow::nativeClientRect(window) == target)
        return true;
    // QWidget can round the extent onto its logical grid in WM_WINDOWPOSCHANGING. The
    // recognition viewport owns this physical extent, including odd fractional-DPI sizes.
    return SetWindowPos(hwnd, nullptr, target.x(), target.y(), target.width(), target.height(),
                        flags | SWP_NOSENDCHANGING) != FALSE &&
           ScreenshotOriginalImagePreviewWindow::nativeClientRect(window) == target;
}
#endif
} // namespace

struct ScreenshotOriginalImagePreviewWindow::NativeSurface final {
#if defined(Q_OS_WIN) || defined(_WIN32)
    explicit NativeSurface(const QSize& extent) : size(extent) {
        dc = CreateCompatibleDC(nullptr);
        if (!dc)
            return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size.width();
        info.bmiHeader.biHeight = -size.height();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap)
            previousBitmap = SelectObject(dc, bitmap);
    }
    ~NativeSurface() {
        if (dc && previousBitmap && previousBitmap != HGDI_ERROR)
            SelectObject(dc, previousBitmap);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
    }
    [[nodiscard]] bool isValid() const {
        return dc && bitmap && pixels && previousBitmap && previousBitmap != HGDI_ERROR;
    }
    [[nodiscard]] static QImage imageFor(const std::shared_ptr<NativeSurface>& surface,
                                         qreal dpr = 1) {
        if (!surface || !surface->isValid())
            return {};
        QImage image(
            static_cast<uchar*>(surface->pixels), surface->size.width(), surface->size.height(),
            qsizetype(surface->size.width()) * 4, QImage::Format_ARGB32_Premultiplied,
            [](void* holder) { delete static_cast<std::shared_ptr<NativeSurface>*>(holder); },
            new std::shared_ptr<NativeSurface>(surface));
        image.setDevicePixelRatio(dpr);
        return image;
    }
    [[nodiscard]] bool publish(WId windowId, const QRect& target) const {
        if (!isValid() || target.size() != size)
            return false;
        const HWND hwnd = nativeWindow(windowId);
        if (!hwnd)
            return false;
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (!(style & WS_EX_LAYERED))
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
        const POINT destination{target.x(), target.y()};
        const POINT source{};
        const SIZE extent{size.width(), size.height()};
        const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const UPDATELAYEREDWINDOWINFO update{sizeof(UPDATELAYEREDWINDOWINFO),
                                             nullptr,
                                             &destination,
                                             &extent,
                                             dc,
                                             &source,
                                             0,
                                             &blend,
                                             ULW_ALPHA,
                                             nullptr};
        return UpdateLayeredWindowIndirect(hwnd, &update) != FALSE;
    }

    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    void* pixels = nullptr;
    QSize size;
#endif
};

struct ScreenshotOriginalImagePreviewWindow::RasterState final {
    QImage image;
    std::shared_ptr<NativeSurface> surface;
    std::shared_ptr<NativeSurface> hint;
    qint64 sourceKey = 0;
    QRectF imageRect;
    QSize pixelSize;
    qreal outputDpr = 0;
    quint64 generation = 0;
    bool publicationPending = false;
};

ScreenshotOriginalImagePreviewWindow::RasterState
ScreenshotOriginalImagePreviewWindow::captureRaster() const {
    return {m_viewportImage,   m_nativeSurface,    m_hintSurface,
            m_cachedSourceKey, m_cachedImageRect,  m_cachedPixelSize,
            m_cachedOutputDpr, m_rasterGeneration, m_nativePublicationPending};
}

void ScreenshotOriginalImagePreviewWindow::restoreRaster(const RasterState& state) {
    m_viewportImage = state.image;
    m_nativeSurface = state.surface;
    m_hintSurface = state.hint;
    m_cachedSourceKey = state.sourceKey;
    m_cachedImageRect = state.imageRect;
    m_cachedPixelSize = state.pixelSize;
    m_cachedOutputDpr = state.outputDpr;
    m_rasterGeneration = state.generation;
    m_nativePublicationPending = state.publicationPending;
}

ScreenshotOriginalImagePreviewWindow::ScreenshotOriginalImagePreviewWindow(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                          Qt::WindowDoesNotAcceptFocus) {
    setObjectName(QStringLiteral("screenshotOriginalImagePreviewWindow"));
    setWindowFlag(Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::OpenHandCursor);
    m_scaleLabel = new CanvasStatusReadout(this);
    m_scaleLabel->setObjectName(QStringLiteral("originalImagePreviewScaleReadout"));
    connect(m_scaleLabel, &CanvasStatusReadout::presentationChanged, this,
            &ScreenshotOriginalImagePreviewWindow::synchronizeHintSurface);
    const auto watch = [this](QScreen* screen) {
        connect(screen, &QScreen::geometryChanged, this, [this] { scheduleRefresh(); });
        connect(screen, &QScreen::availableGeometryChanged, this, [this] { scheduleRefresh(); });
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { scheduleRefresh(); });
    };
    for (QScreen* screen : QGuiApplication::screens())
        watch(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this, watch](QScreen* screen) {
        watch(screen);
        scheduleRefresh();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { scheduleRefresh(); });
}

ScreenshotOriginalImagePreviewWindow::~ScreenshotOriginalImagePreviewWindow() = default;

bool ScreenshotOriginalImagePreviewWindow::usesPhysicalGeometry() {
#if defined(Q_OS_WIN) || defined(_WIN32)
    return QGuiApplication::platformName() == QStringLiteral("windows");
#else
    return false;
#endif
}

QRect ScreenshotOriginalImagePreviewWindow::nativeClientRect(QWidget* widget) {
    if (!widget)
        return {};
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (usesPhysicalGeometry()) {
        QWidget* topLevel = widget->window();
        const HWND hwnd = nativeWindow(topLevel->internalWinId());
        RECT client{};
        POINT origin{};
        if (!hwnd || GetClientRect(hwnd, &client) == FALSE ||
            ClientToScreen(hwnd, &origin) == FALSE)
            return {};
        const QRect native(origin.x, origin.y, client.right - client.left,
                           client.bottom - client.top);
        if (widget == topLevel)
            return native;
        if (topLevel->width() <= 0 || topLevel->height() <= 0)
            return {};
        const QPoint logicalOrigin = widget->mapTo(topLevel, QPoint());
        const auto x = [&](int value) {
            return qRound(qreal(value) * native.width() / topLevel->width());
        };
        const auto y = [&](int value) {
            return qRound(qreal(value) * native.height() / topLevel->height());
        };
        return QRect(native.x() + x(logicalOrigin.x()), native.y() + y(logicalOrigin.y()),
                     x(logicalOrigin.x() + widget->width()) - x(logicalOrigin.x()),
                     y(logicalOrigin.y() + widget->height()) - y(logicalOrigin.y()))
            .intersected(native);
    }
#endif
    return QRect(widget->mapToGlobal(QPoint()), widget->size());
}

ScreenshotOriginalImagePreviewWindow::Placement
ScreenshotOriginalImagePreviewWindow::placement(const QRect& result, const QRect& workArea, int gap,
                                                bool preferLeft) {
    if (!result.isValid() || result.isEmpty())
        return {};
    gap = std::max(0, gap);
    if (!workArea.isValid() || workArea.isEmpty())
        return {
            QRect(result.x() - result.width() - gap, result.y(), result.width(), result.height()),
            1.0};
    const QRectF work(workArea);
    const qreal leftEdge = std::clamp(qreal(result.x()) - gap, work.left(), work.right());
    const qreal topEdge = std::clamp(qreal(result.y()) - gap, work.top(), work.bottom());
    const qreal rightEdge =
        std::clamp(qreal(result.x()) + result.width() + gap, work.left(), work.right());
    const std::array<QSizeF, 3> available = {QSizeF(leftEdge - work.left(), work.height()),
                                             QSizeF(work.width(), topEdge - work.top()),
                                             QSizeF(work.right() - rightEdge, work.height())};
    std::size_t direction = 0;
    qreal fittingScale = 0;
    constexpr qreal minimumFittingScale = 0.5;
    for (std::size_t index = 0; index < available.size(); ++index) {
        const qreal scale =
            std::max(qreal(0), std::min({qreal(1), available[index].width() / result.width(),
                                         available[index].height() / result.height()}));
        if (scale > fittingScale) {
            fittingScale = scale;
            direction = index;
        }
        if (preferLeft && index == 0 && fittingScale >= minimumFittingScale)
            break;
    }
    const qreal scale = fittingScale >= minimumFittingScale ? fittingScale : 1.0;
    const QSize size = screenshot_pinned_resize_geometry::scaledSize(result.size(), scale);
    const auto aligned = [](qreal preferred, qreal start, qreal end, int extent) {
        return qRound(extent <= end - start ? std::clamp(preferred, start, end - extent)
                                            : preferred);
    };
    QPoint position;
    if (direction == 0) {
        position = QPoint(qRound(qreal(result.x()) - gap - size.width()),
                          aligned(result.y(), work.top(), work.bottom(), size.height()));
        if (fittingScale >= minimumFittingScale)
            position.setX(qRound(leftEdge - size.width()));
    } else if (direction == 1) {
        position = QPoint(aligned(result.x(), work.left(), work.right(), size.width()),
                          qRound(qreal(result.y()) - gap - size.height()));
        if (fittingScale >= minimumFittingScale)
            position.setY(qRound(topEdge - size.height()));
    } else {
        position = QPoint(qRound(qreal(result.x()) + result.width() + gap),
                          aligned(result.y(), work.top(), work.bottom(), size.height()));
        if (fittingScale >= minimumFittingScale)
            position.setX(qRound(rightEdge));
    }
    return {QRect(position, size), scale};
}

bool ScreenshotOriginalImagePreviewWindow::present(
    const ScreenshotOriginalImagePreviewState& state) {
    const auto notifyHidden = qScopeGuard([this] {
        if (!isVisible())
            emit hidden();
    });
    const QScopedValueRollback<bool> presenting(m_presenting, true);
    if ((!state.formula && state.image.isNull()) || !finiteRect(state.imageRectInViewport) ||
        !state.resultRect.isValid() || state.resultRect.isEmpty()) {
        clear();
        return false;
    }
    // Automatic placement follows the result, never the preview's manually chosen monitor.
    const PlacementContext context = placementContext(state.resultRect);
    if (!context.screen || !context.workArea.isValid()) {
        clear();
        return false;
    }
    const ScreenshotOriginalImagePreviewState previous{m_sourceImage,
                                                       m_imageRectInViewport,
                                                       m_resultRect,
                                                       m_transientOwner.data(),
                                                       m_pinned,
                                                       m_staysOnTop,
                                                       m_aboveSibling.data(),
                                                       m_formula,
                                                       m_dimmed,
                                                       m_background,
                                                       m_statusColor,
                                                       m_status,
                                                       m_mouseWheelZoomMode,
                                                       m_preferLeft};
    const RasterState previousRaster = captureRaster();
    const QRect previousTarget = m_targetGeometry;
    const qreal previousPercent = m_scalePercent;
    const QRect previousPlacementResult = m_placementResultRect;
    const QRect previousWorkArea = m_placementWorkArea;
    const QPointer<QScreen> previousScreen = m_placementScreen;
    const int previousGap = m_placementGap;
    const bool previousPlacementPreferLeft = m_placementPreferLeft;
    const bool previousUserPositioned = m_userPositioned;
    const bool previousAutoPending = m_autoPlacementPending;
    const auto previousHint = m_scaleLabel->captureState();
    const bool canRestore =
        isVisible() && previousTarget.isValid() && !previousRaster.image.isNull();
    bool prepared = false;
    {
        const QScopedValueRollback<bool> applying(m_applyingGeometry, true);
        const int gap = usesPhysicalGeometry()
                            ? qRound(kPreviewGap * context.screen->devicePixelRatio())
                            : kPreviewGap;
        const bool placementChanged =
            m_placementResultRect != state.resultRect || m_placementScreen != context.screen ||
            m_placementWorkArea != context.workArea || m_placementGap != gap ||
            m_placementPreferLeft != state.preferLeft;
        if (placementChanged && m_dragging)
            m_autoPlacementPending = true;
        const Placement automatic =
            placement(state.resultRect, context.workArea, gap, state.preferLeft);
        if (placementChanged && !m_dragging) {
            m_userPositioned = false;
            m_scalePercent = automatic.scale * 100;
            m_placementResultRect = state.resultRect;
            m_placementScreen = context.screen;
            m_placementWorkArea = context.workArea;
            m_placementGap = gap;
            m_placementPreferLeft = state.preferLeft;
            m_autoPlacementPending = false;
            hideScaleReadout();
            m_wheelSteps.reset();
        }
        m_sourceImage = state.image;
        if (m_formula != state.formula || m_dimmed != state.dimmed ||
            m_background != state.background || m_statusColor != state.statusColor ||
            m_status != state.status)
            m_cachedSourceKey = -1;
        m_formula = state.formula;
        m_dimmed = state.dimmed;
        m_background = state.background;
        m_statusColor = state.statusColor;
        m_status = state.status;
        setAccessibleDescription(m_status);
        m_imageRectInViewport = state.imageRectInViewport;
        m_resultRect = state.resultRect;
        m_mouseWheelZoomMode = state.mouseWheelZoomMode;
        if (m_transientOwner != state.transientOwner || m_aboveSibling != state.aboveSibling ||
            m_pinned != state.pinned || m_staysOnTop != state.staysOnTop)
            m_siblingStackedAbove = false;
        m_transientOwner = state.transientOwner;
        m_aboveSibling = state.aboveSibling;
        m_pinned = state.pinned;
        m_preferLeft = state.preferLeft;
        m_staysOnTop = state.staysOnTop;
        const QRect target =
            m_dragging ? m_targetGeometry
            : m_userPositioned
                ? QRect(m_targetGeometry.topLeft(), screenshot_pinned_resize_geometry::scaledSize(
                                                        m_resultRect.size(), m_scalePercent / 100))
                : automatic.geometry;
        const PlacementContext targetContext = placementContext(target);
        prepared = prepareGeometryAndRaster(target, targetContext.screen);
    }
    if (!prepared) {
        if (canRestore) {
            bool restored = false;
            {
                const QScopedValueRollback<bool> applying(m_applyingGeometry, true);
                m_sourceImage = previous.image;
                m_imageRectInViewport = previous.imageRectInViewport;
                m_resultRect = previous.resultRect;
                m_transientOwner = previous.transientOwner;
                m_pinned = previous.pinned;
                m_preferLeft = previous.preferLeft;
                m_staysOnTop = previous.staysOnTop;
                m_aboveSibling = previous.aboveSibling;
                m_formula = previous.formula;
                m_dimmed = previous.dimmed;
                m_background = previous.background;
                m_statusColor = previous.statusColor;
                m_status = previous.status;
                m_mouseWheelZoomMode = previous.mouseWheelZoomMode;
                setAccessibleDescription(m_status);
                m_targetGeometry = previousTarget;
                m_scalePercent = previousPercent;
                m_placementResultRect = previousPlacementResult;
                m_placementWorkArea = previousWorkArea;
                m_placementScreen = previousScreen;
                m_placementGap = previousGap;
                m_placementPreferLeft = previousPlacementPreferLeft;
                m_userPositioned = previousUserPositioned;
                m_autoPlacementPending = previousAutoPending;
                restoreRaster(previousRaster);
                const PlacementContext oldContext = placementContext(previousTarget);
                restored = oldContext.screen && applyGeometry(previousTarget, oldContext.screen);
                m_scaleLabel->restoreState(previousHint);
            }
            if (restored) {
                synchronizeHintSurface();
                refreshStacking();
                update();
                reconcileNativeGeometry();
                return isVisible();
            }
        }
        hide();
        return false;
    }
    synchronizeHintSurface();
    refreshStacking();
    if (!isVisible()) {
        show();
        // Qt can restore the flag-derived native level while mapping a hidden tool window.
        // Reconcile the pin's current stacking policy after that first mapping too.
        refreshStacking();
    }
    reconcileNativeGeometry();
    return isVisible();
}

void ScreenshotOriginalImagePreviewWindow::clear() {
    m_autoPlacementPending = false;
    endDrag();
    hideScaleReadout();
    m_wheelSteps.reset();
    hide();
    m_sourceImage = {};
    m_formula = false;
    m_dimmed = false;
    m_background = {};
    m_statusColor = {};
    m_status.clear();
    m_viewportImage = {};
    m_nativeSurface.reset();
    m_hintSurface.reset();
    m_nativePublicationPending = false;
    m_imageRectInViewport = {};
    m_resultRect = {};
    m_targetGeometry = {};
    m_placementResultRect = {};
    m_placementWorkArea = {};
    m_placementScreen.clear();
    m_placementGap = 0;
    m_placementPreferLeft = false;
    m_preferLeft = false;
    m_scalePercent = 100;
    m_mouseWheelZoomMode = QStringLiteral("mouse_position");
    m_userPositioned = false;
    m_transientOwner.clear();
    m_aboveSibling.clear();
    m_siblingStackedAbove = false;
    m_cachedSourceKey = 0;
    m_cachedImageRect = {};
    m_cachedPixelSize = {};
    m_cachedOutputDpr = 0;
    if (windowHandle())
        windowHandle()->setTransientParent(nullptr);
    if (m_dpiController)
        m_dpiController->resetBaseline();
}

void ScreenshotOriginalImagePreviewWindow::refreshStacking() {
    if (!windowHandle())
        return;
    QWindow* owner = m_transientOwner ? m_transientOwner->window()->windowHandle() : nullptr;
    if (windowHandle()->transientParent() != owner)
        windowHandle()->setTransientParent(owner);
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        snow_shot::platform::macos::configureWindowCursorUpdates(this);
        snow_shot::platform::configureControlledWindowDragging(this, true);
        if (m_pinned) {
            if (auto* platform = snow_shot::presentation::configurePinnedAuxiliary(this)) {
                static_cast<void>(platform->setInputTransparent(false));
                static_cast<void>(platform->setStaysOnTop(m_staysOnTop));
            }
            if (isVisible() && m_aboveSibling && m_aboveSibling != this &&
                m_aboveSibling->isWindow() && m_aboveSibling->isVisible() &&
                nativeClientRect(m_aboveSibling).intersects(m_targetGeometry)) {
                if (!m_siblingStackedAbove)
                    m_siblingStackedAbove =
                        snow_shot::platform::stackScreenshotWindowBelow(this, m_aboveSibling);
            } else {
                m_siblingStackedAbove = false;
            }
        } else {
            snow_shot::platform::configureScreenshotRecognitionWindow(this);
        }
        return;
    }
#endif
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (usesPhysicalGeometry()) {
        const HWND hwnd = nativeWindow(internalWinId());
        const bool currentlyOnTop = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        if (currentlyOnTop != m_staysOnTop)
            static_cast<void>(SetWindowPos(hwnd, m_staysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
                                           0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        if (isVisible() && m_aboveSibling && m_aboveSibling != this && m_aboveSibling->isWindow() &&
            m_aboveSibling->isVisible() && m_aboveSibling->internalWinId() &&
            nativeClientRect(m_aboveSibling).intersects(m_targetGeometry)) {
            const HWND sibling = nativeWindow(m_aboveSibling->internalWinId());
            const bool siblingOnTop =
                (GetWindowLongPtrW(sibling, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
            bool alreadyAbove = false;
            for (HWND before = GetWindow(hwnd, GW_HWNDPREV); before;
                 before = GetWindow(before, GW_HWNDPREV)) {
                if (before == sibling) {
                    alreadyAbove = true;
                    break;
                }
            }
            if (siblingOnTop == m_staysOnTop && !alreadyAbove)
                static_cast<void>(
                    SetWindowPos(hwnd, sibling, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER));
        }
        return;
    }
#endif
    if (windowFlags().testFlag(Qt::WindowStaysOnTopHint) != m_staysOnTop)
        setWindowFlag(Qt::WindowStaysOnTopHint, m_staysOnTop);
}

bool ScreenshotOriginalImagePreviewWindow::prepareGeometryAndRaster(const QRect& target,
                                                                    QScreen* screen) {
    if (!screen || !target.isValid() || target.isEmpty())
        return false;
    const QScopedValueRollback<bool> applying(m_applyingGeometry, true);
    m_targetGeometry = target;
    // The live window can still belong to another display. Prepare with the destination's
    // DPR before resizing, so allocation limits and pixel dimensions describe the new frame.
    // Suppress native publication throughout the transition. The final refresh reconciles
    // the prepared frame with the actual window DPR, which can differ from the screen DPR.
    return refreshRaster(screen->devicePixelRatio()) && applyGeometry(target, screen) &&
           refreshRaster(devicePixelRatioF());
}

bool ScreenshotOriginalImagePreviewWindow::applyGeometry(const QRect& target, QScreen* screen) {
    const QScopedValueRollback<bool> applying(m_applyingGeometry, true);
    m_targetGeometry = target;
    if (internalWinId() == 0) {
        setScreen(screen);
        // winId() marks this tool WA_NativeWindow and can promote its owner's alien
        // recognition/canvas siblings. Create only the preview's own native surface.
        create();
    }
    if (!usesPhysicalGeometry()) {
        if (windowHandle()->screen() != screen)
            windowHandle()->setScreen(screen);
        if (geometry() != target)
            setGeometry(target);
        return true;
    }
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (!m_dpiController) {
        m_dpiController = new adqt::widgets::AdDpiStableWindowController(this, this);
        m_dpiController->resetBaseline();
        connect(m_dpiController, &adqt::widgets::AdDpiStableWindowController::scaleCommitReady,
                this, [this] {
                    if (isVisible() && (m_formula || !m_sourceImage.isNull()) &&
                        m_targetGeometry.isValid()) {
                        static_cast<void>(applyGeometry(m_targetGeometry, this->screen()));
                        static_cast<void>(refreshRaster(devicePixelRatioF()));
                    }
                });
        connect(m_dpiController, &adqt::widgets::AdDpiStableWindowController::scaleCommitCompleted,
                this, [this] { scheduleRefresh(); });
    }
    windowHandle()->installEventFilter(this);
    const qreal dpr = windowHandle()->devicePixelRatio();
    const QSize logicalSize(coveringLogicalExtent(target.width(), dpr),
                            coveringLogicalExtent(target.height(), dpr));
    if (size() != logicalSize)
        resize(logicalSize);
    if (m_dpiController->stablePhysicalClientSize() != target.size()) {
        m_dpiController->resetBaseline();
        if (!m_dpiController->captureBaseline(dpr, target.size()))
            return false;
    }
    return nativeClientRect(this) == target || applyNativeGeometry(this, target);
#else
    return false;
#endif
}

bool ScreenshotOriginalImagePreviewWindow::refreshRaster(qreal outputDpr) {
    if ((!m_formula && m_sourceImage.isNull()) || !m_resultRect.isValid() ||
        !m_targetGeometry.isValid())
        return false;
    if (!std::isfinite(outputDpr) || outputDpr <= 0)
        return false;
    const qreal geometryScale = usesPhysicalGeometry() ? 1.0 : outputDpr;
    const qreal pixelWidth = m_targetGeometry.width() * geometryScale;
    const qreal pixelHeight = m_targetGeometry.height() * geometryScale;
    if (!std::isfinite(pixelWidth) || !std::isfinite(pixelHeight) ||
        pixelWidth > std::numeric_limits<int>::max() ||
        pixelHeight > std::numeric_limits<int>::max())
        return false;
    const QSize pixelSize(std::max(1, qRound(pixelWidth)), std::max(1, qRound(pixelHeight)));
    if (m_formula && qint64(pixelSize.width()) * pixelSize.height() > 16000000)
        return false;
    const qreal scaleX = qreal(m_targetGeometry.width()) / m_resultRect.width() * geometryScale;
    const qreal scaleY = qreal(m_targetGeometry.height()) / m_resultRect.height() * geometryScale;
    const QRectF pixelImageRect(
        m_imageRectInViewport.x() * scaleX, m_imageRectInViewport.y() * scaleY,
        m_imageRectInViewport.width() * scaleX, m_imageRectInViewport.height() * scaleY);
    if (!finiteRect(pixelImageRect))
        return false;
    if (m_cachedSourceKey == m_sourceImage.cacheKey() && m_cachedImageRect == pixelImageRect &&
        m_cachedPixelSize == pixelSize &&
        (!m_formula || m_status.isEmpty() || qFuzzyCompare(m_cachedOutputDpr, outputDpr))) {
        if (!qFuzzyCompare(m_cachedOutputDpr, outputDpr)) {
            // Moving a Windows preview to another DPI grid only changes its paint metadata;
            // its authoritative physical viewport and image mapping retain the same pixels.
#if defined(Q_OS_WIN) || defined(_WIN32)
            if (m_nativeSurface)
                m_viewportImage = NativeSurface::imageFor(m_nativeSurface, outputDpr);
            else
#endif
                m_viewportImage.setDevicePixelRatio(outputDpr);
            m_cachedOutputDpr = outputDpr;
            updateScaleReadout();
            update();
        }
        return true;
    }
    QImage viewport;
#if defined(Q_OS_WIN) || defined(_WIN32)
    std::shared_ptr<NativeSurface> nativeSurface;
    if (usesPhysicalGeometry()) {
        nativeSurface = std::make_shared<NativeSurface>(pixelSize);
        if (!nativeSurface->isValid())
            return false;
        // The cached QImage paints directly into the DIB used for native publication. Its
        // cleanup holder also keeps snapshots alive after this window replaces or clears it.
        viewport = NativeSurface::imageFor(nativeSurface);
    } else
#endif
        viewport = snowCanvasAllocateImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
    if (viewport.isNull())
        return false;
    viewport.fill(m_formula ? m_background : QColor(Qt::transparent));
    QPainter painter(&viewport);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          pixelImageRect.size() != QSizeF(m_sourceImage.size()));
    if (m_formula) {
        QRectF formulaRect = pixelImageRect;
        QRectF statusRect;
        if (!m_status.isEmpty()) {
            const qreal textScale = usesPhysicalGeometry() ? outputDpr : geometryScale;
            QFont statusFont = font();
            statusFont.setPixelSize(std::max(1, qRound(12 * textScale)));
            painter.setFont(statusFont);
            painter.setPen(m_statusColor);
            const qreal inset =
                std::min({8 * textScale, pixelSize.width() / 8.0, pixelSize.height() / 8.0});
            const QRectF contentRect =
                QRectF(viewport.rect()).adjusted(inset, inset, -inset, -inset);
            if (m_sourceImage.isNull()) {
                statusRect = contentRect;
            } else {
                QRectF formulaArea = contentRect;
                const qreal footerHeight = 36 * textScale;
                // Small selections retain the complete formula and accessible error text.
                // Show a footer only when it leaves room for a formula line and a gap.
                if (contentRect.width() >= 4 * statusFont.pixelSize() &&
                    contentRect.height() >= footerHeight + statusFont.pixelSize() + inset) {
                    statusRect = contentRect;
                    statusRect.setTop(statusRect.bottom() - footerHeight);
                    formulaArea.setBottom(statusRect.top() - inset);
                }
                QSizeF formulaSize = pixelImageRect.size();
                if (formulaSize.width() > formulaArea.width() ||
                    formulaSize.height() > formulaArea.height())
                    formulaSize.scale(formulaArea.size(), Qt::KeepAspectRatio);
                formulaRect = QRectF(formulaArea.center() -
                                         QPointF(formulaSize.width() / 2, formulaSize.height() / 2),
                                     formulaSize);
            }
        }
        if (!m_sourceImage.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                  formulaRect.size() != QSizeF(m_sourceImage.size()));
            painter.setOpacity(m_dimmed ? 0.35 : 1.0);
            painter.drawImage(formulaRect, m_sourceImage);
            painter.setOpacity(1.0);
        }
        if (!statusRect.isEmpty())
            painter.drawText(statusRect, Qt::AlignCenter | Qt::TextWordWrap, m_status);
    } else {
        paintExposedScreenshotImage(painter, pixelImageRect, m_sourceImage,
                                    QRectF(QPointF(), QSizeF(m_sourceImage.size())),
                                    QRegion(viewport.rect()));
    }
    painter.end();
    viewport.setDevicePixelRatio(outputDpr);
    m_viewportImage = std::move(viewport);
#if defined(Q_OS_WIN) || defined(_WIN32)
    m_nativeSurface = std::move(nativeSurface);
#endif
    m_cachedSourceKey = m_sourceImage.cacheKey();
    m_cachedImageRect = pixelImageRect;
    m_cachedPixelSize = pixelSize;
    m_cachedOutputDpr = outputDpr;
    ++m_rasterGeneration;
    m_nativePublicationPending = true;
    updateScaleReadout();
    update();
    return true;
}

void ScreenshotOriginalImagePreviewWindow::hideEvent(QHideEvent* event) {
    m_wheelSteps.reset();
    hideScaleReadout();
    endDrag();
    QWidget::hideEvent(event);
    if (!m_presenting)
        emit hidden();
}

bool ScreenshotOriginalImagePreviewWindow::beginDrag(const QPointF& cursor) {
    if (!isVisible() || !m_targetGeometry.isValid())
        return false;
    if (usesPhysicalGeometry() && (!m_dpiController || !m_dpiController->beginPhysicalDrag(cursor)))
        return false;
    m_dragOffset = cursor - QPointF(m_targetGeometry.topLeft());
    m_dragging = true;
    // Keep receiving movement if the release was lost and no button is held anymore.
    setMouseTracking(true);
    setCursor(Qt::ClosedHandCursor);
    return true;
}

void ScreenshotOriginalImagePreviewWindow::moveDrag(const QPointF& cursor) {
    if (!m_dragging || (cursor - m_dragOffset).toPoint() == m_targetGeometry.topLeft())
        return;
    if (usesPhysicalGeometry()) {
        if (!m_dpiController->moveForPhysicalCursor(cursor)) {
            endDrag();
            return;
        }
        m_targetGeometry.moveTopLeft(nativeClientRect(this).topLeft());
    } else {
        const QRect target((cursor - m_dragOffset).toPoint(), m_targetGeometry.size());
        const PlacementContext context = placementContext(target);
        if (!context.screen || !applyGeometry(target, context.screen)) {
            endDrag();
            return;
        }
    }
    m_userPositioned = true;
    m_siblingStackedAbove = false;
    refreshStacking();
    static_cast<void>(refreshRaster(devicePixelRatioF()));
    reconcileNativeGeometry();
}

void ScreenshotOriginalImagePreviewWindow::endDrag() {
    if (m_dpiController)
        m_dpiController->endPhysicalDrag();
    m_dragging = false;
    m_dragOffset = {};
    setMouseTracking(false);
    setCursor(Qt::OpenHandCursor);
    if (m_autoPlacementPending)
        scheduleRefresh();
}

bool ScreenshotOriginalImagePreviewWindow::applyWheelScaleSteps(int steps, const QPointF& cursor) {
    if (!isVisible() || m_dragging || steps == 0 || !m_targetGeometry.isValid())
        return false;
    const int displayedPercent = qRound(m_scalePercent);
    const qreal level = steps > 0 ? std::floor(qreal(displayedPercent) / kWheelScaleStep)
                                  : std::ceil(qreal(displayedPercent) / kWheelScaleStep);
    const qreal percent = std::clamp(level * kWheelScaleStep + qreal(steps) * kWheelScaleStep,
                                     kMinimumScalePercent, kMaximumScalePercent);
    if (qRound(percent) == displayedPercent)
        return false;
    const QSize targetSize =
        screenshot_pinned_resize_geometry::scaledSize(m_resultRect.size(), percent / 100);
    const QRect target = screenshot_pinned_resize_geometry::anchoredScaleRect(
        m_targetGeometry, targetSize,
        screenshot_pinned_resize_geometry::scaleAnchorFromSetting(m_mouseWheelZoomMode), cursor);
    const PlacementContext context = placementContext(target);
    if (!context.screen)
        return false;

    const QRect oldTarget = m_targetGeometry;
    const qreal oldPercent = m_scalePercent;
    const RasterState oldRaster = captureRaster();
    const auto oldHint = m_scaleLabel->captureState();
    bool applied = false;
    {
        const QScopedValueRollback<bool> applying(m_applyingGeometry, true);
        m_scalePercent = percent;
        applied = prepareGeometryAndRaster(target, context.screen);
        if (!applied) {
            m_targetGeometry = oldTarget;
            m_scalePercent = oldPercent;
            restoreRaster(oldRaster);
            const PlacementContext oldContext = placementContext(oldTarget);
            if (oldContext.screen)
                static_cast<void>(applyGeometry(oldTarget, oldContext.screen));
            m_scaleLabel->restoreState(oldHint);
        }
    }
    if (!applied) {
        synchronizeHintSurface();
        return false;
    }
    m_userPositioned = true;
    m_siblingStackedAbove = false;
    refreshStacking();
    showScaleReadout();
    reconcileNativeGeometry();
    return true;
}

void ScreenshotOriginalImagePreviewWindow::wheelEvent(QWheelEvent* event) {
    if (m_dragging) {
        m_wheelSteps.reset();
        event->accept();
        return;
    }
    if (event->pixelDelta().y() == 0 && event->angleDelta().y() == 0 &&
        event->phase() == Qt::NoScrollPhase) {
        QWidget::wheelEvent(event);
        return;
    }
    const int steps = m_wheelSteps.consume(*event);
    if (const auto cursor = wheelCursorPosition(*event); cursor && steps != 0)
        static_cast<void>(applyWheelScaleSteps(steps, *cursor));
    event->accept();
}

void ScreenshotOriginalImagePreviewWindow::showScaleReadout() {
    {
        const QScopedValueRollback<bool> updating(m_updatingScaleReadout, true);
        m_scaleLabel->setMessage(CanvasStatusReadout::Message::Scale, qRound(m_scalePercent));
        m_scaleLabel->layoutIn(scaleReadoutAnchor());
        m_scaleLabel->showTransient();
    }
    synchronizeHintSurface();
}

void ScreenshotOriginalImagePreviewWindow::hideScaleReadout() {
    const QScopedValueRollback<bool> updating(m_updatingScaleReadout, true);
    m_scaleLabel->dismiss();
    m_hintSurface.reset();
    m_nativePublicationPending = true;
    update();
}

void ScreenshotOriginalImagePreviewWindow::updateScaleReadout() {
    {
        const QScopedValueRollback<bool> updating(m_updatingScaleReadout, true);
        m_scaleLabel->setMessage(CanvasStatusReadout::Message::Scale, qRound(m_scalePercent));
        m_scaleLabel->layoutIn(scaleReadoutAnchor());
    }
    synchronizeHintSurface();
}

QRectF ScreenshotOriginalImagePreviewWindow::scaleReadoutAnchor() const {
    if (usesPhysicalGeometry() && m_targetGeometry.isValid())
        return QRectF(QPointF(), QSizeF(m_targetGeometry.size()) / devicePixelRatioF());
    return QRectF(rect());
}

void ScreenshotOriginalImagePreviewWindow::synchronizeHintSurface() {
    // Geometry/raster transactions restore the hint and pixels together. Compose only once
    // the owner has committed its final surface, including after a rollback.
    if (m_applyingGeometry || m_updatingScaleReadout)
        return;
    const QScopedValueRollback<bool> updating(m_updatingScaleReadout, true);
    if (!refreshHintSurface())
        hideScaleReadout();
    update();
}

bool ScreenshotOriginalImagePreviewWindow::refreshHintSurface() {
    m_hintSurface.reset();
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (usesPhysicalGeometry() && !m_scaleLabel->isHidden() && m_nativeSurface) {
        auto surface = std::make_shared<NativeSurface>(m_viewportImage.size());
        if (!surface->isValid())
            return false;
        QImage composed = NativeSurface::imageFor(surface, m_viewportImage.devicePixelRatio());
        QPainter painter(&composed);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(QPointF(), m_viewportImage);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        m_scaleLabel->paintOverlay(painter);
        painter.end();
        m_hintSurface = std::move(surface);
    }
#endif
    m_nativePublicationPending = true;
    return true;
}

void ScreenshotOriginalImagePreviewWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (const auto cursor = dragCursorPosition(*event); cursor && beginDrag(*cursor)) {
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ScreenshotOriginalImagePreviewWindow::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        const auto cursor = dragCursorPosition(*event);
        if (event->buttons().testFlag(Qt::LeftButton) && cursor)
            moveDrag(*cursor);
        else
            endDrag();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ScreenshotOriginalImagePreviewWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        if (const auto cursor = dragCursorPosition(*event))
            moveDrag(*cursor);
        endDrag();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ScreenshotOriginalImagePreviewWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    if (!m_viewportImage.isNull())
        painter.drawImage(QPointF(), m_viewportImage);
}

void ScreenshotOriginalImagePreviewWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (usesPhysicalGeometry() && m_targetGeometry.isValid() &&
        (m_formula || !m_sourceImage.isNull()) && !m_adjustingPaintSurface) {
        // QWidgetWindow rounds native extents onto its logical grid before forwarding this
        // event. Keep the paint surface large enough for every physical row and column; the
        // native window itself remains pinned to the exact recognition viewport rectangle.
        const qreal dpr = devicePixelRatioF();
        const QSize coveringSize(coveringLogicalExtent(m_targetGeometry.width(), dpr),
                                 coveringLogicalExtent(m_targetGeometry.height(), dpr));
        if (size() != coveringSize) {
            const QScopedValueRollback<bool> adjusting(m_adjustingPaintSurface, true);
            resize(coveringSize);
        }
    }
#endif
    updateScaleReadout();
}

bool ScreenshotOriginalImagePreviewWindow::event(QEvent* event) {
    if (event && event->type() == QEvent::UngrabMouse)
        endDrag();
    if (event && event->type() == QEvent::WindowDeactivate)
        m_wheelSteps.reset();
    const bool handled = QWidget::event(event);
    if (event && event->type() == QEvent::UpdateRequest)
        reconcileNativeGeometry();
    if (event && (event->type() == QEvent::Hide || event->type() == QEvent::WinIdChange))
        m_siblingStackedAbove = false;
    if (event &&
        (event->type() == QEvent::DevicePixelRatioChange ||
         event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::WinIdChange) &&
        !m_applyingGeometry)
        scheduleRefresh();
    return handled;
}

bool ScreenshotOriginalImagePreviewWindow::eventFilter(QObject* watched, QEvent* event) {
    if (usesPhysicalGeometry() && watched == windowHandle() && event &&
        (event->type() == QEvent::Expose || event->type() == QEvent::Resize) &&
        !m_forwardingWindowEvent) {
        // QWidgetWindow's expose and resize handlers can flush the backing store directly,
        // bypassing the QWidget update request. Complete that dispatch once, then restore the
        // native rectangle before the event returns, just as for a regular repaint below.
        const QPointer<ScreenshotOriginalImagePreviewWindow> alive(this);
        m_forwardingWindowEvent = true;
        QCoreApplication::sendEvent(watched, event);
        if (alive) {
            m_forwardingWindowEvent = false;
            reconcileNativeGeometry();
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ScreenshotOriginalImagePreviewWindow::reconcileNativeGeometry() {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (usesPhysicalGeometry() && isVisible() && (m_formula || !m_sourceImage.isNull()) &&
        m_targetGeometry.isValid() && !m_applyingGeometry && m_nativeSurface &&
        (nativeClientRect(this) != m_targetGeometry || m_nativePublicationPending ||
         m_hintSurface)) {
        // Qt publishes translucent frames using an integer logical frame rectangle, which can
        // clip a final physical row or column at fractional DPI. Publish the single cached DIB
        // with the authoritative physical extent instead of growing a cropped layered surface.
        const QScopedValueRollback<bool> applying(m_applyingGeometry, true);
        const auto& surface = m_hintSurface ? m_hintSurface : m_nativeSurface;
        if (surface->size != m_targetGeometry.size())
            return;
        if (!surface->publish(internalWinId(), m_targetGeometry))
            hide();
        else
            m_nativePublicationPending = false;
    }
#endif
}

void ScreenshotOriginalImagePreviewWindow::scheduleRefresh() {
    if (m_refreshPending || !isVisible() || (!m_formula && m_sourceImage.isNull()))
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        // A drag or a parent hide may suppress the preview before this queued update runs.
        if (!isVisible() || (!m_formula && m_sourceImage.isNull()))
            return;
        static_cast<void>(
            present({m_sourceImage, m_imageRectInViewport, m_resultRect, m_transientOwner.data(),
                     m_pinned, m_staysOnTop, m_aboveSibling.data(), m_formula, m_dimmed,
                     m_background, m_statusColor, m_status, m_mouseWheelZoomMode, m_preferLeft}));
    });
}

bool ScreenshotOriginalImagePreviewWindow::nativeEvent(const QByteArray& eventType, void* message,
                                                       qintptr* result) {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (usesPhysicalGeometry() && message && m_targetGeometry.isValid() &&
        (eventType == QByteArrayLiteral("windows_generic_MSG") ||
         eventType == QByteArrayLiteral("windows_dispatcher_MSG"))) {
        const auto* native = static_cast<MSG*>(message);
        if (native->hwnd == nativeWindow(internalWinId())) {
            if (adqt::widgets::AdDpiStableWindowController::enforceStablePhysicalSizeForMessage(
                    message, internalWinId(), m_targetGeometry.size(), true, result))
                return true;
            if (native->message == WM_WINDOWPOSCHANGING && native->lParam) {
                auto* position = reinterpret_cast<WINDOWPOS*>(native->lParam);
                if (!(position->flags & SWP_NOMOVE)) {
                    if (m_dpiController && m_dpiController->physicalDragActive() &&
                        !m_applyingGeometry) {
                        // The controller owns physical cursor mapping and destination-grid
                        // snapping. Adopt its position before a repaint can reconcile it.
                        m_targetGeometry.moveTopLeft(QPoint(position->x, position->y));
                    } else {
                        position->x = m_targetGeometry.x();
                        position->y = m_targetGeometry.y();
                    }
                }
            }
            if (native->message == WM_WINDOWPOSCHANGED) {
                // Retain the chosen position and exact viewport extent as native updates settle.
                reconcileNativeGeometry();
            }
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}
