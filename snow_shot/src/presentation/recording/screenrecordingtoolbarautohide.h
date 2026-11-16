// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QRect>
#include <QRegion>
#include <algorithm>
#include <cmath>

namespace snow_shot::presentation::recording::toolbar_auto_hide {

enum class Side { None, Left, Right };
inline constexpr int edgeThreshold = 16;

inline Side sideForRow(const QRect& row, const QRect& bounds, Side previous = Side::None) {
    if (row.isEmpty() || bounds.isEmpty() || !row.intersects(bounds))
        return Side::None;
    // A negative inward gap means the toolbar has crossed that edge. It remains
    // eligible, with the greater overflow taking precedence over the other edge.
    const int left = row.left() - bounds.left();
    const int right = bounds.right() - row.right();
    if (left > edgeThreshold && right > edgeThreshold)
        return Side::None;
    if (left == right)
        return previous == Side::None ? Side::Right : previous;
    return left < right ? Side::Left : Side::Right;
}

inline QRect tabGeometry(const QRect& row, const QRect& bounds, Side side, qreal scale) {
    if (row.isEmpty() || bounds.isEmpty() || side == Side::None)
        return {};
    if (!std::isfinite(scale) || scale <= 0)
        scale = 1.0;
    const QSize size(std::clamp(qRound(20 * scale), 1, bounds.width()),
                     std::min(row.height(), bounds.height()));
    const int x = side == Side::Left ? bounds.left() : bounds.right() + 1 - size.width();
    const int y = std::clamp(row.top() + (row.height() - size.height()) / 2, bounds.top(),
                             bounds.bottom() + 1 - size.height());
    return {QPoint(x, y), size};
}

inline QRegion transferRegion(const QRect& row, const QRect& tab, Side side) {
    if (row.isEmpty() || tab.isEmpty() || side == Side::None)
        return {};
    const int rowEdge = side == Side::Left ? row.left() : row.right();
    const int left = std::min(tab.left(), rowEdge);
    const int right = std::max(tab.right(), rowEdge);
    const int top = std::min(tab.top(), row.bottom());
    const int bottom = std::max(tab.bottom(), row.top());
    return QRegion(QRect(QPoint(left, top), QPoint(right, bottom)));
}

} // namespace snow_shot::presentation::recording::toolbar_auto_hide
