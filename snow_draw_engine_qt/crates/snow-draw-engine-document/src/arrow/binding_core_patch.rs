use super::decisions::{
    EndpointBindingDecision, EndpointBindingDisposition, get_endpoint_binding_decisions,
    normalize_dragged_points, to_binding,
};
use super::*;

fn add_or_remove_binding_patch(
    arrow: &ArrowState,
    edge: ArrowEndpointEdge,
    decision: &EndpointBindingDecision,
    previous_binding: Option<&FixedPointBinding>,
    bindable_patches: &mut Vec<BindablePatch>,
    events: &mut Vec<ArrowEngineEvent>,
    reorder_target_ids: &mut BTreeSet<ElementId>,
) -> Option<Option<FixedPointBinding>> {
    match decision.disposition {
        EndpointBindingDisposition::Unchanged => None,
        EndpointBindingDisposition::Clear => {
            if let Some(previous_binding) = previous_binding {
                bindable_patches.push(BindablePatch {
                    id: previous_binding.element_id,
                    add_bound_arrow_id: None,
                    remove_bound_arrow_id: Some(arrow.id),
                });
                events.push(ArrowEngineEvent::BindingBroken {
                    arrow_id: arrow.id,
                    edge,
                });
            }
            Some(None)
        }
        EndpointBindingDisposition::Bind => {
            let mode = decision.strategy.mode?;
            let Some(element) = decision.strategy.element.as_ref() else {
                return previous_binding.cloned().map(Some);
            };
            let Some(focus_point) = decision.strategy.focus_point else {
                return previous_binding.cloned().map(Some);
            };

            let binding = to_binding(arrow, edge, element, mode, focus_point);
            if previous_binding.is_none()
                || previous_binding
                    .is_some_and(|previous| previous.element_id != binding.element_id)
            {
                if let Some(previous_binding) = previous_binding {
                    bindable_patches.push(BindablePatch {
                        id: previous_binding.element_id,
                        add_bound_arrow_id: None,
                        remove_bound_arrow_id: Some(arrow.id),
                    });
                }
                bindable_patches.push(BindablePatch {
                    id: binding.element_id,
                    add_bound_arrow_id: Some(arrow.id),
                    remove_bound_arrow_id: None,
                });
                if reorder_target_ids.insert(binding.element_id) {
                    events.push(ArrowEngineEvent::ReorderArrow {
                        arrow_id: arrow.id,
                        bindable_id: binding.element_id,
                    });
                }
            }
            Some(Some(binding))
        }
    }
}

pub fn compute_simple_binding_patch(input: &ComputeEndpointDragInput) -> EngineResult {
    let arrow = &input.arrow;
    let bindables = &input.bindables;
    let context = &input.context;
    let dragged_points = normalize_dragged_points(arrow, &input.dragged_points);
    let start_index = 0usize;
    let end_index = arrow.points.len().saturating_sub(1);
    let start_dragged = dragged_points.contains_key(&start_index);
    let end_dragged = dragged_points.contains_key(&end_index);
    let decisions = get_endpoint_binding_decisions(input);
    let bindables_by_id = bindables
        .iter()
        .map(|bindable| (bindable.id, bindable.clone()))
        .collect::<BTreeMap<_, _>>();

    let mut bindable_patches = Vec::new();
    let mut events = Vec::new();
    let mut reorder_target_ids = BTreeSet::new();
    let next_start_binding = add_or_remove_binding_patch(
        arrow,
        ArrowEndpointEdge::Start,
        &decisions.start,
        arrow.start_binding.as_ref(),
        &mut bindable_patches,
        &mut events,
        &mut reorder_target_ids,
    );
    let next_end_binding = add_or_remove_binding_patch(
        arrow,
        ArrowEndpointEdge::End,
        &decisions.end,
        arrow.end_binding.as_ref(),
        &mut bindable_patches,
        &mut events,
        &mut reorder_target_ids,
    );

    let mut effective_start_binding = next_start_binding.unwrap_or(arrow.start_binding);
    let mut effective_end_binding = next_end_binding.unwrap_or(arrow.end_binding);

    let mut next_points = arrow.points.clone();
    for (index, point) in &dragged_points {
        next_points[*index] = *point;
    }

    let simulated_arrow = ArrowState {
        points: next_points.clone(),
        start_binding: effective_start_binding,
        end_binding: effective_end_binding,
        ..arrow.clone()
    };

    let start_bindable = effective_start_binding
        .as_ref()
        .and_then(|binding| bindables_by_id.get(&binding.element_id));
    let end_bindable = effective_end_binding
        .as_ref()
        .and_then(|binding| bindables_by_id.get(&binding.element_id));

    let suggested_binding = if let (Some(element), Some(focus_point)) = (
        decisions.start.strategy.element.as_ref(),
        decisions.start.strategy.focus_point,
    ) {
        let focus_point = if start_dragged {
            dragged_points
                .get(&start_index)
                .copied()
                .map(|point| to_global_point(arrow, point))
                .unwrap_or(focus_point)
        } else {
            focus_point
        };
        Some(SuggestedBinding {
            bindable_id: decisions.start.strategy.bindable_id,
            element: element.clone(),
            mid_point: get_snap_outline_mid_point(focus_point, element, context.zoom),
        })
    } else if let (Some(element), Some(focus_point)) = (
        decisions.end.strategy.element.as_ref(),
        decisions.end.strategy.focus_point,
    ) {
        let focus_point = if end_dragged {
            dragged_points
                .get(&end_index)
                .copied()
                .map(|point| to_global_point(arrow, point))
                .unwrap_or(focus_point)
        } else {
            focus_point
        };
        Some(SuggestedBinding {
            bindable_id: decisions.end.strategy.bindable_id,
            element: element.clone(),
            mid_point: get_snap_outline_mid_point(focus_point, element, context.zoom),
        })
    } else {
        None
    };

    let complex_bindings = input
        .options
        .as_ref()
        .and_then(|options| options.complex_bindings)
        .unwrap_or(false);
    let start_is_dragging_over_end_element =
        arrow.end_binding.as_ref().is_some_and(|end_binding| {
            effective_start_binding
                .as_ref()
                .is_some_and(|start_binding| {
                    start_dragged && start_binding.element_id == end_binding.element_id
                })
        });
    let end_is_dragging_over_start_element =
        arrow.start_binding.as_ref().is_some_and(|start_binding| {
            effective_end_binding.as_ref().is_some_and(|end_binding| {
                end_dragged && start_binding.element_id == end_binding.element_id
            })
        });

    if let (Some(end_bindable), Some(end_binding)) = (end_bindable, effective_end_binding.as_ref())
    {
        let end_index = next_points.len() - 1;
        let updated_end = if start_is_dragging_over_end_element {
            next_points[end_index]
        } else if end_is_dragging_over_start_element
            && context.bind_mode != BindMode::Inside
            && complex_bindings
        {
            next_points[0]
        } else {
            update_bound_point(
                &simulated_arrow,
                ArrowEndpointSelector::EndBinding,
                Some(end_binding),
                end_bindable,
                &bindables_by_id,
                end_dragged,
            )
            .unwrap_or(next_points[end_index])
        };
        next_points[end_index] = updated_end;
        let updated_end_global = to_global_point(&simulated_arrow, updated_end);
        effective_end_binding = Some(FixedPointBinding {
            fixed_point: calculate_fixed_point_for_binding(end_bindable, updated_end_global),
            ..*end_binding
        });
    }

    let simulated_with_end = ArrowState {
        points: next_points.clone(),
        end_binding: effective_end_binding,
        ..simulated_arrow.clone()
    };

    if let (Some(start_bindable), Some(start_binding)) =
        (start_bindable, effective_start_binding.as_ref())
    {
        let end_index = next_points.len() - 1;
        let updated_start = if end_is_dragging_over_start_element && complex_bindings {
            next_points[0]
        } else if start_is_dragging_over_end_element
            && context.bind_mode != BindMode::Inside
            && complex_bindings
        {
            next_points[end_index]
        } else {
            update_bound_point(
                &simulated_with_end,
                ArrowEndpointSelector::StartBinding,
                Some(start_binding),
                start_bindable,
                &bindables_by_id,
                start_dragged,
            )
            .unwrap_or(next_points[0])
        };
        next_points[0] = updated_start;
        let updated_start_global = to_global_point(&simulated_with_end, updated_start);
        effective_start_binding = Some(FixedPointBinding {
            fixed_point: calculate_fixed_point_for_binding(start_bindable, updated_start_global),
            ..*start_binding
        });
    }

    let origin = next_points.first().copied().unwrap_or([0.0, 0.0]);
    let normalized_points = next_points
        .iter()
        .map(|point| [point[0] - origin[0], point[1] - origin[1]])
        .collect::<Vec<_>>();
    let (width, height) = compute_bounds_from_points(&normalized_points);

    EngineResult {
        arrow_patch: ArrowPatch {
            x: Some(arrow.x + origin[0]),
            y: Some(arrow.y + origin[1]),
            width: Some(width),
            height: Some(height),
            points: Some(normalized_points),
            start_binding: Some(effective_start_binding),
            end_binding: Some(effective_end_binding),
            fixed_segments: None,
            start_is_special: None,
            end_is_special: None,
        },
        bindable_patches,
        suggested_binding,
        events,
    }
}
