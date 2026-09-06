use super::*;

impl Engine {
    pub fn process_pointer_move_batch_with_viewport_changes(
        &mut self,
        id: ViewportId,
        events: &[InputEvent],
    ) -> Result<InputUpdate, ErrorCode> {
        if events.is_empty()
            || events.iter().any(|event| {
                !matches!(
                    event,
                    InputEvent::Pointer(pointer)
                        if pointer.event_type
                            == snow_draw_engine_interaction::PointerEventType::Move
                )
            })
        {
            return Err(ErrorCode::InvalidArgument);
        }

        let before_scene_revision = self.editor.scene_input_revision();
        let before_overlay_revision = self.editor.overlay_input_revision();
        let before_view = self.viewport_slot(id)?.view;
        let mut interaction = InteractionOutput::default();
        for event in events.iter().copied() {
            let update = {
                let model = &self.model;
                let slot = self.viewports.get_mut(&id).ok_or(ErrorCode::NotFound)?;
                self.editor.process_input(model, &mut slot.view, event)?
            };
            if update.command.is_some() {
                return Err(ErrorCode::InvalidArgument);
            }
            interaction = update.interaction;
        }

        let changed_viewports = if self.editor.scene_input_revision() != before_scene_revision
            || self.editor.overlay_input_revision() != before_overlay_revision
        {
            self.refresh_all_viewports()?.changed_viewports
        } else if self.viewport_slot(id)?.view != before_view {
            self.refresh_single_viewport(id)?.changed_viewports
        } else {
            Vec::new()
        };
        Ok(InputUpdate {
            interaction,
            changed_viewports,
        })
    }

    pub fn process_input(
        &mut self,
        id: ViewportId,
        event: InputEvent,
    ) -> Result<InteractionOutput, ErrorCode> {
        self.process_input_with_viewport_changes(id, event)
            .map(|update| update.interaction)
    }

    pub fn process_input_with_viewport_changes(
        &mut self,
        id: ViewportId,
        event: InputEvent,
    ) -> Result<InputUpdate, ErrorCode> {
        let before_scene_revision = self.editor.scene_input_revision();
        let before_overlay_revision = self.editor.overlay_input_revision();
        let before_view = self.viewport_slot(id)?.view;
        let update = {
            let model = &self.model;
            let slot = self.viewports.get_mut(&id).ok_or(ErrorCode::NotFound)?;
            self.editor.process_input(model, &mut slot.view, event)?
        };
        if let Some(command) = update.command {
            let result = self.apply_editor_command(id, command)?;
            Ok(InputUpdate {
                interaction: update.interaction,
                changed_viewports: result.changed_viewports,
            })
        } else if self.editor.scene_input_revision() != before_scene_revision
            || self.editor.overlay_input_revision() != before_overlay_revision
        {
            let result = self.refresh_all_viewports()?;
            Ok(InputUpdate {
                interaction: update.interaction,
                changed_viewports: result.changed_viewports,
            })
        } else if self.viewport_slot(id)?.view != before_view {
            let result = self.refresh_single_viewport(id)?;
            Ok(InputUpdate {
                interaction: update.interaction,
                changed_viewports: result.changed_viewports,
            })
        } else {
            Ok(InputUpdate {
                interaction: update.interaction,
                changed_viewports: Vec::new(),
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use snow_draw_engine_interaction::{
        Modifiers, PointerButton, PointerButtons, PointerDevice, PointerEvent, PointerEventType,
    };

    fn pointer(event_type: PointerEventType, x: f64, y: f64) -> InputEvent {
        InputEvent::Pointer(PointerEvent {
            pointer_id: 1,
            event_type,
            device: PointerDevice::Mouse,
            position: Point::new(x, y),
            button: (event_type == PointerEventType::Down).then_some(PointerButton::Primary),
            buttons: PointerButtons(PointerButtons::PRIMARY),
            modifiers: Modifiers::default(),
        })
    }

    #[test]
    fn pointer_move_batch_refreshes_the_composer_once() {
        let mut engine = Engine::default();
        let viewport = engine.create_viewport(ViewportConfig::default()).unwrap();
        engine
            .set_viewport_surface_size(viewport, 800, 600)
            .unwrap();
        engine
            .set_viewport_active_tool(viewport, ActiveTool::FreeDraw)
            .unwrap();
        engine
            .process_input_with_viewport_changes(
                viewport,
                pointer(PointerEventType::Down, 100.0, 100.0),
            )
            .unwrap();
        let before = engine
            .viewport_slot(viewport)
            .unwrap()
            .composer
            .current_cursor();
        let moves = (1..=32)
            .map(|index| pointer(PointerEventType::Move, 100.0 + index as f64, 120.0))
            .collect::<Vec<_>>();

        let update = engine
            .process_pointer_move_batch_with_viewport_changes(viewport, &moves)
            .unwrap();
        let after = engine
            .viewport_slot(viewport)
            .unwrap()
            .composer
            .current_cursor();

        assert_eq!(update.changed_viewports, vec![viewport]);
        assert_eq!(after.scene_revision.0, before.scene_revision.0 + 1);
    }

    #[test]
    fn pen_filter_batch_refreshes_once_and_commits_simplified_geometry() {
        let mut engine = Engine::default();
        let viewport = engine.create_viewport(ViewportConfig::default()).unwrap();
        engine
            .set_viewport_surface_size(viewport, 800, 600)
            .unwrap();
        engine
            .set_viewport_active_tool(viewport, ActiveTool::PenFilter)
            .unwrap();
        engine
            .process_input_with_viewport_changes(
                viewport,
                pointer(PointerEventType::Down, 100.0, 100.0),
            )
            .unwrap();
        let before = engine
            .viewport_slot(viewport)
            .unwrap()
            .composer
            .current_cursor();
        let moves = (1..=128)
            .map(|index| pointer(PointerEventType::Move, 100.0 + index as f64, 120.0))
            .collect::<Vec<_>>();

        engine
            .process_pointer_move_batch_with_viewport_changes(viewport, &moves)
            .unwrap();
        let after = engine
            .viewport_slot(viewport)
            .unwrap()
            .composer
            .current_cursor();
        assert_eq!(after.scene_revision.0, before.scene_revision.0 + 1);

        engine
            .process_input_with_viewport_changes(
                viewport,
                pointer(PointerEventType::Up, 240.0, 120.0),
            )
            .unwrap();
        let id = engine.model.paint_order()[0];
        let points = engine.model.pen_filter(id).unwrap().global_points();
        assert_eq!(points[0], Point::new(-300.0, -200.0));
        assert_eq!(points.last(), Some(&Point::new(-160.0, -180.0)));
        assert!(
            points.len() <= 4,
            "straight input should collapse to its turns"
        );
    }

    #[test]
    fn pointer_move_batch_rejects_other_events_before_mutation() {
        let mut engine = Engine::default();
        let viewport = engine.create_viewport(ViewportConfig::default()).unwrap();
        let before = engine.editor.snapshot();

        assert_eq!(
            engine.process_pointer_move_batch_with_viewport_changes(
                viewport,
                &[pointer(PointerEventType::Down, 10.0, 10.0)],
            ),
            Err(ErrorCode::InvalidArgument)
        );
        assert_eq!(engine.editor.snapshot(), before);
    }
}
