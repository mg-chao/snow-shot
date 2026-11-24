#include "snow_draw_engine_qt/snow_canvas_snapping.h"
#include "snow_canvas_renderer.h"
#include "snow_canvas_type_conversions.h"
#include <QGuiApplication>
#include <QPainter>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void guideStrokesCoverWholeDevicePixels() {
    SnowCanvasSnapConfig config;
    config.markerSize = 0.0;
    for (const qreal dpr : {1.0, 1.25, 1.5, 1.75, 2.0}) {
        for (const qreal pan : {0.0, 0.25, 0.5, 0.75}) {
            const QTransform view(1.25, 0, 0, 1.25, pan, pan);
            for (const bool vertical : {false, true}) {
                const QPointF along = vertical ? QPointF(0, 1) : QPointF(1, 0);
                const QPoint across = vertical ? QPoint(1, 0) : QPoint(0, 1);
                for (const bool gap : {false, true}) {
                    SnowCanvasSnapGuide guide;
                    guide.vertical = vertical;
                    guide.gap = gap;
                    guide.start = {8, 8};
                    guide.end = guide.start + along * 48;
                    QImage image(QSize(80, 80) * dpr, QImage::Format_ARGB32_Premultiplied);
                    image.setDevicePixelRatio(dpr);
                    image.fill(Qt::transparent);
                    QTransform device;
                    {
                        QPainter painter(&image);
                        painter.translate(0.25, 0.5);
                        device = painter.deviceTransform();
                        snowCanvasPaintSnapGuides(painter, {guide}, view,
                                                  QRegion(QRect(0, 0, 80, 80)), config);
                    }
                    const QPointF start = view.map(guide.start);
                    for (const qreal distance : {2.0, 6.0, 10.0, 14.0}) {
                        const QPoint center = device.map(start + along * distance).toPoint();
                        int covered = 0;
                        for (int offset = -3; offset <= 3; ++offset) {
                            const int alpha = qAlpha(image.pixel(center + across * offset));
                            require(alpha == 0 || alpha == 255,
                                    "guide stroke edges must fall on device pixel boundaries");
                            covered += alpha != 0;
                        }
                        const bool dashGap = gap && (distance == 6.0 || distance == 14.0);
                        require(covered == (dashGap ? 0 : qRound(dpr)),
                                "point and gap guides must have a uniform device pixel width");
                    }
                }
            }
        }
    }
}

void guideRenderingMatchesCanvas() {
    SnowCanvasSnapConfig config;
    config.showGapSize = true;
    const QVector<QRectF> references{{20, 30, 40, 40}, {100, 30, 40, 40}};
    for (const QRectF target : {QRectF(178, 32, 40, 40), QRectF(22, 112, 40, 40)}) {
        const auto snapped = snowCanvasSnapMove(target, references, 1.0, config);
        require(snapped && !snapped->guides.isEmpty(), "fixture must produce guides");
        const SnowSnapRect rawTarget{target.x(), target.y(), target.right(), target.bottom()};
        const SnowSnapRect rawReferences[]{{20, 30, 60, 70}, {100, 30, 140, 70}};
        const auto style = snow_canvas_types::toEngineSnapConfig(config);
        SnowArrowPoint correction{};
        std::size_t count = 0;
        SnowMoveSnap* raw = nullptr;
        require(snow_snap_move(&rawTarget, rawReferences, 2, &style, 1.0, nullptr, &raw) == SNOW_OK,
                "compute raw snap once");
        const std::unique_ptr<SnowMoveSnap, decltype(&snow_move_snap_free)> owner(
            raw, snow_move_snap_free);
        const SnowOverlayDisplayItem* items = nullptr;
        require(snow_move_snap_read(owner.get(), &correction, &items, &count) == SNOW_OK,
                "read owned raw guides");
        for (qreal scale : {0.5, 1.0, 1.5}) {
            for (qreal dpr : {1.0, 1.5, 2.0}) {
                const QTransform transform(scale, 0, 0, scale, 20, 25);
                QImage host(QSize(400, 300) * dpr, QImage::Format_ARGB32_Premultiplied);
                host.setDevicePixelRatio(dpr);
                host.fill(Qt::white);
                QImage engine = host;
                const QRegion exposed(QRect(0, 0, 400, 300));
                {
                    QPainter painter(&host);
                    painter.setRenderHint(QPainter::Antialiasing);
                    snowCanvasPaintSnapGuides(painter, snapped->guides, transform, exposed, config);
                }
                {
                    QPainter painter(&engine);
                    painter.setRenderHint(QPainter::Antialiasing);
                    OverlayDisplayInfo view;
                    view.camera_zoom = scale;
                    view.camera_center_x = -20 / scale;
                    view.camera_center_y = -25 / scale;
                    snow_canvas_renderer::renderOverlayItems(
                        painter, view, items, static_cast<std::uint32_t>(count), exposed);
                }
                require(host == engine, "host guides must match rectangle guide pixels");
                const auto damage = snowCanvasSnapGuideDamage(snapped->guides, transform, config);
                {
                    QPainter painter(&host);
                    painter.setClipRegion(damage);
                    painter.fillRect(QRect(0, 0, 400, 300), Qt::white);
                }
                engine.fill(Qt::white);
                require(host == engine, "guide damage must erase lines, markers and labels");
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    const auto empty = snowCanvasSnapMove(QRectF(0, 0, 40, 40), {}, 1.0);
    require(empty && empty->correction.isNull() && empty->guides.isEmpty(), "empty targets");
    require(
        !snowCanvasSnapMove({}, {}, 1.0) && !snowCanvasSnapMove(QRectF(0, 0, 40, 40), {}, 0.0) &&
            !snowCanvasSnapMove(QRectF(0, 0, 40, 40),
                                {QRectF(std::numeric_limits<qreal>::quiet_NaN(), 0, 40, 40)}, 1),
        "invalid inputs fail without a snap");
    guideStrokesCoverWholeDevicePixels();
    guideRenderingMatchesCanvas();
    std::cout << "Snapping bridge and guide rendering tests passed\n";
}
