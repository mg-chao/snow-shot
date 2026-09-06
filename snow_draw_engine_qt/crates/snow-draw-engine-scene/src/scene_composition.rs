use std::collections::HashMap;

use super::*;

pub(crate) fn compose_scene_items(
    cache: &DocumentSceneCache,
    model: &DocumentModel,
    presentation: &EditorPresentationState,
    frame_view: FrameView,
) -> Vec<SceneDisplayItem> {
    let viewport = canvas_viewport(frame_view.camera, frame_view.surface);
    let mut ordered_ids = model.visible_element_ids(ViewportQuery {
        camera: frame_view.camera,
        surface: frame_view.surface,
    });
    let preview_rects: HashMap<_, _> = presentation
        .preview_elements
        .iter()
        .copied()
        .map(|preview| (preview.id, preview.rect))
        .collect();
    let preview_text_font_sizes: HashMap<_, _> = presentation
        .preview_text_font_sizes
        .iter()
        .map(|preview| (preview.id, preview.font_size))
        .collect();
    let active_existing_text = presentation
        .active_text_draft
        .as_ref()
        .and_then(|draft| draft.existing_id().map(|id| (id, draft.text.clone())));
    let preview_texts = preview_text_items(model, &preview_rects, active_existing_text.as_ref());
    let preview_serials = preview_serial_items(model, &preview_rects);
    let preview_arrows: HashMap<_, _> = presentation
        .preview_arrows
        .iter()
        .cloned()
        .map(|preview| (preview.id, preview.arrow))
        .collect();
    let mut emitted_preview_ids = HashMap::<ElementId, bool>::new();
    let mut items = Vec::new();

    for id in preview_rects
        .keys()
        .chain(preview_arrows.keys())
        .chain(active_existing_text.iter().map(|(id, _)| id))
    {
        if model.paint_rank(*id).is_some() && !ordered_ids.contains(id) {
            ordered_ids.push(*id);
        }
    }
    ordered_ids.sort_unstable_by_key(|id| model.paint_rank(*id).unwrap_or(u32::MAX));

    for id in &ordered_ids {
        if let Some((active_id, active_text)) = active_existing_text.as_ref()
            && id == active_id
        {
            emitted_preview_ids.insert(*id, true);
            if bounds_visible(text_bounds(active_text), viewport) {
                append_serial_connectors_for_text(
                    &mut items,
                    model,
                    *id,
                    &preview_texts,
                    &preview_serials,
                    viewport,
                );
                items.push(scene_item_from_text(*id, active_text.clone()));
            }
            continue;
        }
        if let Some(preview_rect) = preview_rects.get(id) {
            emitted_preview_ids.insert(*id, true);
            if let Some((item, bounds)) = scene_item_from_selection_preview(
                model,
                *id,
                *preview_rect,
                preview_text_font_sizes.get(id).copied(),
            ) && bounds_visible(bounds, viewport)
            {
                if matches!(item, SceneDisplayItem::Text(_)) {
                    append_serial_connectors_for_text(
                        &mut items,
                        model,
                        *id,
                        &preview_texts,
                        &preview_serials,
                        viewport,
                    );
                }
                items.push(item);
            }
            continue;
        }
        if let Some(preview_arrow) = preview_arrows.get(id) {
            emitted_preview_ids.insert(*id, true);
            if !arrow_is_degenerate(preview_arrow)
                && bounds_visible(arrow_bounds(preview_arrow), viewport)
            {
                items.push(scene_item_from_arrow(*id, preview_arrow.clone()));
            }
            continue;
        }

        let Some(bounds) = cache.bounds(*id) else {
            continue;
        };
        if !bounds_visible(bounds, viewport) {
            continue;
        }
        if let Some(item) = cache.entry(*id) {
            append_serial_connectors_for_text(
                &mut items,
                model,
                *id,
                &preview_texts,
                &preview_serials,
                viewport,
            );
            items.push(scene_item_with_serial_bound_text(
                item.clone(),
                model.bound_text_id_for_serial_number(*id),
            ));
        }
    }

    for preview in &presentation.preview_elements {
        if emitted_preview_ids.contains_key(&preview.id) {
            continue;
        }
        if let Some((item, bounds)) = scene_item_from_selection_preview(
            model,
            preview.id,
            preview.rect,
            preview_text_font_sizes.get(&preview.id).copied(),
        ) && bounds_visible(bounds, viewport)
        {
            if matches!(item, SceneDisplayItem::Text(_)) {
                append_serial_connectors_for_text(
                    &mut items,
                    model,
                    preview.id,
                    &preview_texts,
                    &preview_serials,
                    viewport,
                );
            }
            items.push(item);
        }
    }
    for preview in &presentation.preview_arrows {
        if emitted_preview_ids.contains_key(&preview.id) {
            continue;
        }
        if !arrow_is_degenerate(&preview.arrow)
            && bounds_visible(arrow_bounds(&preview.arrow), viewport)
        {
            items.push(scene_item_from_arrow(preview.id, preview.arrow.clone()));
        }
    }
    if let Some(preview) = presentation.creation_preview.as_ref() {
        let id = model.peek_next_element_id();
        match preview {
            ElementCreationPreview::Rectangle(rect)
                if !rect.is_spotlight() && bounds_visible(rect_bounds(*rect), viewport) =>
            {
                items.push(scene_item_from_rect(id, *rect));
            }
            ElementCreationPreview::Filter(filter)
                if bounds_visible(filter_bounds(filter), viewport) =>
            {
                items.push(scene_item_from_filter(id, *filter));
            }
            ElementCreationPreview::PenFilter(preview) => {
                if let Some((item, bounds)) = scene_item_from_pen_filter_preview(id, preview)
                    && bounds_visible(bounds, viewport)
                {
                    items.push(item);
                }
            }
            ElementCreationPreview::Arrow(arrow)
                if !arrow_is_degenerate(arrow) && bounds_visible(arrow_bounds(arrow), viewport) =>
            {
                items.push(scene_item_from_arrow(id, arrow.clone()));
            }
            ElementCreationPreview::FreeDraw(preview)
                if bounds_visible(free_draw_preview_bounds(preview), viewport) =>
            {
                items.push(scene_item_from_free_draw_preview(id, preview));
            }
            ElementCreationPreview::SerialNumber(serial)
                if bounds_visible(serial_number_bounds(serial), viewport) =>
            {
                items.push(scene_item_from_serial_number(id, serial.clone(), None));
            }
            _ => {}
        }
    }
    if let Some(active_draft) = presentation.active_text_draft.as_ref()
        && active_draft.existing_id().is_none()
        && bounds_visible(text_bounds(&active_draft.text), viewport)
    {
        items.push(scene_item_from_text(
            active_draft.display_id(),
            active_draft.text.clone(),
        ));
    }

    items
}

fn preview_text_items(
    model: &DocumentModel,
    preview_rects: &HashMap<ElementId, RectangleData>,
    active_existing_text: Option<&(ElementId, TextData)>,
) -> HashMap<ElementId, TextData> {
    let mut items: HashMap<ElementId, TextData> = preview_rects
        .iter()
        .filter_map(|(id, rect)| {
            let mut text = model.text(*id).ok()?.clone();
            text.center = rect.center;
            text.width = rect.width;
            text.height = rect.height;
            text.rotation = rect.rotation;
            text.corner_radii = rect.corner_radii;
            text.opacity = rect.opacity;
            Some((*id, text))
        })
        .collect();
    if let Some((id, text)) = active_existing_text {
        items.insert(*id, text.clone());
    }
    items
}

fn preview_serial_items(
    model: &DocumentModel,
    preview_rects: &HashMap<ElementId, RectangleData>,
) -> HashMap<ElementId, SerialNumberData> {
    preview_rects
        .iter()
        .filter_map(|(id, rect)| {
            let serial = model.serial_number(*id).ok()?;
            let mut preview = serial_number_with_selection_rect(serial, *rect);
            preview.opacity = rect.opacity;
            Some((*id, preview))
        })
        .collect()
}

fn append_serial_connectors_for_text(
    items: &mut Vec<SceneDisplayItem>,
    model: &DocumentModel,
    text_id: ElementId,
    preview_texts: &HashMap<ElementId, TextData>,
    preview_serials: &HashMap<ElementId, SerialNumberData>,
    viewport: (f64, f64, f64, f64),
) {
    let Some(text) = preview_texts
        .get(&text_id)
        .cloned()
        .or_else(|| model.text(text_id).ok().cloned())
    else {
        return;
    };

    for serial_id in model.serial_number_ids_with_text(text_id) {
        let Some(serial) = preview_serials
            .get(&serial_id)
            .cloned()
            .or_else(|| model.serial_number(serial_id).ok().cloned())
        else {
            continue;
        };
        let Some(connection) = resolve_serial_number_text_connection(&serial, &text) else {
            continue;
        };
        let bounds =
            serial_connector_bounds(&connection, resolve_serial_number_stroke_width(&serial));
        if bounds_visible(bounds, viewport) {
            items.push(scene_item_from_serial_connector(
                serial_id, &serial, connection,
            ));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use snow_draw_engine_core::{Camera, SurfaceSize};
    use snow_draw_engine_document::{ElementMeta, Transaction};
    use snow_draw_engine_editor::{ActiveTextDraftPresentation, ActiveTextDraftTarget};

    fn assert_close(left: f64, right: f64) {
        assert!(
            (left - right).abs() <= 1e-9,
            "expected {left} to be close to {right}"
        );
    }

    fn default_frame_view() -> FrameView {
        FrameView {
            surface: SurfaceSize {
                width: 1000,
                height: 1000,
            },
            camera: Camera {
                center: Point::new(0.0, 0.0),
                zoom: 1.0,
            },
            clear_color: ColorRgba8::default(),
        }
    }

    fn serial_item(items: &[SceneDisplayItem]) -> &SerialNumberDisplayItem {
        items
            .iter()
            .find_map(|item| match item {
                SceneDisplayItem::SerialNumber(serial) => Some(serial),
                _ => None,
            })
            .expect("scene should emit a serial item")
    }

    fn text_items(items: &[SceneDisplayItem]) -> Vec<&TextDisplayItem> {
        items
            .iter()
            .filter_map(|item| match item {
                SceneDisplayItem::Text(text) => Some(text),
                _ => None,
            })
            .collect()
    }

    fn serial_connector_item(items: &[SceneDisplayItem]) -> &SerialNumberConnectorDisplayItem {
        items
            .iter()
            .find_map(|item| match item {
                SceneDisplayItem::SerialNumberConnector(connector) => Some(connector),
                _ => None,
            })
            .expect("scene should emit a serial connector")
    }

    #[test]
    fn active_existing_text_draft_replaces_committed_text_item() {
        let text_id = ElementId {
            index: 0,
            generation: 1,
        };
        let committed = TextData {
            center: Point::new(0.0, 0.0),
            width: 40.0,
            height: 20.0,
            text: "committed".to_owned(),
            ..TextData::default()
        };
        let draft = TextData {
            center: Point::new(180.0, 30.0),
            width: 90.0,
            height: 36.0,
            text: "draft".to_owned(),
            ..committed.clone()
        };

        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("setup");
        transaction.insert_text(text_id, ElementMeta::default(), committed);
        model.apply_transaction(transaction).unwrap();

        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);
        let presentation = EditorPresentationState {
            active_text_draft: Some(ActiveTextDraftPresentation {
                target: ActiveTextDraftTarget::Existing(text_id),
                revision: 7,
                text: draft,
            }),
            ..EditorPresentationState::default()
        };

        let items = compose_scene_items(&cache, &model, &presentation, default_frame_view());
        let texts = text_items(&items);

        assert_eq!(texts.len(), 1);
        assert_eq!(
            texts[0].id,
            DisplayItemId {
                index: text_id.index,
                generation: text_id.generation,
            }
        );
        assert_eq!(texts[0].text, "draft");
        assert_close(texts[0].center_x, 180.0);
        assert_close(texts[0].center_y, 30.0);
        assert_close(texts[0].width, 90.0);
        assert_close(texts[0].height, 36.0);
    }

    #[test]
    fn active_existing_text_draft_connector_uses_draft_text_geometry() {
        let serial_id = ElementId {
            index: 0,
            generation: 1,
        };
        let text_id = ElementId {
            index: 1,
            generation: 1,
        };
        let serial = SerialNumberData {
            center: Point::new(0.0, 0.0),
            diameter: 24.0,
            stroke_width: 2.0,
            text_element_id: Some(text_id),
            ..SerialNumberData::default()
        };
        let committed = TextData {
            center: Point::new(90.0, 0.0),
            width: 40.0,
            height: 20.0,
            ..TextData::default()
        };
        let draft = TextData {
            center: Point::new(220.0, 0.0),
            width: 80.0,
            height: 30.0,
            ..committed.clone()
        };

        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("setup");
        transaction.insert_serial_number(serial_id, ElementMeta::default(), serial.clone());
        transaction.insert_text(text_id, ElementMeta::default(), committed);
        model.apply_transaction(transaction).unwrap();

        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);
        let presentation = EditorPresentationState {
            active_text_draft: Some(ActiveTextDraftPresentation {
                target: ActiveTextDraftTarget::Existing(text_id),
                revision: 3,
                text: draft.clone(),
            }),
            ..EditorPresentationState::default()
        };

        let items = compose_scene_items(&cache, &model, &presentation, default_frame_view());
        let connector = serial_connector_item(&items);
        let expected = resolve_serial_number_text_connection(&serial, &draft).unwrap();

        assert_close(connector.end_x, expected.end.x);
        assert_close(connector.end_y, expected.end.y);
    }

    #[test]
    fn active_new_text_draft_emits_synthetic_text_item() {
        let draft = TextData {
            center: Point::new(24.0, 32.0),
            width: 120.0,
            height: 48.0,
            text: "new draft".to_owned(),
            ..TextData::default()
        };
        let presentation = EditorPresentationState {
            active_text_draft: Some(ActiveTextDraftPresentation {
                target: ActiveTextDraftTarget::New,
                revision: 11,
                text: draft,
            }),
            ..EditorPresentationState::default()
        };
        let model = DocumentModel::new();
        let cache = DocumentSceneCache::new();

        let items = compose_scene_items(&cache, &model, &presentation, default_frame_view());
        let texts = text_items(&items);

        assert_eq!(texts.len(), 1);
        assert_eq!(
            texts[0].id,
            DisplayItemId {
                index: u32::MAX,
                generation: 11,
            }
        );
        assert_eq!(texts[0].text, "new draft");
        assert_close(texts[0].center_x, 24.0);
        assert_close(texts[0].center_y, 32.0);
    }

    #[test]
    fn connector_uses_selection_preview_text_position() {
        let serial_id = ElementId {
            index: 0,
            generation: 1,
        };
        let text_id = ElementId {
            index: 1,
            generation: 1,
        };
        let serial = SerialNumberData {
            center: Point::new(0.0, 0.0),
            diameter: 24.0,
            stroke_width: 2.0,
            text_element_id: Some(text_id),
            ..SerialNumberData::default()
        };
        let text = TextData {
            center: Point::new(90.0, 0.0),
            width: 40.0,
            height: 20.0,
            ..TextData::default()
        };

        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("setup");
        transaction.insert_serial_number(serial_id, ElementMeta::default(), serial.clone());
        transaction.insert_text(text_id, ElementMeta::default(), text.clone());
        model.apply_transaction(transaction).unwrap();

        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);

        let preview_rect = RectangleData {
            rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
            highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
            center: Point::new(180.0, 0.0),
            width: text.width,
            height: text.height,
            rotation: text.rotation,
            fill: text.fill,
            fill_style: text.fill_style,
            stroke: text.stroke,
            stroke_width: text.stroke_width,
            stroke_style: StrokeStyle::Solid,
            corner_radii: text.corner_radii,
            opacity: text.opacity,
        };
        let presentation = EditorPresentationState {
            preview_elements: vec![SelectionRectState {
                id: text_id,
                rect: preview_rect,
            }],
            ..EditorPresentationState::default()
        };
        let frame_view = default_frame_view();

        let items = compose_scene_items(&cache, &model, &presentation, frame_view);
        let connector = items
            .iter()
            .find_map(|item| match item {
                SceneDisplayItem::SerialNumberConnector(connector) => Some(connector),
                _ => None,
            })
            .expect("preview text should emit a serial connector");

        let mut preview_text = text;
        preview_text.center = preview_rect.center;
        preview_text.width = preview_rect.width;
        preview_text.height = preview_rect.height;
        let expected = resolve_serial_number_text_connection(&serial, &preview_text).unwrap();

        assert_close(connector.end_x, expected.end.x);
        assert_close(connector.end_y, expected.end.y);
    }

    #[test]
    fn serial_preview_uses_committed_resize_transform() {
        let serial_id = ElementId {
            index: 0,
            generation: 1,
        };
        let serial = SerialNumberData {
            center: Point::new(0.0, 0.0),
            diameter: 40.0,
            font_size: 16.0,
            ..SerialNumberData::default()
        };

        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("setup");
        transaction.insert_serial_number(serial_id, ElementMeta::default(), serial);
        model.apply_transaction(transaction).unwrap();

        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);

        let presentation = EditorPresentationState {
            preview_elements: vec![SelectionRectState {
                id: serial_id,
                rect: RectangleData {
                    rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
                    highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
                    center: Point::new(10.0, 20.0),
                    width: 80.0,
                    height: 100.0,
                    rotation: 0.25,
                    fill: ColorRgba8::default(),
                    fill_style: FillStyle::Solid,
                    stroke: ColorRgba8::default(),
                    stroke_width: 0.0,
                    stroke_style: StrokeStyle::Solid,
                    corner_radii: Default::default(),
                    opacity: 1.0,
                },
            }],
            ..EditorPresentationState::default()
        };
        let frame_view = default_frame_view();

        let items = compose_scene_items(&cache, &model, &presentation, frame_view);
        let preview = items
            .iter()
            .find_map(|item| match item {
                SceneDisplayItem::SerialNumber(serial) => Some(serial),
                _ => None,
            })
            .expect("serial resize preview should emit a serial item");

        assert_close(preview.center_x, 10.0);
        assert_close(preview.center_y, 20.0);
        assert_close(preview.diameter, 80.0);
        assert_close(preview.font_size, 32.0);
    }

    #[test]
    fn selection_previews_preserve_text_and_serial_opacity() {
        let text_id = ElementId {
            index: 0,
            generation: 1,
        };
        let serial_id = ElementId {
            index: 1,
            generation: 1,
        };
        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("setup");
        transaction.insert_text(
            text_id,
            ElementMeta::default(),
            TextData {
                center: Point::new(-40.0, 0.0),
                width: 60.0,
                height: 24.0,
                opacity: 0.8,
                ..TextData::default()
            },
        );
        transaction.insert_serial_number(
            serial_id,
            ElementMeta::default(),
            SerialNumberData {
                center: Point::new(40.0, 0.0),
                diameter: 24.0,
                opacity: 0.8,
                ..SerialNumberData::default()
            },
        );
        model.apply_transaction(transaction).unwrap();

        let mut text_preview = model.element_rect_proxy(text_id).unwrap();
        text_preview.opacity = 0.4;
        let mut serial_preview = model.element_rect_proxy(serial_id).unwrap();
        serial_preview.opacity = 0.4;
        let presentation = EditorPresentationState {
            preview_elements: vec![
                SelectionRectState {
                    id: text_id,
                    rect: text_preview,
                },
                SelectionRectState {
                    id: serial_id,
                    rect: serial_preview,
                },
            ],
            ..EditorPresentationState::default()
        };
        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);

        let items = compose_scene_items(&cache, &model, &presentation, default_frame_view());
        let text = text_items(&items)
            .into_iter()
            .find(|item| item.id.index == text_id.index && item.id.generation == text_id.generation)
            .expect("scene should emit the text preview");
        let serial = items
            .iter()
            .find_map(|item| match item {
                SceneDisplayItem::SerialNumber(item)
                    if item.id.index == serial_id.index
                        && item.id.generation == serial_id.generation =>
                {
                    Some(item)
                }
                _ => None,
            })
            .expect("scene should emit the serial-number preview");

        assert_close(text.opacity, 0.4);
        assert_close(serial.opacity, 0.4);
    }

    #[test]
    fn serial_item_omits_missing_bound_text_id() {
        let serial_id = ElementId {
            index: 0,
            generation: 1,
        };
        let missing_text_id = ElementId {
            index: 1,
            generation: 1,
        };
        let serial = SerialNumberData {
            center: Point::new(0.0, 0.0),
            diameter: 24.0,
            text_element_id: Some(missing_text_id),
            ..SerialNumberData::default()
        };

        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("setup");
        transaction.insert_serial_number(serial_id, ElementMeta::default(), serial);
        model.apply_transaction(transaction).unwrap();

        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);

        let items = compose_scene_items(
            &cache,
            &model,
            &EditorPresentationState::default(),
            default_frame_view(),
        );

        assert_eq!(serial_item(&items).bound_text_id, None);
    }

    #[test]
    fn serial_item_clears_cached_bound_text_id_after_text_removal() {
        let serial_id = ElementId {
            index: 0,
            generation: 1,
        };
        let text_id = ElementId {
            index: 1,
            generation: 1,
        };
        let serial = SerialNumberData {
            center: Point::new(0.0, 0.0),
            diameter: 24.0,
            text_element_id: Some(text_id),
            ..SerialNumberData::default()
        };
        let text = TextData {
            center: Point::new(90.0, 0.0),
            width: 40.0,
            height: 20.0,
            ..TextData::default()
        };

        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("setup");
        transaction.insert_serial_number(serial_id, ElementMeta::default(), serial);
        transaction.insert_text(text_id, ElementMeta::default(), text);
        model.apply_transaction(transaction).unwrap();

        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);
        let items = compose_scene_items(
            &cache,
            &model,
            &EditorPresentationState::default(),
            default_frame_view(),
        );
        assert_eq!(
            serial_item(&items).bound_text_id,
            Some(DisplayItemId {
                index: text_id.index,
                generation: text_id.generation,
            })
        );

        let mut transaction = Transaction::new("remove text only");
        transaction.remove_element(text_id);
        let result = model.apply_transaction(transaction).unwrap();
        cache.sync(&model, Some(&result.changes));

        let items = compose_scene_items(
            &cache,
            &model,
            &EditorPresentationState::default(),
            default_frame_view(),
        );

        assert_eq!(serial_item(&items).bound_text_id, None);
    }

    #[test]
    fn offscreen_committed_item_is_emitted_when_preview_moves_into_viewport() {
        let id = ElementId {
            index: 0,
            generation: 1,
        };
        let committed = RectangleData {
            rectangle_kind: snow_draw_engine_document::RectangleElementKind::Rectangle,
            highlight_shape: snow_draw_engine_document::HighlightShape::Rectangle,
            center: Point::new(2000.0, 0.0),
            width: 80.0,
            height: 60.0,
            rotation: 0.0,
            fill: ColorRgba8::default(),
            fill_style: FillStyle::Solid,
            stroke: ColorRgba8::default(),
            stroke_width: 0.0,
            stroke_style: StrokeStyle::Solid,
            corner_radii: CornerRadii::default(),
            opacity: 1.0,
        };
        let preview = RectangleData {
            center: Point::new(0.0, 0.0),
            ..committed
        };
        let mut model = DocumentModel::new();
        let mut transaction = Transaction::new("offscreen preview");
        transaction.insert_rectangle(id, ElementMeta::default(), committed);
        model.apply_transaction(transaction).unwrap();
        let mut cache = DocumentSceneCache::new();
        cache.sync(&model, None);

        let items = compose_scene_items(
            &cache,
            &model,
            &EditorPresentationState {
                preview_elements: vec![SelectionRectState { id, rect: preview }],
                ..EditorPresentationState::default()
            },
            default_frame_view(),
        );
        let rectangle = items.iter().find_map(|item| match item {
            SceneDisplayItem::Rectangle(item) => Some(item),
            _ => None,
        });
        assert_eq!(rectangle.map(|item| item.center_x), Some(0.0));
    }
}
