use super::*;

impl Editor {
    pub(crate) fn active_cursor_for_selection_state(
        &self,
        _document: &DocumentModel,
        state: &EditSelectionState,
        _canvas_point: Point<f64>,
        _modifiers: Modifiers,
    ) -> CursorStyle {
        match state.mode {
            SelectionEditMode::Move { .. } => CursorStyle::Move,
            SelectionEditMode::Resize { handle, .. } => {
                resize_cursor_for_handle(handle, state.preview_bounds.rotation)
            }
            SelectionEditMode::Rotate { .. } => CursorStyle::Grabbing,
            SelectionEditMode::CornerRadius { .. } => CursorStyle::CornerRadius,
        }
    }

    pub(crate) fn commit_marquee_selection(
        &mut self,
        document: &DocumentModel,
        state: MarqueeSelectionState,
        end_canvas_position: Point<f64>,
    ) {
        let Some(bounds) = axis_aligned_bounds(state.start_canvas_position, end_canvas_position)
        else {
            if !state.additive {
                self.clear_selection();
            }
            return;
        };

        let matched_ids = self.selection_ids_intersecting_bounds(
            document,
            bounds,
            self.tool_policy().selection_scope,
        );
        if state.additive {
            let mut next_ids = state.base_selection.ids;
            for id in matched_ids.iter().copied() {
                if !next_ids.contains(&id) {
                    next_ids.push(id);
                }
            }
            let primary = matched_ids.last().copied().or(state.base_selection.primary);
            self.set_selection_state_with_document(Some(document), next_ids, primary);
            return;
        }

        let primary = matched_ids.last().copied();
        self.set_selection_state_with_document(Some(document), matched_ids, primary);
    }

    pub(crate) fn hover_cursor_for_canvas_point(
        &self,
        document: &DocumentModel,
        policy: ToolPolicy,
        canvas_point: Point<f64>,
    ) -> CursorStyle {
        self.hover_feedback_for_canvas_point(document, policy, canvas_point)
            .0
    }

    pub(crate) fn hover_feedback_for_canvas_point(
        &self,
        document: &DocumentModel,
        policy: ToolPolicy,
        canvas_point: Point<f64>,
    ) -> (CursorStyle, Option<ElementId>) {
        let intent = self.resolve_primary_pointer_intent(
            document,
            policy,
            canvas_point,
            Modifiers::default(),
        );
        self.hover_feedback_for_primary_pointer_intent(document, policy, intent)
    }

    pub(crate) fn selection_ids_intersecting_bounds(
        &self,
        document: &DocumentModel,
        bounds: AxisAlignedBounds,
        scope: ToolSelectionScope,
    ) -> Vec<ElementId> {
        let mut ids = Vec::new();
        for id in document.paint_order() {
            let Ok(element) = document.element(*id) else {
                continue;
            };
            if !element.meta.visible {
                continue;
            }
            let kind = element.data.kind();
            if !Self::selection_scope_matches_document(document, scope, *id, kind) {
                continue;
            }
            if let Some(rect) = document.element_rect_proxy(*id)
                && rectangle_intersects_axis_aligned_bounds(&rect, bounds)
            {
                ids.push(*id);
            }
        }
        document.for_each_visible_arrow(|id, arrow| {
            if Self::selection_scope_matches(scope, arrow.element_kind())
                && draw_rect_intersects_axis_aligned_bounds(arrow_bounds(arrow), bounds)
            {
                ids.push(id);
            }
        });
        ids
    }
}
