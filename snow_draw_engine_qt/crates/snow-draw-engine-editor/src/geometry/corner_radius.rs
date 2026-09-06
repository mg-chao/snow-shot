use super::*;

pub(crate) fn rect_corner_point(width: f64, height: f64, corner: RectCorner) -> Point<f64> {
    Point {
        x: corner.x_sign() * width / 2.0,
        y: corner.y_sign() * height / 2.0,
    }
}

pub(crate) fn corner_radius_handle_local_point(
    rect: &RectangleData,
    zoom: f64,
    corner: RectCorner,
) -> Point<f64> {
    let radius = corner_radius(rect.corner_radii, corner);
    let max_radius = max_corner_radius_for(rect, corner);
    let inset = corner_radius_handle_inset(rect, zoom, radius, max_radius);
    Point {
        x: corner.x_sign() * (rect.width / 2.0 - inset),
        y: corner.y_sign() * (rect.height / 2.0 - inset),
    }
}

pub(crate) fn corner_radius_handle_inset(
    rect: &RectangleData,
    zoom: f64,
    radius: f64,
    max_radius: f64,
) -> f64 {
    let min_inset = corner_radius_handle_min_inset(rect, zoom);
    let max_inset = corner_radius_handle_max_inset(rect, zoom);
    if max_radius <= f64::EPSILON || max_inset <= min_inset {
        return min_inset;
    }

    let t = (radius / max_radius).clamp(0.0, 1.0);
    min_inset + (max_inset - min_inset) * t
}

pub(crate) fn corner_radius_for_handle_inset(
    rect: &RectangleData,
    zoom: f64,
    inset: f64,
    max_radius: f64,
) -> f64 {
    let min_inset = corner_radius_handle_min_inset(rect, zoom);
    let max_inset = corner_radius_handle_max_inset(rect, zoom);
    if max_radius <= f64::EPSILON || max_inset <= min_inset {
        return 0.0;
    }

    let clamped_inset = inset.clamp(min_inset, max_inset);
    let t = (clamped_inset - min_inset) / (max_inset - min_inset);
    max_radius * t
}

pub(crate) fn corner_radius_handle_min_inset(rect: &RectangleData, zoom: f64) -> f64 {
    selection_corner_handle_min_inset(zoom).min(corner_radius_handle_max_inset(rect, zoom))
}

pub(crate) fn corner_radius_handle_max_inset(rect: &RectangleData, zoom: f64) -> f64 {
    let handle_radius = selection_handle_size(zoom) / 2.0;
    (rect.width.min(rect.height) / 2.0 - handle_radius).max(0.0)
}

pub(crate) fn corner_radius(radii: CornerRadii, corner: RectCorner) -> f64 {
    match corner {
        RectCorner::TopLeft => radii.top_left,
        RectCorner::TopRight => radii.top_right,
        RectCorner::BottomRight => radii.bottom_right,
        RectCorner::BottomLeft => radii.bottom_left,
    }
}

pub(crate) fn set_corner_radius(radii: CornerRadii, corner: RectCorner, value: f64) -> CornerRadii {
    let mut next = radii;
    match corner {
        RectCorner::TopLeft => next.top_left = value,
        RectCorner::TopRight => next.top_right = value,
        RectCorner::BottomRight => next.bottom_right = value,
        RectCorner::BottomLeft => next.bottom_left = value,
    }
    next
}

pub(crate) fn max_corner_radius_for(rect: &RectangleData, corner: RectCorner) -> f64 {
    match corner {
        RectCorner::TopLeft => (rect.width - rect.corner_radii.top_right)
            .min(rect.height - rect.corner_radii.bottom_left),
        RectCorner::TopRight => (rect.width - rect.corner_radii.top_left)
            .min(rect.height - rect.corner_radii.bottom_right),
        RectCorner::BottomRight => (rect.width - rect.corner_radii.bottom_left)
            .min(rect.height - rect.corner_radii.top_right),
        RectCorner::BottomLeft => (rect.width - rect.corner_radii.bottom_right)
            .min(rect.height - rect.corner_radii.top_left),
    }
    .max(0.0)
}

pub(crate) fn uniform_corner_radius_limit(rect: &RectangleData) -> f64 {
    (rect.width / 2.0).min(rect.height / 2.0).max(0.0)
}
