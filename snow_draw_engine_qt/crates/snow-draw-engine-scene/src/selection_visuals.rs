use super::*;

const SELECTION_FRAME_PADDING_PX: f64 = 4.0;
const TEXT_SELECTION_FRAME_PADDING_PX: f64 = 14.0;
const SELECTION_HANDLE_SIZE_PX: f64 = 8.0;
const SELECTION_ARROW_CORNER_HANDLE_OUTSET_PX: f64 = 8.0;
const ARROW_POINT_HANDLE_SIZE_PX: f64 = 10.0;
const FOCUS_POINT_HANDLE_SIZE_PX: f64 = 9.0;
const SELECTION_HANDLE_CORNER_RADIUS_PX: f64 = 2.0;
const SELECTION_ROTATION_OFFSET_PX: f64 = 20.0;
const SELECTION_CORNER_HANDLE_MIN_INSET_PX: f64 = 12.0;
pub(crate) const TRANSPARENT: ColorRgba8 = ColorRgba8 {
    r: 0x00,
    g: 0x00,
    b: 0x00,
    a: 0x00,
};
pub(crate) const SELECTION_COLOR: ColorRgba8 = ColorRgba8 {
    r: 0x40,
    g: 0x96,
    b: 0xff,
    a: 0xff,
};
pub(crate) const SNOW_SHOT_CONTROL_FILL: ColorRgba8 = ColorRgba8 {
    r: 0xff,
    g: 0xff,
    b: 0xff,
    a: 0xe6,
};
pub(crate) const SNOW_SHOT_CONTROL_STROKE: ColorRgba8 = ColorRgba8 {
    r: 0x40,
    g: 0x96,
    b: 0xff,
    a: 0xff,
};
pub(crate) const SNOW_SHOT_CONTROL_PHANTOM_FILL: ColorRgba8 = ColorRgba8 {
    r: 0x16,
    g: 0x77,
    b: 0xff,
    a: 0xb3,
};
pub(crate) const SNOW_SHOT_FOCUS_CONNECTION_STROKE: ColorRgba8 = ColorRgba8 {
    r: 0x40,
    g: 0x96,
    b: 0xff,
    a: 0x99,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Corner {
    TopLeft,
    TopRight,
    BottomRight,
    BottomLeft,
}

impl Corner {
    pub(crate) const ALL: [Corner; 4] = [
        Corner::TopLeft,
        Corner::TopRight,
        Corner::BottomRight,
        Corner::BottomLeft,
    ];

    fn x_sign(self) -> f64 {
        match self {
            Self::TopLeft | Self::BottomLeft => -1.0,
            Self::TopRight | Self::BottomRight => 1.0,
        }
    }

    fn y_sign(self) -> f64 {
        match self {
            Self::TopLeft | Self::TopRight => -1.0,
            Self::BottomRight | Self::BottomLeft => 1.0,
        }
    }
}

pub(crate) fn selection_outline_for_rect(rect: RectangleData, zoom: f64) -> RectangleData {
    selection_outline_bounds(
        SelectionBounds {
            center: rect.center,
            width: rect.width,
            height: rect.height,
            rotation: rect.rotation,
        },
        zoom,
        selection_frame_padding(zoom),
    )
}

pub(crate) fn selection_outline_for_arrow_bounds(bounds: DrawRect, zoom: f64) -> RectangleData {
    selection_outline_bounds(
        SelectionBounds {
            center: bounds.center(),
            width: bounds.width(),
            height: bounds.height(),
            rotation: 0.0,
        },
        zoom,
        selection_frame_padding_for_members(zoom, 0, 1),
    )
}

pub(crate) fn text_actual_frame(rect: RectangleData, zoom: f64) -> RectangleData {
    RectangleData {
        rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
        highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
        center: rect.center,
        width: rect.width,
        height: rect.height,
        rotation: rect.rotation,
        fill: TRANSPARENT,
        fill_style: FillStyle::Solid,
        stroke: SELECTION_COLOR,
        stroke_width: 1.0 / zoom.max(0.0001),
        stroke_style: StrokeStyle::Solid,
        corner_radii: CornerRadii::default(),
        opacity: 1.0,
    }
}

pub(crate) fn text_hover_underline_for_rect(rect: RectangleData, zoom: f64) -> RectangleData {
    RectangleData {
        rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
        highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
        center: rect.center,
        width: rect.width,
        height: rect.height,
        rotation: rect.rotation,
        fill: TRANSPARENT,
        fill_style: FillStyle::Solid,
        stroke: SELECTION_COLOR,
        stroke_width: 1.5 / zoom.max(0.0001),
        stroke_style: StrokeStyle::Solid,
        corner_radii: CornerRadii::default(),
        opacity: 1.0,
    }
}

pub(crate) fn selection_outline_bounds(
    bounds: SelectionBounds,
    zoom: f64,
    frame_padding: f64,
) -> RectangleData {
    RectangleData {
        rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
        highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
        center: bounds.center,
        width: bounds.width + frame_padding * 2.0,
        height: bounds.height + frame_padding * 2.0,
        rotation: bounds.rotation,
        fill: ColorRgba8 {
            r: 0,
            g: 0,
            b: 0,
            a: 0,
        },
        fill_style: FillStyle::Solid,
        stroke: SELECTION_COLOR,
        stroke_width: 1.0 / zoom.max(0.0001),
        stroke_style: StrokeStyle::Solid,
        corner_radii: CornerRadii::default(),
        opacity: 1.0,
    }
}

fn selection_frame_padding(zoom: f64) -> f64 {
    SELECTION_FRAME_PADDING_PX / zoom.max(0.0001)
}

pub(crate) fn text_selection_frame_padding(zoom: f64) -> f64 {
    TEXT_SELECTION_FRAME_PADDING_PX / zoom.max(0.0001)
}

pub(crate) fn selection_frame_padding_for_members(
    zoom: f64,
    selected_rect_count: usize,
    selected_arrow_count: usize,
) -> f64 {
    if selected_rect_count == 0 && selected_arrow_count > 0 {
        0.0
    } else {
        selection_frame_padding(zoom)
    }
}

pub(crate) fn selection_corner_handle_outset_for_members(
    zoom: f64,
    selected_rect_count: usize,
    selected_arrow_count: usize,
) -> f64 {
    if selected_rect_count == 0 && selected_arrow_count > 0 {
        SELECTION_ARROW_CORNER_HANDLE_OUTSET_PX / zoom.max(0.0001)
    } else {
        0.0
    }
}

pub(crate) fn selection_handle_size(zoom: f64) -> f64 {
    SELECTION_HANDLE_SIZE_PX / zoom.max(0.0001)
}

pub(crate) fn arrow_point_handle_size(zoom: f64) -> f64 {
    ARROW_POINT_HANDLE_SIZE_PX / zoom.max(0.0001)
}

pub(crate) fn focus_point_handle_size(zoom: f64) -> f64 {
    FOCUS_POINT_HANDLE_SIZE_PX / zoom.max(0.0001)
}

fn selection_rotation_offset(zoom: f64) -> f64 {
    SELECTION_ROTATION_OFFSET_PX / zoom.max(0.0001)
}

fn selection_corner_handle_min_inset(zoom: f64) -> f64 {
    SELECTION_CORNER_HANDLE_MIN_INSET_PX / zoom.max(0.0001)
}

pub(crate) fn selection_resize_handle_center(
    bounds: SelectionBounds,
    frame_padding: f64,
    corner_handle_outset: f64,
    corner: Corner,
) -> Point<f64> {
    rect_local_to_canvas(
        bounds.center,
        bounds.rotation,
        Point {
            x: corner.x_sign() * (bounds.width / 2.0 + frame_padding + corner_handle_outset),
            y: corner.y_sign() * (bounds.height / 2.0 + frame_padding + corner_handle_outset),
        },
    )
}

pub(crate) fn selection_rotation_handle_center(
    bounds: SelectionBounds,
    zoom: f64,
    frame_padding: f64,
) -> Point<f64> {
    rect_local_to_canvas(
        bounds.center,
        bounds.rotation,
        Point {
            x: 0.0,
            y: -(bounds.height / 2.0 + frame_padding + selection_rotation_offset(zoom)),
        },
    )
}

pub(crate) fn selection_handle_rect(
    center: Point<f64>,
    size: f64,
    rotation: f64,
    circular: bool,
    zoom: f64,
) -> RectangleData {
    RectangleData {
        rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
        highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
        center,
        width: size,
        height: size,
        rotation,
        fill: ColorRgba8 {
            r: 0xff,
            g: 0xff,
            b: 0xff,
            a: 0xff,
        },
        fill_style: FillStyle::Solid,
        stroke: SELECTION_COLOR,
        stroke_width: 1.0 / zoom.max(0.0001),
        stroke_style: StrokeStyle::Solid,
        corner_radii: if circular {
            CornerRadii::splat(size / 2.0)
        } else {
            CornerRadii::splat(
                (SELECTION_HANDLE_CORNER_RADIUS_PX / zoom.max(0.0001)).min(size / 2.0),
            )
        },
        opacity: 1.0,
    }
}

pub(crate) fn corner_radius_handle_local_point(
    rect: RectangleData,
    zoom: f64,
    corner: Corner,
) -> Point<f64> {
    let radius = corner_radius(rect.corner_radii, corner);
    let max_radius = max_corner_radius_for(rect, corner);
    let inset = corner_radius_handle_inset(rect, zoom, radius, max_radius);
    Point {
        x: corner.x_sign() * (rect.width / 2.0 - inset),
        y: corner.y_sign() * (rect.height / 2.0 - inset),
    }
}

fn corner_radius_handle_inset(rect: RectangleData, zoom: f64, radius: f64, max_radius: f64) -> f64 {
    let min_inset = corner_radius_handle_min_inset(rect, zoom);
    let max_inset = corner_radius_handle_max_inset(rect, zoom);
    if max_radius <= f64::EPSILON || max_inset <= min_inset {
        return min_inset;
    }

    let t = (radius / max_radius).clamp(0.0, 1.0);
    min_inset + (max_inset - min_inset) * t
}

fn corner_radius_handle_min_inset(rect: RectangleData, zoom: f64) -> f64 {
    selection_corner_handle_min_inset(zoom).min(corner_radius_handle_max_inset(rect, zoom))
}

fn corner_radius_handle_max_inset(rect: RectangleData, zoom: f64) -> f64 {
    let handle_radius = selection_handle_size(zoom) / 2.0;
    (rect.width.min(rect.height) / 2.0 - handle_radius).max(0.0)
}

fn corner_radius(radii: CornerRadii, corner: Corner) -> f64 {
    match corner {
        Corner::TopLeft => radii.top_left,
        Corner::TopRight => radii.top_right,
        Corner::BottomRight => radii.bottom_right,
        Corner::BottomLeft => radii.bottom_left,
    }
}

fn max_corner_radius_for(rect: RectangleData, corner: Corner) -> f64 {
    match corner {
        Corner::TopLeft => (rect.width - rect.corner_radii.top_right)
            .min(rect.height - rect.corner_radii.bottom_left),
        Corner::TopRight => (rect.width - rect.corner_radii.top_left)
            .min(rect.height - rect.corner_radii.bottom_right),
        Corner::BottomRight => (rect.width - rect.corner_radii.bottom_left)
            .min(rect.height - rect.corner_radii.top_right),
        Corner::BottomLeft => (rect.width - rect.corner_radii.bottom_right)
            .min(rect.height - rect.corner_radii.top_left),
    }
    .max(0.0)
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

fn rotate_vector(vector: Point<f64>, rotation: f64) -> Point<f64> {
    Point {
        x: vector.x * rotation.cos() - vector.y * rotation.sin(),
        y: vector.x * rotation.sin() + vector.y * rotation.cos(),
    }
}
