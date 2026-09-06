//! Shared FFmpeg helpers used by both `recording` and `editing`.

use std::ptr;
use std::sync::OnceLock;

use ffmpeg_next as ffmpeg;

use crate::error::{RecordingExportError as ScreenRecorderError, Result};

/// Initialize FFmpeg exactly once. Thread-safe via `OnceLock`.
pub(crate) fn ensure_ffmpeg_initialized() -> Result<()> {
    static INIT: OnceLock<std::result::Result<(), String>> = OnceLock::new();
    INIT.get_or_init(|| ffmpeg::init().map_err(|err| err.to_string()))
        .clone()
        .map_err(|err| ScreenRecorderError::Encode(format!("failed to initialize ffmpeg: {err}")))
}

/// Check whether an FFmpeg error is EAGAIN.
pub(crate) fn is_eagain(err: &ffmpeg::Error) -> bool {
    matches!(
        err,
        ffmpeg::Error::Other { errno } if *errno == ffmpeg::error::EAGAIN
    )
}

/// Copy an RGBA buffer into an FFmpeg video frame, respecting stride.
pub(crate) fn copy_rgba_into_frame(frame: &mut ffmpeg::frame::Video, width: u32, rgba: &[u8]) {
    let stride = frame.stride(0);
    let row_bytes = width as usize * 4;
    let height = frame.height() as usize;
    let dst = frame.data_mut(0);

    let packed_len = row_bytes.saturating_mul(height);
    if stride == row_bytes && dst.len() >= packed_len && rgba.len() >= packed_len {
        dst[..packed_len].copy_from_slice(&rgba[..packed_len]);
        return;
    }

    for y in 0..height {
        let src_start = y * row_bytes;
        let dst_start = y * stride;
        // SAFETY:
        // - `src_start + row_bytes` and `dst_start + row_bytes` stay in-bounds by loop construction.
        // - Source and destination buffers are distinct (caller-owned RGBA and FFmpeg frame storage).
        unsafe {
            ptr::copy_nonoverlapping(
                rgba.as_ptr().add(src_start),
                dst.as_mut_ptr().add(dst_start),
                row_bytes,
            );
        }
    }
}
