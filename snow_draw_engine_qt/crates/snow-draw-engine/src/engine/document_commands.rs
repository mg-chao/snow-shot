use super::*;

impl Engine {
    pub fn reset_editing_state_with_viewport_changes(
        &mut self,
        source_viewport_id: ViewportId,
    ) -> Result<MutationResult, ErrorCode> {
        self.ensure_viewport(source_viewport_id)?;
        let before = self.editor.snapshot();
        self.editor.reset_editing_state();
        self.refresh_after_session_mutation(before)
    }

    pub fn select_element_with_viewport_changes(
        &mut self,
        source_viewport_id: ViewportId,
        id: ElementId,
    ) -> Result<MutationResult, ErrorCode> {
        self.ensure_viewport(source_viewport_id)?;
        let before = self.editor.snapshot();
        self.editor.select_element(&self.model, id)?;
        self.refresh_after_session_mutation(before)
    }

    pub fn selected_ids(&self) -> Vec<ElementId> {
        self.editor.selected_ids()
    }

    pub fn delete_selected_with_viewport_changes(
        &mut self,
        source_viewport_id: ViewportId,
    ) -> Result<MutationResult, ErrorCode> {
        self.ensure_viewport(source_viewport_id)?;
        let before = self.editor.snapshot();
        let command = self.editor.delete_selected(&self.model)?;
        if let Some(command) = command {
            self.apply_editor_command(source_viewport_id, command)
        } else {
            self.refresh_after_session_mutation(before)
        }
    }

    pub fn duplicate_selected_with_viewport_changes(
        &mut self,
        source_viewport_id: ViewportId,
        offset: Point<f64>,
    ) -> Result<MutationResult, ErrorCode> {
        self.ensure_viewport(source_viewport_id)?;
        let before = self.editor.snapshot();
        let command = self.editor.duplicate_selected(&self.model, offset)?;
        if let Some(command) = command {
            self.apply_editor_command(source_viewport_id, command)
        } else {
            self.refresh_after_session_mutation(before)
        }
    }

    pub fn reorder_selected_with_viewport_changes(
        &mut self,
        source_viewport_id: ViewportId,
        action: u32,
    ) -> Result<MutationResult, ErrorCode> {
        self.ensure_viewport(source_viewport_id)?;
        let before = self.editor.snapshot();
        let command = self.editor.reorder_selected(&self.model, action)?;
        if let Some(command) = command {
            self.apply_editor_command(source_viewport_id, command)
        } else {
            self.refresh_after_session_mutation(before)
        }
    }

    pub fn set_selected_opacity_with_viewport_changes(
        &mut self,
        source_viewport_id: ViewportId,
        opacity: f64,
    ) -> Result<MutationResult, ErrorCode> {
        self.ensure_viewport(source_viewport_id)?;
        let before = self.editor.snapshot();
        let command = self.editor.set_selected_opacity(&self.model, opacity)?;
        if let Some(command) = command {
            self.apply_editor_command(source_viewport_id, command)
        } else {
            self.refresh_after_session_mutation(before)
        }
    }

    pub fn adjust_selected_serial_numbers_with_viewport_changes(
        &mut self,
        source_viewport_id: ViewportId,
        delta: i64,
    ) -> Result<MutationResult, ErrorCode> {
        self.ensure_viewport(source_viewport_id)?;
        let before = self.editor.snapshot();
        let command = self
            .editor
            .adjust_selected_serial_numbers(&self.model, delta)?;
        if let Some(command) = command {
            self.apply_editor_command(source_viewport_id, command)
        } else {
            self.refresh_after_session_mutation(before)
        }
    }
}
