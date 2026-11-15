use super::*;
use snow_draw_engine_core::arrow::{ArrowType, BindMode};
use snow_draw_engine_document::{ArrowData, ElementData, Transaction};
use snow_draw_engine_editor::ElementCreationPreview;
use snow_draw_engine_interaction::{
    KeyCode, KeyEvent, KeyEventType, Modifiers, PointerButton, PointerButtons, PointerDevice,
    PointerEvent, PointerEventType,
};

const SHIFT: Modifiers = Modifiers {
    shift: true,
    ctrl: false,
    alt: false,
    meta: false,
};

fn setup(zoom: f64, arrow_type: ArrowType) -> (Engine, ViewportId) {
    let mut config = EngineConfig::default();
    config.style_defaults.editor.arrow.arrow_type = arrow_type;
    config.style_defaults.editor.arrow.stroke_width = 5.0;
    let mut engine = Engine::new(config);
    let view = engine.create_viewport(ViewportConfig::default()).unwrap();
    engine.set_viewport_surface_size(view, 800, 600).unwrap();
    engine
        .set_viewport_camera(
            view,
            Camera {
                center: Point::default(),
                zoom,
            },
        )
        .unwrap();
    engine
        .set_viewport_snap_config(
            view,
            SnapConfig {
                enabled: false,
                ..Default::default()
            },
        )
        .unwrap();
    engine
        .set_viewport_active_tool(view, ActiveTool::SerialNumber)
        .unwrap();
    (engine, view)
}

fn event(kind: PointerEventType, x: f64, y: f64, modifiers: Modifiers) -> PointerEvent {
    PointerEvent {
        pointer_id: 1,
        event_type: kind,
        device: PointerDevice::Mouse,
        position: Point::new(400.0 + x, 300.0 + y),
        button: Some(PointerButton::Primary),
        buttons: PointerButtons(PointerButtons::PRIMARY),
        modifiers,
        pressure: None,
        timestamp_ms: 0,
    }
}

fn pointer(
    engine: &mut Engine,
    view: ViewportId,
    kind: PointerEventType,
    x: f64,
    y: f64,
    modifiers: Modifiers,
) {
    engine
        .process_input(view, InputEvent::Pointer(event(kind, x, y, modifiers)))
        .unwrap();
}

fn preview(engine: &Engine, view: ViewportId) -> ArrowData {
    match engine
        .editor
        .presentation_state(&engine.model, &engine.viewport_slot(view).unwrap().view)
        .creation_preview
    {
        Some(ElementCreationPreview::Arrow(arrow)) => arrow,
        other => panic!("expected arrow preview, got {other:?}"),
    }
}

fn arrows(engine: &Engine) -> Vec<(ElementId, ArrowData)> {
    engine
        .model
        .paint_order()
        .iter()
        .filter_map(|id| {
            engine
                .model
                .arrow(*id)
                .ok()
                .map(|arrow| (*id, arrow.clone()))
        })
        .collect()
}

fn assert_bound(arrow: &ArrowData, serial: ElementId) {
    assert_eq!(
        arrow
            .start_binding
            .as_ref()
            .map(|binding| binding.element_id),
        Some(serial)
    );
    assert!(arrow.text_element_id.is_none());
    let binding = arrow.start_binding.as_ref().unwrap();
    assert_eq!(binding.mode, BindMode::Orbit);
    if arrow.arrow_type != ArrowType::Elbow {
        assert_eq!(binding.fixed_point, [0.5, 0.5]);
    }
}

fn assert_exterior_tail(engine: &Engine, arrow: &ArrowData, serial: ElementId) {
    assert_bound(arrow, serial);
    let badge = engine.model.serial_number(serial).unwrap();
    let tail = arrow.start();
    let dx = tail.x - badge.center.x;
    let dy = tail.y - badge.center.y;
    let (sin, cos) = badge.rotation.sin_cos();
    let local_x = dx * cos + dy * sin;
    let local_y = -dx * sin + dy * cos;
    assert!(
        local_x.abs().max(local_y.abs()) > badge.diameter / 2.0,
        "tail {tail:?} must stay outside badge {badge:?}"
    );
    if arrow.arrow_type != ArrowType::Elbow {
        let end = arrow.end();
        let ex = end.x - badge.center.x;
        let ey = end.y - badge.center.y;
        let distance_to_axis = (dx * ey - dy * ex).abs() / ex.hypot(ey);
        assert!(
            distance_to_axis < 0.02,
            "tail must aim at the badge center: {arrow:?}"
        );
    }
}

#[test]
fn new_sequence_arrow_previews_and_commits_release_position_and_style() {
    for zoom in [0.5, 1.0, 3.0] {
        for arrow_type in [ArrowType::Straight, ArrowType::Curve, ArrowType::Elbow] {
            let (mut engine, view) = setup(zoom, arrow_type);
            pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
            let serial = engine.model.paint_order()[0];
            let history = engine.history_state();
            pointer(&mut engine, view, PointerEventType::Move, 120.0, 0.0, SHIFT);
            assert_exterior_tail(&engine, &preview(&engine, view), serial);
            assert_eq!(engine.history_state(), history);
            assert_eq!(engine.model.paint_order(), &[serial]);
            pointer(&mut engine, view, PointerEventType::Up, 210.0, 0.0, SHIFT);
            let (arrow_id, arrow) = arrows(&engine).pop().unwrap();
            assert_exterior_tail(&engine, &arrow, serial);
            assert_eq!(arrow.arrow_type, arrow_type);
            assert_eq!(arrow.stroke_width, 5.0);
            assert!((arrow.end().x - 210.0 / zoom).abs() < 1e-6);
            assert!(arrow.end().y.abs() < 1e-6);
            assert_eq!(engine.model.paint_order().len(), 2);
            assert!(
                engine
                    .model
                    .serial_number(serial)
                    .unwrap()
                    .text_element_id
                    .is_none()
            );
            assert_eq!(engine.take_text_edit_request(view).unwrap(), None);
            assert_eq!(engine.editor.active_tool(), ActiveTool::SerialNumber);
            assert!(engine.undo().unwrap());
            assert_eq!(engine.model.paint_order(), &[serial]);
            assert!(engine.redo().unwrap());
            assert_eq!(engine.model.arrow(arrow_id).unwrap(), &arrow);
            let restored = Engine::from_serialized_document_session_with_config(
                &engine.serialize_document_session().unwrap(),
                Default::default(),
            )
            .unwrap();
            assert_eq!(restored.model.arrow(arrow_id).unwrap(), &arrow);
        }
    }
}

#[test]
fn sequence_arrow_modifiers_and_coalesced_release_follow_arrow_tool() {
    for release_modifiers in [
        SHIFT,
        Modifiers::default(),
        Modifiers {
            ctrl: true,
            ..SHIFT
        },
    ] {
        let (mut engine, view) = setup(1.0, ArrowType::Straight);
        pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
        let serial = engine.model.paint_order()[0];
        pointer(
            &mut engine,
            view,
            PointerEventType::Up,
            180.0,
            67.0,
            release_modifiers,
        );
        let arrow = arrows(&engine).pop().unwrap().1;
        assert_bound(&arrow, serial);
        let tip = arrow.end();
        if release_modifiers.shift {
            let steps = tip.y.atan2(tip.x) / (std::f64::consts::PI / 12.0);
            assert!(
                (steps - steps.round()).abs() < 1e-6,
                "angle must snap: {tip:?}"
            );
        } else {
            assert!((tip.x - 180.0).abs() < 1e-6 && (tip.y - 67.0).abs() < 1e-6);
        }
        assert_eq!(engine.take_text_edit_request(view).unwrap(), None);
    }
}

#[test]
fn existing_sequence_supports_shift_click_and_repeated_arrows_without_touching_text() {
    let (mut engine, view) = setup(1.0, ArrowType::Straight);
    pointer(
        &mut engine,
        view,
        PointerEventType::Down,
        0.0,
        0.0,
        Modifiers::default(),
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Up,
        100.0,
        100.0,
        Modifiers::default(),
    );
    let serial = engine.model.paint_order()[0];
    let original = engine.model.serial_number(serial).unwrap().clone();
    let text_id = original.text_element_id.unwrap();
    let text = engine.model.text(text_id).unwrap().clone();
    engine.take_text_edit_request(view).unwrap();
    assert!(engine.selected_ids().is_empty());
    for selected in [true, false] {
        pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
        pointer(&mut engine, view, PointerEventType::Up, 1.0, 0.0, SHIFT);
        assert_eq!(engine.selected_ids().contains(&serial), selected);
        assert!(arrows(&engine).is_empty());
    }
    for selected in [false, true] {
        if selected {
            engine
                .select_element_with_viewport_changes(view, serial)
                .unwrap();
        }
        pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
        pointer(
            &mut engine,
            view,
            PointerEventType::Move,
            -120.0,
            0.0,
            SHIFT,
        );
        pointer(&mut engine, view, PointerEventType::Up, -200.0, 0.0, SHIFT);
        assert_eq!(engine.model.serial_number(serial).unwrap(), &original);
        assert_eq!(engine.model.text(text_id).unwrap(), &text);
        assert_eq!(engine.take_text_edit_request(view).unwrap(), None);
    }
    let created = arrows(&engine);
    assert_eq!(created.len(), 2);
    for (_, arrow) in created {
        assert_bound(&arrow, serial);
    }
}

#[test]
fn sequence_arrow_clicks_short_drags_and_cancellation_leave_no_arrow() {
    for existing in [false, true] {
        for distance in [0.0, 2.0, 12.0] {
            let (mut engine, view) = setup(1.0, ArrowType::Straight);
            if existing {
                pointer(
                    &mut engine,
                    view,
                    PointerEventType::Down,
                    0.0,
                    0.0,
                    Modifiers::default(),
                );
                pointer(
                    &mut engine,
                    view,
                    PointerEventType::Up,
                    0.0,
                    0.0,
                    Modifiers::default(),
                );
            }
            pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
            pointer(
                &mut engine,
                view,
                PointerEventType::Move,
                distance,
                0.0,
                SHIFT,
            );
            pointer(
                &mut engine,
                view,
                PointerEventType::Up,
                distance,
                0.0,
                SHIFT,
            );
            pointer(&mut engine, view, PointerEventType::Move, 200.0, 0.0, SHIFT);
            assert_eq!(engine.model.paint_order().len(), 1);
            assert!(
                engine
                    .editor
                    .presentation_state(&engine.model, &engine.viewport_slot(view).unwrap().view)
                    .creation_preview
                    .is_none()
            );
        }
        for cancel in 0..4 {
            let (mut engine, view) = setup(1.0, ArrowType::Straight);
            if existing {
                pointer(
                    &mut engine,
                    view,
                    PointerEventType::Down,
                    0.0,
                    0.0,
                    Modifiers::default(),
                );
                pointer(
                    &mut engine,
                    view,
                    PointerEventType::Up,
                    0.0,
                    0.0,
                    Modifiers::default(),
                );
            }
            pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
            pointer(&mut engine, view, PointerEventType::Move, 150.0, 0.0, SHIFT);
            match cancel {
                0 => {
                    engine.process_input(view, InputEvent::FocusLost).unwrap();
                }
                1 => pointer(
                    &mut engine,
                    view,
                    PointerEventType::Cancel,
                    150.0,
                    0.0,
                    SHIFT,
                ),
                2 => {
                    engine
                        .process_input(
                            view,
                            InputEvent::Key(KeyEvent {
                                event_type: KeyEventType::KeyDown,
                                key_code: KeyCode::Escape,
                                modifiers: Modifiers::default(),
                                repeat: false,
                            }),
                        )
                        .unwrap();
                }
                _ => {
                    engine
                        .set_viewport_active_tool(view, ActiveTool::Select)
                        .unwrap();
                }
            }
            pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
            assert_eq!(engine.model.paint_order().len(), 1);
            assert_eq!(engine.take_text_edit_request(view).unwrap(), None);
        }
    }
}

#[test]
fn sequence_arrow_ignores_foreign_pointers_and_secondary_release() {
    for preview_started in [false, true] {
        let (mut engine, view) = setup(1.0, ArrowType::Straight);
        pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
        if preview_started {
            pointer(&mut engine, view, PointerEventType::Move, 100.0, 0.0, SHIFT);
        }
        for (id, kind, button) in [
            (2, PointerEventType::Move, PointerButton::Primary),
            (2, PointerEventType::Up, PointerButton::Primary),
            (1, PointerEventType::Up, PointerButton::Secondary),
        ] {
            let mut input = event(kind, 250.0, 0.0, SHIFT);
            input.pointer_id = id;
            input.button = Some(button);
            engine
                .process_input(view, InputEvent::Pointer(input))
                .unwrap();
            assert!(arrows(&engine).is_empty());
        }
        pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
        assert_eq!(arrows(&engine).len(), 1);
    }
}

#[test]
fn sequence_arrow_follows_badge_move_and_resize() {
    let (mut engine, view) = setup(1.0, ArrowType::Straight);
    pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
    pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
    let serial = engine.model.paint_order()[0];
    let (arrow_id, original) = arrows(&engine).pop().unwrap();
    engine
        .select_element_with_viewport_changes(view, serial)
        .unwrap();
    pointer(
        &mut engine,
        view,
        PointerEventType::Down,
        0.0,
        0.0,
        Modifiers::default(),
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Move,
        0.0,
        80.0,
        Modifiers::default(),
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Up,
        0.0,
        80.0,
        Modifiers::default(),
    );
    let moved = engine.model.arrow(arrow_id).unwrap().clone();
    assert_exterior_tail(&engine, &moved, serial);
    assert_ne!(moved.start(), original.start());
    assert_eq!(moved.end(), original.end());
    let handle = engine
        .acquire_patch(view, None)
        .unwrap()
        .overlay
        .ops
        .iter()
        .flat_map(|op| &op.insert_items)
        .filter_map(|item| match item {
            snow_draw_engine_display::OverlayDisplayItem::Rectangle(item)
                if item.kind == snow_draw_engine_display::UiShapeKind::SelectionResizeHandle =>
            {
                Some(Point::new(item.center_x, item.center_y))
            }
            _ => None,
        })
        .max_by(|a, b| (a.x + a.y).total_cmp(&(b.x + b.y)))
        .unwrap();
    pointer(
        &mut engine,
        view,
        PointerEventType::Down,
        handle.x,
        handle.y,
        SHIFT,
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Move,
        handle.x + 40.0,
        handle.y + 40.0,
        SHIFT,
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Up,
        handle.x + 40.0,
        handle.y + 40.0,
        SHIFT,
    );
    let resized = engine.model.arrow(arrow_id).unwrap().clone();
    assert_exterior_tail(&engine, &resized, serial);
    assert_ne!(resized.start(), moved.start());
    assert!(engine.undo().unwrap());
    assert_eq!(engine.model.arrow(arrow_id).unwrap(), &moved);
    assert!(engine.redo().unwrap());
    assert_eq!(engine.model.arrow(arrow_id).unwrap(), &resized);
}

#[test]
fn sequence_arrow_respects_selection_preferences_and_tool_scope() {
    for enabled in [false, true] {
        for excluded in [false, true] {
            let (mut engine, view) = setup(1.0, ArrowType::Straight);
            engine
                .set_automatically_select_newly_created_elements(enabled)
                .unwrap();
            engine
                .set_automatic_selection_excluded_tools(if excluded {
                    ActiveTool::SerialNumber.policy_bit()
                } else {
                    0
                })
                .unwrap();
            engine
                .set_quick_selection_disabled_tools(ActiveTool::SerialNumber.policy_bit())
                .unwrap();
            pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
            pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
            let arrow_id = arrows(&engine)[0].0;
            assert_eq!(
                engine.selected_ids().contains(&arrow_id),
                enabled && !excluded
            );
        }
    }
    let (mut engine, view) = setup(1.0, ArrowType::Straight);
    pointer(
        &mut engine,
        view,
        PointerEventType::Down,
        0.0,
        0.0,
        Modifiers::default(),
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Up,
        0.0,
        0.0,
        Modifiers::default(),
    );
    engine
        .set_viewport_active_tool(view, ActiveTool::Select)
        .unwrap();
    pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
    pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
    assert!(arrows(&engine).is_empty());
    assert_eq!(engine.model.paint_order().len(), 1);
}

#[test]
fn sequence_arrow_keeps_explicit_source_with_overlapping_bindables() {
    let (mut engine, view) = setup(1.0, ArrowType::Straight);
    pointer(
        &mut engine,
        view,
        PointerEventType::Down,
        0.0,
        0.0,
        Modifiers::default(),
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Up,
        0.0,
        0.0,
        Modifiers::default(),
    );
    let serial = engine.model.paint_order()[0];
    let rectangle_id = engine.model.peek_next_element_id();
    let mut rectangle = engine.model.element_rect_proxy(serial).unwrap();
    rectangle.width = 350.0;
    rectangle.height = 300.0;
    let mut transaction = Transaction::new("overlapping rectangle");
    transaction.insert_rectangle(rectangle_id, Default::default(), rectangle);
    engine
        .apply_editor_command(
            view,
            EditorCommand::ApplyTransaction(ApplyTransactionCommand {
                transaction,
                history_undo_snapshot: None,
                created_selection_target: None,
            }),
        )
        .unwrap();
    pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
    pointer(&mut engine, view, PointerEventType::Move, 400.0, 0.0, SHIFT);
    assert_bound(&preview(&engine, view), serial);
    pointer(&mut engine, view, PointerEventType::Up, 400.0, 0.0, SHIFT);
    let arrow = arrows(&engine).pop().unwrap().1;
    assert_bound(&arrow, serial);
    assert!(engine.model.paint_order().iter().all(|id| !matches!(
        engine.model.element(*id).unwrap().data,
        ElementData::Text(_)
    )));
}

#[test]
fn sequence_arrow_tip_binding_obeys_ctrl_without_unbinding_tail() {
    for arrow_type in [ArrowType::Straight, ArrowType::Curve, ArrowType::Elbow] {
        for ctrl in [false, true] {
            let (mut engine, view) = setup(1.0, arrow_type);
            pointer(
                &mut engine,
                view,
                PointerEventType::Down,
                220.0,
                0.0,
                Modifiers::default(),
            );
            pointer(
                &mut engine,
                view,
                PointerEventType::Up,
                220.0,
                0.0,
                Modifiers::default(),
            );
            let target = engine.model.paint_order()[0];
            pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
            let source = engine.model.paint_order()[1];
            pointer(
                &mut engine,
                view,
                PointerEventType::Up,
                220.0,
                0.0,
                Modifiers { ctrl, ..SHIFT },
            );
            let arrow = arrows(&engine).pop().unwrap().1;
            assert_bound(&arrow, source);
            assert_eq!(
                arrow.end_binding.as_ref().map(|binding| binding.element_id),
                (!ctrl).then_some(target)
            );
        }
    }
}

#[test]
fn sequence_arrow_choice_is_latched_on_press_and_preserves_creation_color() {
    let (mut engine, view) = setup(1.0, ArrowType::Straight);
    pointer(
        &mut engine,
        view,
        PointerEventType::Down,
        0.0,
        0.0,
        Modifiers::default(),
    );
    pointer(&mut engine, view, PointerEventType::Up, 180.0, 0.0, SHIFT);
    assert!(arrows(&engine).is_empty());
    assert!(engine.take_text_edit_request(view).unwrap().is_some());

    for update_before_preview in [false, true] {
        let (mut engine, view) = setup(1.0, ArrowType::Straight);
        let color = engine.editor.arrow_style(&engine.model).stroke;
        pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
        if !update_before_preview {
            pointer(&mut engine, view, PointerEventType::Move, 100.0, 0.0, SHIFT);
        }
        engine
            .set_creation_color(Some(snow_draw_engine_core::ColorRgba8 {
                r: 1,
                g: 2,
                b: 3,
                a: 255,
            }))
            .unwrap();
        pointer(&mut engine, view, PointerEventType::Move, 140.0, 0.0, SHIFT);
        assert_eq!(preview(&engine, view).stroke, color);
        pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
        assert_eq!(arrows(&engine)[0].1.stroke, color);
    }
}

#[test]
fn sequence_arrow_does_not_bypass_disabled_quick_selection() {
    let (mut engine, view) = setup(1.0, ArrowType::Straight);
    pointer(
        &mut engine,
        view,
        PointerEventType::Down,
        0.0,
        0.0,
        Modifiers::default(),
    );
    pointer(
        &mut engine,
        view,
        PointerEventType::Up,
        0.0,
        0.0,
        Modifiers::default(),
    );
    let original = engine.model.paint_order()[0];
    engine
        .set_quick_selection_disabled_tools(ActiveTool::SerialNumber.policy_bit())
        .unwrap();
    pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
    let new_number = engine.model.paint_order()[1];
    pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
    assert_bound(&arrows(&engine)[0].1, new_number);
    assert_ne!(new_number, original);
}

#[test]
fn sequence_arrow_exterior_tail_tracks_drag_direction_and_shift_release() {
    for arrow_type in [ArrowType::Straight, ArrowType::Curve, ArrowType::Elbow] {
        for number_type in [
            snow_draw_engine_document::SerialNumberType::OutlinedCircle,
            snow_draw_engine_document::SerialNumberType::SolidSquare,
        ] {
            let (mut engine, view) = setup(1.0, arrow_type);
            let mut style = engine.editor.serial_number_style(&engine.model);
            style.serial_number_type = number_type;
            engine
                .set_viewport_serial_number_style(view, style)
                .unwrap();
            pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
            let source = engine.model.paint_order()[0];
            for (x, y, modifiers) in [
                (200.0, 73.0, SHIFT),
                (-180.0, 101.0, Modifiers::default()),
                (
                    -210.0,
                    -95.0,
                    Modifiers {
                        ctrl: true,
                        ..SHIFT
                    },
                ),
                (130.0, -190.0, Modifiers::default()),
            ] {
                pointer(&mut engine, view, PointerEventType::Move, x, y, modifiers);
                assert_exterior_tail(&engine, &preview(&engine, view), source);
            }
            pointer(
                &mut engine,
                view,
                PointerEventType::Up,
                130.0,
                -190.0,
                Modifiers::default(),
            );
            assert_exterior_tail(&engine, &arrows(&engine)[0].1, source);
        }
    }
}

#[test]
fn sequence_arrow_tail_matches_manual_exterior_binding_gap() {
    for arrow_type in [ArrowType::Straight, ArrowType::Curve, ArrowType::Elbow] {
        let (mut engine, view) = setup(1.0, arrow_type);
        pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
        pointer(&mut engine, view, PointerEventType::Up, 200.0, 0.0, SHIFT);
        let source = engine.model.paint_order()[0];
        let automatic = arrows(&engine)[0].1.clone();
        let diameter = engine.model.serial_number(source).unwrap().diameter;
        engine
            .set_viewport_active_tool(view, ActiveTool::Arrow)
            .unwrap();
        // A normal Arrow-tool press just outside the badge uses manual orbit binding.
        pointer(
            &mut engine,
            view,
            PointerEventType::Down,
            diameter / 2.0 + 2.0,
            0.0,
            Modifiers::default(),
        );
        pointer(
            &mut engine,
            view,
            PointerEventType::Up,
            200.0,
            0.0,
            Modifiers::default(),
        );
        let manual = arrows(&engine).pop().unwrap().1;
        let binding = manual.start_binding.as_ref().unwrap();
        assert_eq!(binding.element_id, source);
        assert_eq!(binding.mode, BindMode::Orbit);
        assert_exterior_tail(&engine, &automatic, source);
        let delta_x = manual.start().x - automatic.start().x;
        let delta_y = manual.start().y - automatic.start().y;
        assert!(
            delta_x.hypot(delta_y) < 0.02,
            "manual {:?}, automatic {:?}",
            manual.start(),
            automatic.start()
        );
    }
}

#[test]
fn sequence_arrow_short_drags_never_use_inside_tail() {
    for arrow_type in [ArrowType::Straight, ArrowType::Curve, ArrowType::Elbow] {
        for distance in [22.0, 30.0] {
            let (mut engine, view) = setup(1.0, arrow_type);
            pointer(&mut engine, view, PointerEventType::Down, 0.0, 0.0, SHIFT);
            pointer(
                &mut engine,
                view,
                PointerEventType::Move,
                distance,
                0.0,
                SHIFT,
            );
            let source = engine.model.paint_order()[0];
            if let Some(ElementCreationPreview::Arrow(arrow)) = engine
                .editor
                .presentation_state(&engine.model, &engine.viewport_slot(view).unwrap().view)
                .creation_preview
            {
                assert_exterior_tail(&engine, &arrow, source);
            }
            pointer(
                &mut engine,
                view,
                PointerEventType::Up,
                distance,
                0.0,
                SHIFT,
            );
            for (_, arrow) in arrows(&engine) {
                assert_exterior_tail(&engine, &arrow, source);
            }
        }
    }
}
