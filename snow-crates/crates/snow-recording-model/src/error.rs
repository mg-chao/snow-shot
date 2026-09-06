use std::io;

use thiserror::Error;

#[derive(Debug, Error)]
pub enum RecordingModelError {
    #[error("io error: {0}")]
    Io(#[from] io::Error),

    #[error("decode error: {0}")]
    Decode(String),
}

pub type Result<T> = std::result::Result<T, RecordingModelError>;
