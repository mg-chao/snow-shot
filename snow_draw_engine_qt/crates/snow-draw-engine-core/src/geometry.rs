use std::f64::consts::PI;

use crate::ErrorCode;
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Point<T = f64> {
    pub x: T,
    pub y: T,
}

impl Point<f64> {
    pub const fn new(x: f64, y: f64) -> Self {
        Self { x, y }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vector2<T = f64> {
    pub x: T,
    pub y: T,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SurfaceSize {
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Camera {
    pub center: Point<f64>,
    pub zoom: f64,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ViewportQuery {
    pub surface: SurfaceSize,
    pub camera: Camera,
}

pub fn validate_camera(camera: &Camera) -> Result<(), ErrorCode> {
    if !camera.center.x.is_finite()
        || !camera.center.y.is_finite()
        || !camera.zoom.is_finite()
        || camera.zoom <= 0.0
    {
        return Err(ErrorCode::InvalidArgument);
    }
    Ok(())
}

pub fn canvas_to_view(point: Point<f64>, camera: &Camera, surface: SurfaceSize) -> Point<f64> {
    Point {
        x: (point.x - camera.center.x) * camera.zoom + surface.width as f64 / 2.0,
        y: (point.y - camera.center.y) * camera.zoom + surface.height as f64 / 2.0,
    }
}

pub fn view_to_canvas(point: Point<f64>, camera: &Camera, surface: SurfaceSize) -> Point<f64> {
    Point {
        x: (point.x - surface.width as f64 / 2.0) / camera.zoom + camera.center.x,
        y: (point.y - surface.height as f64 / 2.0) / camera.zoom + camera.center.y,
    }
}

pub fn normalize_rotation(rotation: f64) -> f64 {
    let turns = rotation / (2.0 * PI);
    let whole_turns = turns.trunc();
    rotation - whole_turns * 2.0 * PI
}

pub fn canvas_viewport(camera: Camera, surface: SurfaceSize) -> (f64, f64, f64, f64) {
    let half_width = surface.width as f64 / (2.0 * camera.zoom);
    let half_height = surface.height as f64 / (2.0 * camera.zoom);
    (
        camera.center.x - half_width,
        camera.center.y - half_height,
        camera.center.x + half_width,
        camera.center.y + half_height,
    )
}

pub fn rotated_rect_extents(
    width: f64,
    height: f64,
    rotation: f64,
    stroke_width: f64,
) -> (f64, f64) {
    let theta = normalize_rotation(rotation);
    let cos_theta = theta.cos().abs();
    let sin_theta = theta.sin().abs();
    // Pad in local space before projecting to the world AABB so the stroke
    // survives rotation without being clipped out of the dirty region.
    let stroke_padding = stroke_width.max(0.0) / 2.0;
    let half_width = width / 2.0 + stroke_padding;
    let half_height = height / 2.0 + stroke_padding;
    (
        cos_theta * half_width + sin_theta * half_height,
        sin_theta * half_width + cos_theta * half_height,
    )
}

pub fn rectangle_intersects_viewport(
    center: Point<f64>,
    width: f64,
    height: f64,
    rotation: f64,
    stroke_width: f64,
    viewport: (f64, f64, f64, f64),
) -> bool {
    let (left, top, right, bottom) = viewport;
    let (extent_x, extent_y) = rotated_rect_extents(width, height, rotation, stroke_width);

    let rect_left = center.x - extent_x;
    let rect_top = center.y - extent_y;
    let rect_right = center.x + extent_x;
    let rect_bottom = center.y + extent_y;

    rect_right >= left && rect_left <= right && rect_bottom >= top && rect_top <= bottom
}

pub fn rectangle_contains_point(
    center: Point<f64>,
    width: f64,
    height: f64,
    rotation: f64,
    point: Point<f64>,
) -> bool {
    if width <= 0.0 || height <= 0.0 {
        return false;
    }

    let dx = point.x - center.x;
    let dy = point.y - center.y;
    let theta = -normalize_rotation(rotation);
    let local_x = dx * theta.cos() - dy * theta.sin();
    let local_y = dx * theta.sin() + dy * theta.cos();
    local_x.abs() <= width / 2.0 && local_y.abs() <= height / 2.0
}
