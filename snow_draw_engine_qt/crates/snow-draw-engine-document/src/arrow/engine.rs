use std::collections::{BTreeMap, BTreeSet};

use crate::arrow_binding_core::{
    calculate_fixed_point_for_binding, get_snap_outline_mid_point, pick_hovered_bindable,
};
use crate::arrow_focus_core::{compute_focus_point_drag, resolve_bound_point_local};
use crate::arrow_geom::{normalize_arrow_from_global_points, points_equal, to_global_point};
use crate::arrow_hit_test::is_point_in_bindable;
use crate::arrow_state_core::apply_arrow_patch_internal;
use crate::{
    ArrowEngineEvent, ArrowPatch, ArrowState, BindMode, BindablePatch, BindableState,
    ComputeEndpointDragInput, ComputeFocusPointDragInput, ElbowUpdatePatch, ElementId,
    EngineResult, FixedPointBinding, Point, RecomputeAfterBindableChangeInput, RecomputeElbowInput,
    UpdateElbowArrowInput, UpdateElbowArrowOptions, ValidationReport,
};

fn merge_arrow_patches(primary: ArrowPatch, secondary: ArrowPatch) -> ArrowPatch {
    ArrowPatch {
        x: secondary.x.or(primary.x),
        y: secondary.y.or(primary.y),
        width: secondary.width.or(primary.width),
        height: secondary.height.or(primary.height),
        points: secondary.points.or(primary.points),
        start_binding: secondary.start_binding.or(primary.start_binding),
        end_binding: secondary.end_binding.or(primary.end_binding),
        fixed_segments: secondary.fixed_segments.or(primary.fixed_segments),
        start_is_special: secondary.start_is_special.or(primary.start_is_special),
        end_is_special: secondary.end_is_special.or(primary.end_is_special),
    }
}

fn compute_patch_from_local_points(
    arrow: &ArrowState,
    points: &[Point],
    max_coordinate: f64,
) -> ArrowPatch {
    let global_points = points
        .iter()
        .copied()
        .map(|point| to_global_point(arrow, point))
        .collect::<Vec<_>>();
    let normalized = normalize_arrow_from_global_points(&global_points, max_coordinate);
    ArrowPatch {
        x: Some(normalized.x),
        y: Some(normalized.y),
        width: Some(normalized.width),
        height: Some(normalized.height),
        points: Some(normalized.points),
        start_binding: None,
        end_binding: None,
        fixed_segments: None,
        start_is_special: None,
        end_is_special: None,
    }
}

fn bindables_by_id(bindables: &[BindableState]) -> BTreeMap<ElementId, BindableState> {
    bindables
        .iter()
        .cloned()
        .map(|bindable| (bindable.id, bindable))
        .collect()
}

fn collect_binding_transition(
    arrow_id: ElementId,
    edge: crate::ArrowEndpointEdge,
    previous_binding: Option<&FixedPointBinding>,
    next_binding: Option<&FixedPointBinding>,
    bindable_patches: &mut Vec<BindablePatch>,
    events: &mut Vec<ArrowEngineEvent>,
    reorder_targets: &mut BTreeSet<ElementId>,
) {
    if let Some(previous_binding) = previous_binding
        && next_binding
            .is_none_or(|next_binding| next_binding.element_id != previous_binding.element_id)
    {
        bindable_patches.push(BindablePatch {
            id: previous_binding.element_id,
            add_bound_arrow_id: None,
            remove_bound_arrow_id: Some(arrow_id),
        });
        if next_binding.is_none() {
            events.push(ArrowEngineEvent::BindingBroken { arrow_id, edge });
        }
    }

    if let Some(next_binding) = next_binding
        && previous_binding
            .is_none_or(|previous_binding| previous_binding.element_id != next_binding.element_id)
    {
        bindable_patches.push(BindablePatch {
            id: next_binding.element_id,
            add_bound_arrow_id: Some(arrow_id),
            remove_bound_arrow_id: None,
        });
        if reorder_targets.insert(next_binding.element_id) {
            events.push(ArrowEngineEvent::ReorderArrow {
                arrow_id,
                bindable_id: next_binding.element_id,
            });
        }
    }
}

fn apply_binding_to_edge(
    start_binding: &mut Option<FixedPointBinding>,
    end_binding: &mut Option<FixedPointBinding>,
    edge: crate::ArrowEndpointEdge,
    binding: Option<FixedPointBinding>,
) {
    match edge {
        crate::ArrowEndpointEdge::Start => *start_binding = binding,
        crate::ArrowEndpointEdge::End => *end_binding = binding,
    }
}

fn simple_drag_binding_mode(
    hovered: &BindableState,
    pointer: Point,
    input: &ComputeEndpointDragInput,
) -> BindMode {
    if input.arrow.elbowed {
        return BindMode::Orbit;
    }
    if input.context.bind_mode == BindMode::Inside
        || input
            .options
            .as_ref()
            .and_then(|options| options.alt_key)
            .unwrap_or(false)
        || is_point_in_bindable(pointer, hovered)
    {
        BindMode::Inside
    } else {
        BindMode::Orbit
    }
}

fn recompute_endpoint_from_binding(
    arrow: &ArrowState,
    binding: &FixedPointBinding,
    bindables_by_id: &BTreeMap<ElementId, BindableState>,
) -> Option<Point> {
    bindables_by_id
        .get(&binding.element_id)
        .map(|bindable| resolve_bound_point_local(arrow, binding, bindable))
}

fn endpoint_drag_base_result(input: &ComputeEndpointDragInput) -> EngineResult {
    let mut next_points = input.arrow.points.clone();
    let point_updates = crate::point_updates_to_pairs(&input.dragged_points);
    for (index, point) in &point_updates {
        if *index < next_points.len() {
            next_points[*index] = *point;
        }
    }

    let bindables_by_id = bindables_by_id(&input.bindables);
    let hovered = if input.context.is_binding_enabled {
        pick_hovered_bindable(input.pointer, &input.bindables, &input.context)
    } else {
        None
    };

    let start_index = 0;
    let end_index = input.arrow.points.len().saturating_sub(1);
    let start_dragged = point_updates.iter().any(|(index, _)| *index == start_index);
    let end_dragged = point_updates.iter().any(|(index, _)| *index == end_index);

    let mut start_binding = input.arrow.start_binding;
    let mut end_binding = input.arrow.end_binding;

    for dragged_edge in [
        crate::ArrowEndpointEdge::Start,
        crate::ArrowEndpointEdge::End,
    ] {
        let dragged = match dragged_edge {
            crate::ArrowEndpointEdge::Start => start_dragged,
            crate::ArrowEndpointEdge::End => end_dragged,
        };
        if !dragged {
            continue;
        }

        if let Some(hovered_bindable) = hovered.as_ref() {
            let binding = FixedPointBinding {
                element_id: hovered_bindable.id,
                mode: simple_drag_binding_mode(hovered_bindable, input.pointer, input),
                fixed_point: calculate_fixed_point_for_binding(hovered_bindable, input.pointer),
            };
            apply_binding_to_edge(
                &mut start_binding,
                &mut end_binding,
                dragged_edge,
                Some(binding),
            );
        } else {
            apply_binding_to_edge(&mut start_binding, &mut end_binding, dragged_edge, None);
        }
    }

    let simulated_arrow = ArrowState {
        points: next_points.clone(),
        start_binding,
        end_binding,
        ..input.arrow.clone()
    };

    if let Some(start_binding_value) = start_binding.as_ref()
        && let Some(local_point) =
            recompute_endpoint_from_binding(&simulated_arrow, start_binding_value, &bindables_by_id)
    {
        next_points[start_index] = local_point;
    }

    let simulated_arrow = ArrowState {
        points: next_points.clone(),
        start_binding,
        end_binding,
        ..input.arrow.clone()
    };

    if let Some(end_binding_value) = end_binding.as_ref()
        && let Some(local_point) =
            recompute_endpoint_from_binding(&simulated_arrow, end_binding_value, &bindables_by_id)
    {
        next_points[end_index] = local_point;
    }

    let mut bindable_patches = Vec::new();
    let mut events = Vec::new();
    let mut reorder_targets = BTreeSet::new();
    collect_binding_transition(
        input.arrow.id,
        crate::ArrowEndpointEdge::Start,
        input.arrow.start_binding.as_ref(),
        start_binding.as_ref(),
        &mut bindable_patches,
        &mut events,
        &mut reorder_targets,
    );
    collect_binding_transition(
        input.arrow.id,
        crate::ArrowEndpointEdge::End,
        input.arrow.end_binding.as_ref(),
        end_binding.as_ref(),
        &mut bindable_patches,
        &mut events,
        &mut reorder_targets,
    );

    let mut arrow_patch =
        compute_patch_from_local_points(&input.arrow, &next_points, input.context.max_coordinate);
    arrow_patch.start_binding = Some(start_binding);
    arrow_patch.end_binding = Some(end_binding);

    EngineResult {
        arrow_patch,
        bindable_patches,
        suggested_binding: hovered.clone().map(|element| crate::SuggestedBinding {
            bindable_id: Some(element.id),
            mid_point: get_snap_outline_mid_point(input.pointer, &element, input.context.zoom),
            element,
        }),
        events,
    }
}

fn recompute_elbow_patch_internal(input: &RecomputeElbowInput) -> ArrowPatch {
    crate::arrow_elbow_core::recompute_elbow_patch(input.clone())
}

fn has_elbow_updates(updates: &ElbowUpdatePatch) -> bool {
    updates.points.is_some()
        || updates.fixed_segments.is_some()
        || updates.start_binding.is_some()
        || updates.end_binding.is_some()
}

fn update_elbow_arrow_patch_internal(input: &UpdateElbowArrowInput) -> ArrowPatch {
    if !has_elbow_updates(&input.updates) {
        return ArrowPatch::default();
    }

    crate::arrow_elbow_core::update_elbow_arrow_patch(input.clone())
}

fn validate_elbow_points(points: &[Point], tolerance: f64) -> bool {
    crate::arrow_elbow_core::validate_elbow_points(points, Some(tolerance))
}

fn validate_elbow_invariant(arrow: &ArrowState) -> Vec<String> {
    crate::arrow_elbow_core::validate_elbow_invariant(arrow)
}

pub fn compute_endpoint_drag(input: &ComputeEndpointDragInput) -> EngineResult {
    let base = if input.arrow.elbowed {
        crate::arrow_binding_core::compute_simple_binding_patch(input)
    } else {
        endpoint_drag_base_result(input)
    };
    let next_arrow = apply_arrow_patch_internal(&input.arrow, &base.arrow_patch);

    if !next_arrow.elbowed {
        return base;
    }

    let elbow_patch = recompute_elbow_patch_internal(&RecomputeElbowInput {
        arrow: next_arrow.clone(),
        bindables: input.bindables.clone(),
        context: input.context,
    });

    EngineResult {
        arrow_patch: merge_arrow_patches(base.arrow_patch, elbow_patch),
        bindable_patches: base.bindable_patches,
        suggested_binding: base.suggested_binding,
        events: base.events,
    }
}

pub fn finalize_endpoint_drag(input: &ComputeEndpointDragInput) -> EngineResult {
    let mut next_input = input.clone();
    let mut options = next_input.options.unwrap_or_default();
    options.finalize = Some(true);
    next_input.options = Some(options);
    compute_endpoint_drag(&next_input)
}

pub fn recompute_after_bindable_change(input: &RecomputeAfterBindableChangeInput) -> EngineResult {
    let bindables_by_id = bindables_by_id(&input.bindables);
    let mut start_binding = input.arrow.start_binding;
    let mut end_binding = input.arrow.end_binding;
    let mut bindable_patches = Vec::new();
    let mut events = Vec::new();

    if let Some(binding) = start_binding.as_ref()
        && !bindables_by_id.contains_key(&binding.element_id)
    {
        bindable_patches.push(BindablePatch {
            id: binding.element_id,
            add_bound_arrow_id: None,
            remove_bound_arrow_id: Some(input.arrow.id),
        });
        events.push(ArrowEngineEvent::BindingBroken {
            arrow_id: input.arrow.id,
            edge: crate::ArrowEndpointEdge::Start,
        });
        start_binding = None;
    }
    if let Some(binding) = end_binding.as_ref()
        && !bindables_by_id.contains_key(&binding.element_id)
    {
        bindable_patches.push(BindablePatch {
            id: binding.element_id,
            add_bound_arrow_id: None,
            remove_bound_arrow_id: Some(input.arrow.id),
        });
        events.push(ArrowEngineEvent::BindingBroken {
            arrow_id: input.arrow.id,
            edge: crate::ArrowEndpointEdge::End,
        });
        end_binding = None;
    }

    let mut next_points = input.arrow.points.clone();
    let should_update_start = start_binding.as_ref().is_some_and(|binding| {
        input
            .changed_bindable_ids
            .as_ref()
            .is_none_or(|ids| ids.is_empty() || ids.iter().any(|id| id == &binding.element_id))
    });
    let should_update_end = end_binding.as_ref().is_some_and(|binding| {
        input
            .changed_bindable_ids
            .as_ref()
            .is_none_or(|ids| ids.is_empty() || ids.iter().any(|id| id == &binding.element_id))
    });

    let simulated_arrow = ArrowState {
        points: next_points.clone(),
        start_binding,
        end_binding,
        ..input.arrow.clone()
    };

    if should_update_start
        && let Some(binding) = start_binding.as_ref()
        && let Some(updated) =
            recompute_endpoint_from_binding(&simulated_arrow, binding, &bindables_by_id)
    {
        next_points[0] = updated;
    }

    let simulated_arrow = ArrowState {
        points: next_points.clone(),
        start_binding,
        end_binding,
        ..input.arrow.clone()
    };

    if should_update_end
        && let Some(binding) = end_binding.as_ref()
        && let Some(updated) =
            recompute_endpoint_from_binding(&simulated_arrow, binding, &bindables_by_id)
    {
        let end_index = next_points.len().saturating_sub(1);
        next_points[end_index] = updated;
    }

    let origin = next_points.first().copied().unwrap_or([0.0, 0.0]);
    let move_mid_points_with_element = input
        .options
        .as_ref()
        .and_then(|options| options.move_mid_points_with_element)
        .unwrap_or(false);
    let last_index = next_points.len().saturating_sub(1);
    let normalized_points = next_points
        .iter()
        .enumerate()
        .map(|(index, point)| {
            if move_mid_points_with_element && index != 0 && index != last_index {
                *point
            } else {
                [point[0] - origin[0], point[1] - origin[1]]
            }
        })
        .collect::<Vec<_>>();
    let (mut min_x, mut max_x): (f64, f64) = (0.0, 0.0);
    let (mut min_y, mut max_y): (f64, f64) = (0.0, 0.0);
    for point in &normalized_points {
        min_x = min_x.min(point[0]);
        max_x = max_x.max(point[0]);
        min_y = min_y.min(point[1]);
        max_y = max_y.max(point[1]);
    }

    let base_patch = ArrowPatch {
        x: Some(input.arrow.x + origin[0]),
        y: Some(input.arrow.y + origin[1]),
        width: Some(max_x - min_x),
        height: Some(max_y - min_y),
        points: Some(normalized_points),
        start_binding: Some(start_binding),
        end_binding: Some(end_binding),
        fixed_segments: None,
        start_is_special: None,
        end_is_special: None,
    };

    let arrow_with_base = apply_arrow_patch_internal(&input.arrow, &base_patch);
    if !arrow_with_base.elbowed {
        return EngineResult {
            arrow_patch: base_patch,
            bindable_patches,
            suggested_binding: None,
            events,
        };
    }

    let elbow_patch = if input
        .arrow
        .fixed_segments
        .as_ref()
        .is_some_and(|segments| !segments.is_empty())
    {
        let end_index = next_points.len().saturating_sub(1);
        update_elbow_arrow_patch_internal(&UpdateElbowArrowInput {
            arrow: input.arrow.clone(),
            updates: ElbowUpdatePatch {
                points: Some(vec![next_points[0], next_points[end_index]]),
                start_binding: Some(start_binding),
                end_binding: Some(end_binding),
                ..ElbowUpdatePatch::default()
            },
            bindables: input.bindables.clone(),
            context: input.context,
            options: Some(UpdateElbowArrowOptions {
                is_dragging: Some(false),
                validate_invariants: None,
            }),
        })
    } else {
        recompute_elbow_patch_internal(&RecomputeElbowInput {
            arrow: arrow_with_base,
            bindables: input.bindables.clone(),
            context: input.context,
        })
    };

    EngineResult {
        arrow_patch: merge_arrow_patches(base_patch, elbow_patch),
        bindable_patches,
        suggested_binding: None,
        events,
    }
}

pub fn compute_focus_drag(input: &ComputeFocusPointDragInput) -> EngineResult {
    compute_focus_point_drag(input)
}

pub fn validate_arrow_invariant(arrow: &ArrowState) -> ValidationReport {
    let mut violations = Vec::new();
    if arrow.points.len() < 2 {
        violations.push("arrow must contain at least two points".to_owned());
    }

    if let Some(first_point) = arrow.points.first().copied()
        && !points_equal(first_point, [0.0, 0.0], 1e-6)
    {
        violations.push("arrow points must be normalized with [0,0] as first point".to_owned());
    }

    if arrow.elbowed {
        if !validate_elbow_points(&arrow.points, 1.0) {
            violations.push("elbow arrow must keep orthogonal segments".to_owned());
        }
        violations.extend(validate_elbow_invariant(arrow));
    }

    ValidationReport {
        valid: violations.is_empty(),
        violations,
    }
}
