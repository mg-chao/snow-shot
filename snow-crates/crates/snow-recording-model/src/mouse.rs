use std::fs::File;
use std::io::{BufReader, BufWriter};
use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::error::{RecordingModelError, Result};

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub enum MouseButton {
    Left,
    Right,
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub enum CursorShapeCompositionMode {
    AlphaBlend,
    MaskedColor,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CursorShapeRecord {
    pub shape_id: u64,
    pub hotspot_x: u32,
    pub hotspot_y: u32,
    pub width: u32,
    pub height: u32,
    pub mode: CursorShapeCompositionMode,
    pub shape_rgba: Vec<u8>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CursorFrameRecord {
    pub timestamp_ms: u64,
    pub x: i32,
    pub y: i32,
    pub visible: bool,
    pub shape_id: Option<u64>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ClickEventRecord {
    pub timestamp_ms: u64,
    pub x: i32,
    pub y: i32,
    pub button: MouseButton,
    pub down: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct MouseStore {
    pub cursor_shapes: Vec<CursorShapeRecord>,
    pub cursor_frames: Vec<CursorFrameRecord>,
    pub clicks: Vec<ClickEventRecord>,
}

impl MouseStore {
    pub fn new() -> Self {
        Self::default()
    }
}

pub fn write_mouse_records(path: &Path, store: &MouseStore) -> Result<()> {
    let file = File::create(path)?;
    bincode::serialize_into(BufWriter::with_capacity(128 * 1024, file), store)
        .map_err(|err| RecordingModelError::Io(std::io::Error::other(err)))
}

pub fn read_mouse_records(path: &Path) -> Result<MouseStore> {
    let file = File::open(path)?;
    bincode::deserialize_from(BufReader::with_capacity(128 * 1024, file))
        .map_err(|err| RecordingModelError::Decode(format!("failed to decode mouse store: {err}")))
}

pub fn decode_mouse_records(bytes: &[u8]) -> Result<MouseStore> {
    bincode::deserialize(bytes)
        .map_err(|err| RecordingModelError::Decode(format!("failed to decode mouse store: {err}")))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mouse_store_roundtrip() {
        let path = std::env::temp_dir().join(format!(
            "snow-recording-model-mouse-store-{}.bin",
            uuid::Uuid::new_v4().simple()
        ));

        let mut store = MouseStore::new();
        store.cursor_frames.push(CursorFrameRecord {
            timestamp_ms: 10,
            x: 20,
            y: 30,
            visible: true,
            shape_id: None,
        });

        write_mouse_records(&path, &store).expect("write should succeed");
        let decoded = read_mouse_records(&path).expect("read should succeed");
        assert_eq!(decoded.cursor_frames.len(), 1);

        let _ = std::fs::remove_file(path);
    }
}
