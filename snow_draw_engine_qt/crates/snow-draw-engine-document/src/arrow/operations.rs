use crate::arrow::apply_arrow_patch;
use crate::{
    ArrowData, ArrowEndpointBinding, BindableElementState, ElementId,
    arrow_binding_core as binding, arrow_elbow_core as routing, arrow_engine as editing,
    arrow_focus_core as focus, arrow_hit_test as hit_testing,
};
use crate::{
    ArrowEngineEvent, ArrowState, BindableState, ComputeEndpointDragInput,
    ComputeFocusPointDragInput, ElbowUpdatePatch, EngineResult, FixedPointBinding,
    ListVisibleFocusPointsInput, PointUpdate, RecomputeAfterBindableChangeInput,
    UpdateElbowArrowInput,
};
use snow_draw_engine_core::{
    Point,
    arrow::{
        ArrowEndpointEdge, ArrowType, BindMode, ComputeEndpointDragOptions,
        ComputeFocusPointDragOptions, EngineContext, FocusPointContext, UpdateElbowArrowOptions,
    },
};

#[derive(Clone, Debug, PartialEq)]
pub struct ArrowEditResult {
    pub arrow: ArrowData,
    pub reorder_targets: Vec<ElementId>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ArrowEndpointDragOptions {
    pub new_arrow: bool,
    pub initial_binding: bool,
    pub alt_key: bool,
    pub finalize: bool,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ArrowFocusPointState {
    pub edge: ArrowEndpointEdge,
    pub point: Point<f64>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ArrowFocusDragOptions {
    pub switch_to_inside_binding: bool,
    pub grid_size: Option<f64>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ArrowSegmentDragResult {
    pub arrow: ArrowData,
    pub next_segment_index: usize,
}

pub fn compute_arrow_endpoint_drag(
    arrow_id: ElementId,
    arrow: &ArrowData,
    edge: ArrowEndpointEdge,
    canvas_point: Point<f64>,
    bindables: &[BindableElementState],
    context: EngineContext,
    options: ArrowEndpointDragOptions,
) -> ArrowEditResult {
    if arrow.is_elbow() {
        return ArrowEditResult {
            arrow: preview_elbow_arrow_endpoint_binding(
                arrow,
                edge,
                canvas_point,
                bindables,
                context,
                arrow_id,
                options.finalize,
            ),
            reorder_targets: Vec::new(),
        };
    }

    let local_point = PointUpdate {
        index: arrow_endpoint_index(arrow.points.len(), edge),
        point: [canvas_point.x - arrow.x, canvas_point.y - arrow.y],
    };
    let input = ComputeEndpointDragInput {
        arrow: arrow_engine_state(arrow_id, arrow),
        dragged_points: vec![local_point],
        pointer: [canvas_point.x, canvas_point.y],
        bindables: bindable_states_from_elements(bindables),
        context,
        options: Some(ComputeEndpointDragOptions {
            new_arrow: options.new_arrow.then_some(true),
            initial_binding: options.initial_binding.then_some(true),
            alt_key: options.alt_key.then_some(true),
            finalize: options.finalize.then_some(true),
            ..ComputeEndpointDragOptions::default()
        }),
    };
    let result = if options.finalize {
        editing::finalize_endpoint_drag(&input)
    } else {
        editing::compute_endpoint_drag(&input)
    };

    arrow_edit_result_from_engine_result(arrow, bindables, result)
}

pub fn compute_arrow_focus_drag(
    arrow_id: ElementId,
    arrow: &ArrowData,
    edge: ArrowEndpointEdge,
    canvas_point: Point<f64>,
    bindables: &[BindableElementState],
    context: EngineContext,
    options: ArrowFocusDragOptions,
) -> ArrowEditResult {
    let result = editing::compute_focus_drag(&ComputeFocusPointDragInput {
        arrow: arrow_engine_state(arrow_id, arrow),
        dragged_edge: edge,
        pointer: [canvas_point.x, canvas_point.y],
        bindables: bindable_states_from_elements(bindables),
        context,
        options: Some(ComputeFocusPointDragOptions {
            switch_to_inside_binding: Some(options.switch_to_inside_binding),
            grid_size: options.grid_size,
        }),
    });

    arrow_edit_result_from_engine_result(arrow, bindables, result)
}

pub fn recompute_arrow_after_bindable_change(
    arrow_id: ElementId,
    arrow: &ArrowData,
    bindables: &[BindableElementState],
    changed_bindable_ids: &[ElementId],
    context: EngineContext,
) -> ArrowEditResult {
    let changed_bindable_ids =
        (!changed_bindable_ids.is_empty()).then(|| changed_bindable_ids.to_vec());
    let result = editing::recompute_after_bindable_change(&RecomputeAfterBindableChangeInput {
        arrow: arrow_engine_state(arrow_id, arrow),
        bindables: bindable_states_from_elements(bindables),
        changed_bindable_ids,
        context,
        options: None,
    });

    arrow_edit_result_from_engine_result(arrow, bindables, result)
}

pub fn visible_arrow_focus_points(
    arrow_id: ElementId,
    arrow: &ArrowData,
    bindables: &[BindableElementState],
    zoom: f64,
) -> Vec<ArrowFocusPointState> {
    focus::list_visible_focus_points(&ListVisibleFocusPointsInput {
        arrow: arrow_engine_state(arrow_id, arrow),
        bindables: bindable_states_from_elements(bindables),
        context: FocusPointContext {
            zoom,
            is_binding_enabled: true,
        },
        options: None,
    })
    .into_iter()
    .map(|focus| ArrowFocusPointState {
        edge: focus.edge,
        point: Point::new(focus.point[0], focus.point[1]),
    })
    .collect()
}

pub fn drag_elbow_arrow_segment(
    arrow_id: ElementId,
    arrow: &ArrowData,
    segment_index: usize,
    canvas_point: Point<f64>,
    bindables: &[BindableElementState],
    context: EngineContext,
    finalize: bool,
) -> ArrowSegmentDragResult {
    let mut source_arrow = arrow.clone();
    source_arrow.arrow_type = ArrowType::Elbow;
    let arrow_state = arrow_engine_state(arrow_id, &source_arrow);
    let fixed_segment = routing::move_fixed_segment_to_point(
        &arrow_state,
        segment_index,
        [canvas_point.x, canvas_point.y],
    );
    let fixed_segment_offset = fixed_segment
        .patch
        .fixed_segments
        .as_ref()
        .and_then(|segments| segments.as_ref())
        .map(|segments| {
            segments
                .iter()
                .filter(|segment| segment.index < segment_index)
                .count()
        })
        .unwrap_or(0);
    let patch = routing::update_elbow_arrow_patch(UpdateElbowArrowInput {
        arrow: arrow_state,
        updates: ElbowUpdatePatch {
            fixed_segments: fixed_segment.patch.fixed_segments,
            ..ElbowUpdatePatch::default()
        },
        bindables: bindable_states_from_elements(bindables),
        context,
        options: Some(UpdateElbowArrowOptions {
            is_dragging: Some(!finalize),
            validate_invariants: Some(finalize),
        }),
    });
    let preview_arrow = apply_arrow_patch(&source_arrow, &patch);
    let next_segment_index = preview_arrow
        .fixed_segments
        .as_ref()
        .and_then(|segments| segments.get(fixed_segment_offset))
        .map(|segment| segment.index)
        .unwrap_or(segment_index);

    ArrowSegmentDragResult {
        arrow: preview_arrow,
        next_segment_index,
    }
}

pub fn preview_elbow_arrow_endpoint_binding(
    arrow: &ArrowData,
    edge: ArrowEndpointEdge,
    canvas_point: Point<f64>,
    bindables: &[BindableElementState],
    context: EngineContext,
    arrow_id: ElementId,
    finalize: bool,
) -> ArrowData {
    let bindable_states = bindable_states_from_elements(bindables);
    let hovered = if context.is_binding_enabled {
        hit_testing::get_hovered_bindable(
            [canvas_point.x, canvas_point.y],
            &bindable_states,
            binding::max_binding_distance(context.zoom),
        )
    } else {
        None
    };

    let mut preview_arrow = arrow.clone();
    let tentative_binding = hovered.as_ref().map(|bindable| ArrowEndpointBinding {
        element_id: bindable.id,
        fixed_point: binding::calculate_fixed_point_for_binding(
            bindable,
            [canvas_point.x, canvas_point.y],
        ),
        mode: BindMode::Orbit,
    });
    match edge {
        ArrowEndpointEdge::Start => preview_arrow.set_start_element_binding(tentative_binding),
        ArrowEndpointEdge::End => preview_arrow.set_end_element_binding(tentative_binding),
    }

    let endpoint_points = vec![
        match edge {
            ArrowEndpointEdge::Start => [
                canvas_point.x - preview_arrow.x,
                canvas_point.y - preview_arrow.y,
            ],
            ArrowEndpointEdge::End => [
                preview_arrow.start().x - preview_arrow.x,
                preview_arrow.start().y - preview_arrow.y,
            ],
        },
        match edge {
            ArrowEndpointEdge::Start => [
                preview_arrow.end().x - preview_arrow.x,
                preview_arrow.end().y - preview_arrow.y,
            ],
            ArrowEndpointEdge::End => [
                canvas_point.x - preview_arrow.x,
                canvas_point.y - preview_arrow.y,
            ],
        },
    ];
    let route_patch = routing::update_elbow_arrow_patch(UpdateElbowArrowInput {
        arrow: arrow_engine_state(arrow_id, &preview_arrow),
        updates: ElbowUpdatePatch {
            points: Some(endpoint_points),
            ..ElbowUpdatePatch::default()
        },
        bindables: bindable_states.clone(),
        context,
        options: Some(UpdateElbowArrowOptions {
            is_dragging: Some(!finalize),
            validate_invariants: None,
        }),
    });
    preview_arrow = apply_arrow_patch(&preview_arrow, &route_patch);

    let binding = hovered
        .as_ref()
        .map(|bindable| ArrowEndpointBinding {
            element_id: bindable.id,
            fixed_point: binding::calculate_fixed_point_for_elbow_binding(
                &arrow_engine_state(arrow_id, &preview_arrow),
                bindable,
                edge,
            ),
            mode: BindMode::Orbit,
        })
        .map(FixedPointBinding::from);
    let updates = match edge {
        ArrowEndpointEdge::Start => ElbowUpdatePatch {
            start_binding: Some(binding),
            ..ElbowUpdatePatch::default()
        },
        ArrowEndpointEdge::End => ElbowUpdatePatch {
            end_binding: Some(binding),
            ..ElbowUpdatePatch::default()
        },
    };
    let binding_patch = routing::update_elbow_arrow_patch(UpdateElbowArrowInput {
        arrow: arrow_engine_state(arrow_id, &preview_arrow),
        updates,
        bindables: bindable_states,
        context,
        options: Some(UpdateElbowArrowOptions {
            is_dragging: Some(!finalize),
            validate_invariants: None,
        }),
    });
    apply_arrow_patch(&preview_arrow, &binding_patch)
}

fn bindable_states_from_elements(bindables: &[BindableElementState]) -> Vec<BindableState> {
    bindables
        .iter()
        .map(BindableElementState::arrow_bindable_state)
        .collect()
}

fn arrow_engine_state(arrow_id: ElementId, arrow: &ArrowData) -> ArrowState {
    ArrowState::from_arrow_data(arrow_id, arrow)
}

fn arrow_edit_result_from_engine_result(
    arrow: &ArrowData,
    bindables: &[BindableElementState],
    result: EngineResult,
) -> ArrowEditResult {
    ArrowEditResult {
        arrow: apply_arrow_patch(arrow, &result.arrow_patch),
        reorder_targets: reorder_targets_from_events(bindables, &result.events),
    }
}

fn reorder_targets_from_events(
    bindables: &[BindableElementState],
    events: &[ArrowEngineEvent],
) -> Vec<ElementId> {
    let mut targets = Vec::new();
    for event in events {
        let ArrowEngineEvent::ReorderArrow { bindable_id, .. } = event else {
            continue;
        };
        let Some(target) = bindables
            .iter()
            .find(|bindable| bindable.matches_arrow_bindable_id(*bindable_id))
            .map(|bindable| bindable.id())
        else {
            continue;
        };
        if !targets.contains(&target) {
            targets.push(target);
        }
    }
    targets
}

fn arrow_endpoint_index(point_count: usize, edge: ArrowEndpointEdge) -> usize {
    match edge {
        ArrowEndpointEdge::Start => 0,
        ArrowEndpointEdge::End => point_count.saturating_sub(1),
    }
}
