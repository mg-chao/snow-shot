use super::*;
use snow_draw_engine_document::{FillStyle, StrokeStyle};

pub(crate) fn rectangle_to_draw_rect(rect: &RectangleData) -> DrawRect {
    DrawRect::from_center(rect.center, rect.width, rect.height)
}

pub(crate) fn rectangle_from_draw_rect(
    bounds: DrawRect,
    template: &RectangleData,
) -> RectangleData {
    let width = (bounds.max_x - bounds.min_x).max(MIN_RECT_SIZE);
    let height = (bounds.max_y - bounds.min_y).max(MIN_RECT_SIZE);
    RectangleData {
        rectangle_kind: template.rectangle_kind,
        highlight_shape: template.highlight_shape,
        center: Point::new(
            f64::midpoint(bounds.min_x, bounds.max_x),
            f64::midpoint(bounds.min_y, bounds.max_y),
        ),
        width,
        height,
        rotation: template.rotation,
        fill: template.fill,
        fill_style: template.fill_style,
        stroke: template.stroke,
        stroke_width: template.stroke_width,
        stroke_style: template.stroke_style,
        corner_radii: normalize_corner_radii(width, height, template.corner_radii),
        opacity: template.opacity,
    }
}

pub(crate) fn rotated_rectangle_aabb(rect: &RectangleData) -> DrawRect {
    if rect.rotation.abs() <= f64::EPSILON {
        return rectangle_to_draw_rect(rect);
    }

    let half_width = rect.width.abs() / 2.0;
    let half_height = rect.height.abs() / 2.0;
    let cos_theta = rect.rotation.cos().abs();
    let sin_theta = rect.rotation.sin().abs();
    let x_extent = half_width * cos_theta + half_height * sin_theta;
    let y_extent = half_width * sin_theta + half_height * cos_theta;

    DrawRect::new(
        rect.center.x - x_extent,
        rect.center.y - y_extent,
        rect.center.x + x_extent,
        rect.center.y + y_extent,
    )
}

pub(crate) fn selection_bounds_to_draw_rect(bounds: &SelectionBounds) -> DrawRect {
    if bounds.rotation.abs() <= f64::EPSILON {
        return DrawRect::from_center(bounds.center, bounds.width, bounds.height);
    }

    let half_width = bounds.width.abs() / 2.0;
    let half_height = bounds.height.abs() / 2.0;
    let cos_theta = bounds.rotation.cos().abs();
    let sin_theta = bounds.rotation.sin().abs();
    let x_extent = half_width * cos_theta + half_height * sin_theta;
    let y_extent = half_width * sin_theta + half_height * cos_theta;

    DrawRect::new(
        bounds.center.x - x_extent,
        bounds.center.y - y_extent,
        bounds.center.x + x_extent,
        bounds.center.y + y_extent,
    )
}

pub(crate) fn snap_rect_to_grid_min_corner(rect: DrawRect, grid_size: f64) -> DrawRect {
    if !(grid_size.is_finite() && grid_size > 0.0) {
        return rect;
    }

    let snapped_min_x = (rect.min_x / grid_size).round() * grid_size;
    let snapped_min_y = (rect.min_y / grid_size).round() * grid_size;
    rect.translate(Point::new(
        snapped_min_x - rect.min_x,
        snapped_min_y - rect.min_y,
    ))
}

pub(crate) fn resize_snap_anchors_for_sign(sign: f64) -> Vec<SnapAxisAnchor> {
    if sign < 0.0 {
        vec![SnapAxisAnchor::Start]
    } else if sign > 0.0 {
        vec![SnapAxisAnchor::End]
    } else {
        Vec::new()
    }
}

pub(crate) fn square_constrained_rectangle_end(start: Point<f64>, end: Point<f64>) -> Point<f64> {
    let dx = end.x - start.x;
    let dy = end.y - start.y;
    let side = dx.abs().max(dy.abs());
    if side <= 0.0 {
        return end;
    }

    Point {
        x: start.x + constraint_axis_sign(dx, dy) * side,
        y: start.y + constraint_axis_sign(dy, dx) * side,
    }
}

pub(crate) fn constraint_axis_sign(primary_delta: f64, fallback_delta: f64) -> f64 {
    if primary_delta < 0.0 {
        -1.0
    } else if primary_delta > 0.0 {
        1.0
    } else if fallback_delta < 0.0 {
        -1.0
    } else {
        1.0
    }
}

pub(crate) fn selection_marquee_rectangle(
    start: Point<f64>,
    end: Point<f64>,
    zoom: f64,
) -> Option<RectangleData> {
    let bounds = axis_aligned_bounds(start, end)?;
    Some(RectangleData {
        rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
        highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
        center: Point {
            x: f64::midpoint(bounds.left, bounds.right),
            y: f64::midpoint(bounds.top, bounds.bottom),
        },
        width: bounds.right - bounds.left,
        height: bounds.bottom - bounds.top,
        rotation: 0.0,
        fill: SELECTION_MARQUEE_FILL,
        fill_style: FillStyle::Solid,
        stroke: SELECTION_BLUE,
        stroke_width: 1.0 / zoom.max(0.0001),
        stroke_style: StrokeStyle::Solid,
        corner_radii: CornerRadii::default(),
        opacity: 1.0,
    })
}

pub(crate) fn selection_outline_for_rect(rect: &RectangleData, zoom: f64) -> RectangleData {
    selection_outline_bounds(
        &SelectionBounds {
            center: rect.center,
            width: rect.width,
            height: rect.height,
            rotation: rect.rotation,
        },
        zoom,
    )
}

#[cfg(any())]
pub(crate) fn selection_outline(rect: &RectangleData, zoom: f64) -> RectangleData {
    selection_outline_for_rect(rect, zoom)
}

pub(crate) fn selection_outline_bounds(bounds: &SelectionBounds, zoom: f64) -> RectangleData {
    let padding = selection_frame_padding(zoom);
    RectangleData {
        rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
        highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
        center: bounds.center,
        width: bounds.width + padding * 2.0,
        height: bounds.height + padding * 2.0,
        rotation: bounds.rotation,
        fill: ColorRgba8 {
            r: 0,
            g: 0,
            b: 0,
            a: 0,
        },
        fill_style: FillStyle::Solid,
        stroke: ColorRgba8 {
            r: 0x40,
            g: 0x96,
            b: 0xff,
            a: 0xff,
        },
        stroke_width: 1.0 / zoom.max(0.0001),
        stroke_style: StrokeStyle::Solid,
        corner_radii: CornerRadii::default(),
        opacity: 1.0,
    }
}
