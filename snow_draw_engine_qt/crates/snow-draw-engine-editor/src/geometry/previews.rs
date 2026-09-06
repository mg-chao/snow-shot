use super::*;

#[cfg(any())]
pub(crate) fn preview_elements_for_resize(
    original_elements: &[SelectionRectState],
    original_bounds: &SelectionBounds,
    handle: ResizeHandle,
    handle_offset_canvas: Point<f64>,
    frame_padding: f64,
    canvas_point: Point<f64>,
    modifiers: Modifiers,
) -> Vec<SelectionRectState> {
    let geometry = resize_drag_geometry(ResizeDragGeometryRequest {
        original_elements,
        original_bounds,
        handle,
        handle_offset_canvas,
        frame_padding,
        corner_handle_outset: 0.0,
        canvas_point,
        modifiers,
        force_aspect_lock: false,
        allow_flip: false,
        minimum_scale: 0.0,
    });

    original_elements
        .iter()
        .map(|element| SelectionRectState {
            id: element.id,
            rect: resized_selection_rect(
                &element.rect,
                original_bounds,
                geometry.anchor_local,
                geometry.scale_x,
                geometry.scale_y,
            ),
        })
        .collect()
}

pub(crate) fn preview_elements_for_corner_radius(
    original_elements: &[SelectionRectState],
    corner: RectCorner,
    canvas_point: Point<f64>,
    uniform: bool,
    zoom: f64,
) -> Vec<SelectionRectState> {
    let mut preview = original_elements.to_vec();
    if let Some(first) = preview.first_mut() {
        first.rect =
            preview_rect_for_corner_radius(&first.rect, corner, canvas_point, uniform, zoom);
    }
    preview
}

pub(crate) fn preview_rect_for_corner_radius(
    original_rect: &RectangleData,
    corner: RectCorner,
    canvas_point: Point<f64>,
    uniform: bool,
    zoom: f64,
) -> RectangleData {
    let mut rect = *original_rect;
    let local_point = canvas_to_rect_local(rect.center, rect.rotation, canvas_point);
    let corner_point = rect_corner_point(rect.width, rect.height, corner);
    let inward_x = if corner.x_sign() < 0.0 {
        local_point.x - corner_point.x
    } else {
        corner_point.x - local_point.x
    };
    let inward_y = if corner.y_sign() < 0.0 {
        local_point.y - corner_point.y
    } else {
        corner_point.y - local_point.y
    };
    let max_radius = if uniform {
        uniform_corner_radius_limit(&rect)
    } else {
        max_corner_radius_for(&rect, corner)
    };
    let desired = corner_radius_for_handle_inset(&rect, zoom, inward_x.min(inward_y), max_radius);
    rect.corner_radii = if uniform {
        CornerRadii::splat(desired)
    } else {
        set_corner_radius(rect.corner_radii, corner, desired)
    };
    rect
}
