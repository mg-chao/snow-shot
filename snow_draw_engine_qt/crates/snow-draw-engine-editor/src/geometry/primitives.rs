use super::*;

pub(crate) fn point_in_circle(center: Point<f64>, radius: f64, point: Point<f64>) -> bool {
    let dx = point.x - center.x;
    let dy = point.y - center.y;
    dx * dx + dy * dy <= radius * radius
}

pub(crate) fn point_in_rotated_rect(
    center: Point<f64>,
    width: f64,
    height: f64,
    rotation: f64,
    point: Point<f64>,
) -> bool {
    let local = canvas_to_rect_local(center, rotation, point);
    local.x.abs() <= width / 2.0 && local.y.abs() <= height / 2.0
}

pub(crate) fn rect_local_to_canvas(
    center: Point<f64>,
    rotation: f64,
    local: Point<f64>,
) -> Point<f64> {
    let rotated = rotate_vector(local, rotation);
    Point {
        x: center.x + rotated.x,
        y: center.y + rotated.y,
    }
}

pub(crate) fn canvas_to_rect_local(
    center: Point<f64>,
    rotation: f64,
    point: Point<f64>,
) -> Point<f64> {
    let dx = point.x - center.x;
    let dy = point.y - center.y;
    let theta = -rotation;
    Point {
        x: dx * theta.cos() - dy * theta.sin(),
        y: dx * theta.sin() + dy * theta.cos(),
    }
}

pub(crate) fn rotate_vector(vector: Point<f64>, rotation: f64) -> Point<f64> {
    Point {
        x: vector.x * rotation.cos() - vector.y * rotation.sin(),
        y: vector.x * rotation.sin() + vector.y * rotation.cos(),
    }
}

pub(crate) fn rotate_point_around(
    point: Point<f64>,
    center: Point<f64>,
    rotation: f64,
) -> Point<f64> {
    rect_local_to_canvas(
        center,
        rotation,
        Point {
            x: point.x - center.x,
            y: point.y - center.y,
        },
    )
}

pub(crate) fn angle_between(center: Point<f64>, point: Point<f64>) -> f64 {
    (point.y - center.y).atan2(point.x - center.x)
}

pub(crate) fn pointer_drag_distance(start: Point<f64>, current: Point<f64>) -> f64 {
    let dx = current.x - start.x;
    let dy = current.y - start.y;
    (dx * dx + dy * dy).sqrt()
}
