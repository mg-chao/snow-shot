use crate::{ArrowPatch, ArrowState};

pub(crate) fn apply_arrow_patch_internal(arrow: &ArrowState, patch: &ArrowPatch) -> ArrowState {
    ArrowState {
        id: arrow.id,
        x: patch.x.unwrap_or(arrow.x),
        y: patch.y.unwrap_or(arrow.y),
        width: patch.width.unwrap_or(arrow.width),
        height: patch.height.unwrap_or(arrow.height),
        points: patch.points.clone().unwrap_or_else(|| arrow.points.clone()),
        start_binding: patch.start_binding.unwrap_or(arrow.start_binding),
        end_binding: patch.end_binding.unwrap_or(arrow.end_binding),
        start_arrowhead: arrow.start_arrowhead,
        end_arrowhead: arrow.end_arrowhead,
        elbowed: arrow.elbowed,
        fixed_segments: patch
            .fixed_segments
            .clone()
            .unwrap_or_else(|| arrow.fixed_segments.clone()),
        start_is_special: patch.start_is_special.unwrap_or(arrow.start_is_special),
        end_is_special: patch.end_is_special.unwrap_or(arrow.end_is_special),
    }
}
