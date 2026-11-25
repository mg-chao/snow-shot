#include "screenshotocrlinegeometry.h"

#include <QLineF>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <optional>

namespace snow_shot::ocr {
namespace {
constexpr qreal kVerticalAspectRatio = 1.5;

struct DetectedLine {
    qsizetype index;
    QPointF center;
    qreal length;
    qreal thickness;
    qreal drift;
    qreal angle;
    bool vertical;

    bool canAlign() const {
        return drift != 0 && std::abs(angle) <= 5.0 &&
               std::abs(drift) <= std::clamp(0.2 * thickness, 2.0, 5.0);
    }
};

std::optional<DetectedLine> measureLine(qsizetype index, const QPolygonF& quad,
                                        const QSize& imageSize) {
    for (const auto& point : quad) {
        // A clipped detector rectangle can have a different angle on each edge.
        if (!(point.x() > 0 && point.x() < imageSize.width() && point.y() > 0 &&
              point.y() < imageSize.height()))
            return {};
    }
    const QPointF top = quad[1] - quad[0];
    const QPointF bottom = quad[2] - quad[3];
    const QPointF right = (top + bottom) / 2;
    const QPointF down = (quad[2] + quad[3] - quad[0] - quad[1]) / 2;
    const qreal width = std::hypot(right.x(), right.y());
    const qreal height = std::hypot(down.x(), down.y());
    // Use the OCR tall-box convention before measuring top-to-bottom drift in source pixels.
    const bool vertical = height >= kVerticalAspectRatio * width;
    const qreal length = vertical ? height : width;
    const qreal thickness = vertical ? width : height;
    // Ignore square glyphs and perspective geometry. Allow integer rounding of corners.
    if (right.x() <= 0 || down.y() <= 0 || thickness < 1 || length < 1.2 * thickness ||
        QLineF(top, bottom).length() > 2 || std::abs(QPointF::dotProduct(right, down)) > 2 * length)
        return {};
    const qreal drift = vertical ? down.x() : right.y();
    const qreal advance = vertical ? down.y() : right.x();
    // On a long skewed column, ordering detector corners by x can make the top edge
    // run down the page. Such a box cannot supply evidence for the other axis.
    if (std::abs(drift) >= advance)
        return {};
    return DetectedLine{index, (quad[0] + quad[1] + quad[2] + quad[3]) / 4,   length,  thickness,
                        drift, qRadiansToDegrees(std::atan2(drift, advance)), vertical};
}

struct Votes {
    int aligned = 0;
    int total = 0;

    void count(const DetectedLine& line, int weight = 1) {
        if (std::abs(line.angle) <= 0.6 && std::abs(line.drift) <= 1.0)
            aligned += weight;
        total += weight;
    }

    bool mostlyAligned() const {
        return aligned * 3 >= total * 2;
    }
};

void alignSmallTilts(QVector<ScreenshotOcrLine>& lines, const QVector<DetectedLine>& peers,
                     const QSize& imageSize) {
    if (std::ranges::none_of(peers, &DetectedLine::canAlign))
        return;
    QVector<qreal> thicknesses;
    thicknesses.reserve(peers.size());
    Votes page;
    for (const auto& line : peers) {
        thicknesses.push_back(line.thickness);
        if (line.length >= 3 * line.thickness)
            page.count(line);
    }

    // Estimate the neighborhood size independently for each writing direction.
    auto middle = thicknesses.begin() + thicknesses.size() / 2;
    std::nth_element(thicknesses.begin(), middle, thicknesses.end());
    const qreal cellSize = std::max(16.0, 3 * *middle);
    const auto cellFor = [cellSize](QPointF center) {
        return QPoint(static_cast<int>(center.x() / cellSize),
                      static_cast<int>(center.y() / cellSize));
    };
    QHash<QPoint, Votes> cells;
    for (const auto& line : peers)
        if (line.length >= 2 * line.thickness)
            cells[cellFor(line.center)].count(line);

    for (const auto& line : peers) {
        if (!line.canAlign())
            continue;
        const QPoint cell = cellFor(line.center);
        Votes local;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const auto votes = cells.value(cell + QPoint(x, y));
                local.aligned += votes.aligned;
                local.total += votes.total;
            }
        }
        // Exclude the detection's own vote. Prefer local text before longer lines elsewhere.
        if (line.length >= 2 * line.thickness)
            local.count(line, -1);
        if (local.total == 0) {
            local = page;
            if (line.length >= 3 * line.thickness)
                local.count(line, -1);
            if (local.total < 3)
                local = {};
        }
        if (local.total == 0) {
            // A narrow fragment such as a wrapped two-letter suffix needs independent support.
            if (std::abs(line.drift) > 2.0 || line.length < 1.5 * line.thickness)
                continue;
        } else if (!local.mostlyAligned()) {
            continue;
        }
        QSizeF size(line.length, line.thickness);
        if (line.vertical)
            size.transpose();
        QRectF level(QPointF(), size);
        level.moveCenter(line.center);
        if (QRectF(QPointF(), QSizeF(imageSize)).contains(level))
            lines[line.index].quad = {level.topLeft(), level.topRight(), level.bottomRight(),
                                      level.bottomLeft()};
    }
}
} // namespace

void prepareDetectedLineGeometry(QVector<ScreenshotOcrLine>& lines, const QSize& imageSize,
                                 const QRectF& canvasRect) {
    QVector<DetectedLine> rows, columns;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (const auto measured = measureLine(i, lines.at(i).quad, imageSize))
            (measured->vertical ? columns : rows).push_back(*measured);
    }
    alignSmallTilts(lines, rows, imageSize);
    alignSmallTilts(lines, columns, imageSize);
    const qreal scaleX = canvasRect.width() / imageSize.width();
    const qreal scaleY = canvasRect.height() / imageSize.height();
    for (auto& line : lines) {
        auto& quad = line.quad;
        for (auto& point : quad)
            point = {canvasRect.left() + point.x() * scaleX, canvasRect.top() + point.y() * scaleY};
        const qreal width =
            std::max(QLineF(quad[0], quad[1]).length(), QLineF(quad[3], quad[2]).length());
        const qreal height =
            std::max(QLineF(quad[0], quad[3]).length(), QLineF(quad[1], quad[2]).length());
        line.direction = height >= width * kVerticalAspectRatio
                             ? ScreenshotOcrTextDirection::Vertical
                             : ScreenshotOcrTextDirection::Horizontal;
    }
}
} // namespace snow_shot::ocr
