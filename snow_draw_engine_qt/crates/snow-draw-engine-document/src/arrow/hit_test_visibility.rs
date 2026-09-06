use super::*;

pub fn is_bindable_background_opaque(bindable: &BindableState) -> bool {
    bindable.background_opaque != Some(false)
}

pub fn is_bindable_binding_enabled(bindable: &BindableState) -> bool {
    bindable.binding_enabled != Some(false)
}

pub fn is_bindable_interior_hit_enabled(bindable: &BindableState) -> bool {
    bindable.interior_hit_enabled != Some(false)
}

fn has_finite_z_index(bindable: &BindableState) -> bool {
    bindable.z_index.is_some_and(|z_index| z_index.is_finite())
}

fn is_point_within_bounds(point: Point, bounds: Bounds) -> bool {
    point[0] >= bounds[0] && point[1] >= bounds[1] && point[0] <= bounds[2] && point[1] <= bounds[3]
}

pub fn is_bindable_visible_at_point(point: Point, bindable: &BindableState) -> bool {
    bindable
        .visibility_bounds
        .is_none_or(|visibility_bounds| is_point_within_bounds(point, visibility_bounds))
}

pub fn sort_bindables_by_z_index(bindables: &[BindableState]) -> Vec<BindableState> {
    let has_explicit_z_order = bindables.iter().any(has_finite_z_index);
    if !has_explicit_z_order {
        return bindables.to_vec();
    }

    let mut entries = bindables
        .iter()
        .cloned()
        .enumerate()
        .map(|(index, bindable)| {
            let z_index = bindable.z_index.unwrap_or(index as f64);
            (bindable, index, z_index)
        })
        .collect::<Vec<_>>();
    entries.sort_by(|left, right| {
        left.2
            .partial_cmp(&right.2)
            .unwrap_or(std::cmp::Ordering::Equal)
            .then(left.1.cmp(&right.1))
    });
    entries.into_iter().map(|entry| entry.0).collect()
}
