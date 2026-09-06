use std::collections::HashSet;

use snow_cursor::{AttachedCursorSample, CursorCompositionMode, CursorShape, CursorShapeId};
use snow_recording_model::{
    CursorFrameRecord, CursorShapeCompositionMode, CursorShapeRecord, MouseStore,
};

fn record_from_shape(shape: &CursorShape) -> CursorShapeRecord {
    CursorShapeRecord {
        shape_id: shape.shape_id.get(),
        hotspot_x: shape.hotspot_x,
        hotspot_y: shape.hotspot_y,
        width: shape.width,
        height: shape.height,
        mode: match shape.composition_mode {
            CursorCompositionMode::AlphaBlend => CursorShapeCompositionMode::AlphaBlend,
            CursorCompositionMode::MaskedColor => CursorShapeCompositionMode::MaskedColor,
        },
        shape_rgba: shape.shape_rgba.to_vec(),
    }
}

fn frame_from_sample(cursor: &AttachedCursorSample, timestamp_ms: u64) -> CursorFrameRecord {
    CursorFrameRecord {
        timestamp_ms,
        x: cursor.x,
        y: cursor.y,
        visible: cursor.visible,
        shape_id: cursor.shape_id().map(CursorShapeId::get),
    }
}

pub(crate) struct CursorProcessor {
    mouse_store: MouseStore,
    shapes_emitted: HashSet<u64>,
    last_frame: Option<CursorFrameRecord>,
}

impl CursorProcessor {
    pub(crate) fn new() -> Self {
        Self {
            mouse_store: MouseStore::new(),
            shapes_emitted: HashSet::new(),
            last_frame: None,
        }
    }

    pub(crate) fn record_frame(&mut self, timestamp_ms: u64, cursor: &AttachedCursorSample) {
        if let Some(shape) = cursor.shape.embedded_shape()
            && self.shapes_emitted.insert(shape.shape_id.get())
        {
            self.mouse_store
                .cursor_shapes
                .push(record_from_shape(shape));
        }

        let should_store = self.last_frame.as_ref().is_none_or(|last| {
            last.x != cursor.x
                || last.y != cursor.y
                || last.visible != cursor.visible
                || last.shape_id != cursor.shape_id().map(CursorShapeId::get)
        });
        let frame = frame_from_sample(cursor, timestamp_ms);
        if should_store {
            self.mouse_store.cursor_frames.push(frame.clone());
        }
        self.last_frame = Some(frame);
    }

    pub(crate) fn synthesize_frame_for_drop(&mut self, timestamp_ms: u64) {
        if let Some(last) = self.last_frame.as_mut() {
            last.timestamp_ms = timestamp_ms;
        }
    }

    #[cfg(test)]
    pub(crate) fn mouse_store(&self) -> &MouseStore {
        &self.mouse_store
    }

    #[cfg(test)]
    pub(crate) fn last_frame(&self) -> Option<&CursorFrameRecord> {
        self.last_frame.as_ref()
    }

    pub(crate) fn into_mouse_store(self) -> MouseStore {
        self.mouse_store
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;
    use snow_cursor::CursorShapeState;
    use std::collections::HashSet;

    fn sample_with_shape(shape_id: u64) -> AttachedCursorSample {
        AttachedCursorSample {
            x: 100,
            y: 200,
            visible: true,
            shape: CursorShapeState::Embedded(CursorShape {
                shape_id: CursorShapeId::from_raw(shape_id),
                hotspot_x: 0,
                hotspot_y: 0,
                width: 1,
                height: 1,
                composition_mode: CursorCompositionMode::AlphaBlend,
                shape_rgba: vec![shape_id as u8; 4].into(),
            }),
        }
    }

    fn sample_without_shape(shape_id: u64) -> AttachedCursorSample {
        AttachedCursorSample {
            x: 50,
            y: 60,
            visible: true,
            shape: CursorShapeState::Cached(CursorShapeId::from_raw(shape_id)),
        }
    }

    fn arb_cursor_sequence() -> impl Strategy<Value = Vec<(u64, bool)>> {
        prop::collection::vec((0u64..32, any::<bool>()), 1..64)
    }

    proptest! {
        #[test]
        fn prop_record_frame_preserves_embedded_coordinates(
            x in -1_000_000i32..1_000_000,
            y in -1_000_000i32..1_000_000,
            visible in any::<bool>(),
            shape_id in prop::option::of(any::<u64>()),
        ) {
            let mut proc = CursorProcessor::new();
            let sample = AttachedCursorSample {
                x,
                y,
                visible,
                shape: shape_id
                    .map(CursorShapeId::from_raw)
                    .map(CursorShapeState::Cached)
                    .unwrap_or(CursorShapeState::Unavailable),
            };

            let frames_before = proc.mouse_store().cursor_frames.len();
            proc.record_frame(42, &sample);

            prop_assert_eq!(proc.mouse_store().cursor_frames.len(), frames_before + 1);

            let record = proc.mouse_store().cursor_frames.last().unwrap();
            prop_assert_eq!(record.x, x);
            prop_assert_eq!(record.y, y);

            let last = proc.last_frame().unwrap();
            prop_assert_eq!(last.timestamp_ms, record.timestamp_ms);
            prop_assert_eq!(last.x, record.x);
            prop_assert_eq!(last.y, record.y);
            prop_assert_eq!(last.visible, record.visible);
            prop_assert_eq!(last.shape_id, record.shape_id);
        }
    }

    proptest! {
        #[test]
        fn prop_cursor_shape_deduplication(seq in arb_cursor_sequence()) {
            let mut proc = CursorProcessor::new();
            let mut unique_ids: HashSet<u64> = HashSet::new();

            for (i, (shape_id, first_occurrence)) in seq.iter().enumerate() {
                let sample = if *first_occurrence && !unique_ids.contains(shape_id) {
                    sample_with_shape(*shape_id)
                } else {
                    sample_without_shape(*shape_id)
                };

                if matches!(sample.shape, CursorShapeState::Embedded(_)) {
                    unique_ids.insert(*shape_id);
                }

                proc.record_frame(i as u64, &sample);

                prop_assert_eq!(
                    proc.mouse_store().cursor_shapes.len(),
                    unique_ids.len(),
                    "shapes stored ({}) != unique ids seen ({}) after sample {}",
                    proc.mouse_store().cursor_shapes.len(),
                    unique_ids.len(),
                    i,
                );
            }

            let stored_ids: HashSet<u64> = proc
                .mouse_store()
                .cursor_shapes
                .iter()
                .map(|shape| shape.shape_id)
                .collect();

            for frame in &proc.mouse_store().cursor_frames {
                if let Some(shape_id) = frame.shape_id
                    && unique_ids.contains(&shape_id)
                {
                    prop_assert!(
                        stored_ids.contains(&shape_id),
                        "frame references shape_id {} which is missing from cursor_shapes",
                        shape_id,
                    );
                }
            }
        }
    }

    #[test]
    fn record_frame_appends_exactly_one_frame() {
        let mut proc = CursorProcessor::new();
        let sample = sample_without_shape(1);
        proc.record_frame(100, &sample);
        assert_eq!(proc.mouse_store().cursor_frames.len(), 1);
        assert_eq!(proc.mouse_store().cursor_frames[0].timestamp_ms, 100);
    }

    #[test]
    fn new_shape_is_stored_on_first_occurrence() {
        let mut proc = CursorProcessor::new();
        let sample = sample_with_shape(42);
        proc.record_frame(0, &sample);
        assert_eq!(proc.mouse_store().cursor_shapes.len(), 1);
        assert_eq!(proc.mouse_store().cursor_shapes[0].shape_id, 42);
    }

    #[test]
    fn duplicate_shape_id_is_not_stored_twice() {
        let mut proc = CursorProcessor::new();
        proc.record_frame(0, &sample_with_shape(1));
        proc.record_frame(1, &sample_with_shape(1));
        assert_eq!(proc.mouse_store().cursor_shapes.len(), 1);
        assert_eq!(proc.mouse_store().cursor_frames.len(), 1);
    }

    #[test]
    fn synthesize_frame_for_drop_updates_last_frame_without_appending() {
        let mut proc = CursorProcessor::new();
        let sample = AttachedCursorSample {
            x: 50,
            y: 60,
            visible: true,
            shape: CursorShapeState::Cached(CursorShapeId::from_raw(1)),
        };
        proc.record_frame(100, &sample);
        proc.synthesize_frame_for_drop(200);

        assert_eq!(proc.mouse_store().cursor_frames.len(), 1);
        let synth = proc.last_frame().unwrap();
        assert_eq!(synth.timestamp_ms, 200);
        assert_eq!(synth.x, 50);
        assert_eq!(synth.y, 60);
    }

    #[test]
    fn synthesize_frame_for_drop_noop_when_no_last_frame() {
        let mut proc = CursorProcessor::new();
        proc.synthesize_frame_for_drop(100);
        assert_eq!(proc.mouse_store().cursor_frames.len(), 0);
    }

    #[test]
    fn into_mouse_store_returns_accumulated_data() {
        let mut proc = CursorProcessor::new();
        proc.record_frame(0, &sample_with_shape(1));
        proc.record_frame(1, &sample_with_shape(2));
        proc.record_frame(2, &sample_without_shape(1));

        let store = proc.into_mouse_store();
        assert_eq!(store.cursor_shapes.len(), 2);
        assert_eq!(store.cursor_frames.len(), 3);
    }
}
