use super::*;

impl Engine {
    pub fn history_state(&self) -> HistoryState {
        HistoryState {
            can_undo: self.history.can_undo(),
            can_redo: self.history.can_redo(),
        }
    }

    pub fn undo(&mut self) -> Result<bool, ErrorCode> {
        self.undo_with_viewport_changes()
            .map(|result| !result.changed_viewports.is_empty())
    }

    pub fn undo_with_viewport_changes(&mut self) -> Result<MutationResult, ErrorCode> {
        let Some(history_result) = self.history.undo(&mut self.model)? else {
            return Ok(MutationResult::default());
        };
        Ok(self.finish_document_change(
            &history_result.snapshot,
            &history_result.apply_result.changes,
        ))
    }

    pub fn redo(&mut self) -> Result<bool, ErrorCode> {
        self.redo_with_viewport_changes()
            .map(|result| !result.changed_viewports.is_empty())
    }

    pub fn redo_with_viewport_changes(&mut self) -> Result<MutationResult, ErrorCode> {
        let Some(history_result) = self.history.redo(&mut self.model)? else {
            return Ok(MutationResult::default());
        };
        Ok(self.finish_document_change(
            &history_result.snapshot,
            &history_result.apply_result.changes,
        ))
    }

    pub(super) fn apply_editor_command(
        &mut self,
        source_viewport_id: ViewportId,
        command: EditorCommand,
    ) -> Result<MutationResult, ErrorCode> {
        match command {
            EditorCommand::ApplyTransaction(command) => {
                self.commit_transaction(source_viewport_id, command)
            }
            EditorCommand::Undo => self.undo_with_viewport_changes(),
            EditorCommand::Redo => self.redo_with_viewport_changes(),
        }
    }

    pub(super) fn commit_transaction(
        &mut self,
        _source_viewport_id: ViewportId,
        command: ApplyTransactionCommand,
    ) -> Result<MutationResult, ErrorCode> {
        let ApplyTransactionCommand {
            transaction,
            history_undo_snapshot,
        } = command;
        if transaction.is_empty() {
            return Ok(MutationResult::default());
        }
        let redo_snapshot = self.capture_session_snapshot();
        let undo_snapshot = history_undo_snapshot.unwrap_or_else(|| redo_snapshot.clone());
        let label = transaction.label().to_owned();
        let redo = transaction.clone();
        let apply_result = self.model.apply_transaction(transaction)?;
        let mutation_result = self.finish_document_change(&redo_snapshot, &apply_result.changes);
        self.history.push_committed(
            label,
            redo,
            apply_result.inverse.clone(),
            undo_snapshot,
            redo_snapshot,
        );
        Ok(mutation_result)
    }

    pub(super) fn capture_session_snapshot(&self) -> DocumentSyncSnapshot {
        self.editor.capture_document_sync_snapshot(&self.model)
    }

    pub(super) fn finish_document_change(
        &mut self,
        snapshot: &DocumentSyncSnapshot,
        changes: &snow_draw_engine_document::DocumentDelta,
    ) -> MutationResult {
        self.scene_cache.sync(&self.model, Some(changes));
        self.editor
            .sync_after_document_change(&self.model, snapshot);
        self.refresh_all_viewports().unwrap_or_default()
    }
}
