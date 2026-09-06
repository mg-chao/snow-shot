use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct StoredFrame {
    pub timestamp_ms: u64,
    pub duration_ms: u32,
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}
