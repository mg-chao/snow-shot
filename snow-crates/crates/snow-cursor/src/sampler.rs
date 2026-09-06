use crate::{CursorCaptureError, platform};
use crate::{CursorShapeCapture, CursorSnapshot};

#[derive(Clone, Debug)]
pub(crate) struct CursorProbe {
    pub x: i32,
    pub y: i32,
    pub visible: bool,
    pub shape: CursorShapeCapture,
}

/// Synchronous, poll-based cursor sampler.
///
/// Each call to [`sample`](Self::sample) captures the current cursor state.
pub struct CursorSampler {
    inner: platform::CursorSamplerImpl,
}

impl CursorSampler {
    pub fn new() -> Result<Self, CursorCaptureError> {
        Ok(Self {
            inner: platform::CursorSamplerImpl::new()?,
        })
    }

    pub fn sample(&mut self) -> Result<CursorSnapshot, CursorCaptureError> {
        let probe = self.inner.sample_cursor()?;

        Ok(CursorSnapshot {
            absolute_x: probe.x,
            absolute_y: probe.y,
            visible: probe.visible,
            shape: probe.shape,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{CursorCompositionMode, CursorShape, CursorShapeCapture};

    #[test]
    fn raw_probe_maps_to_absolute_snapshot() {
        let shape = CursorShape::from_rgba(
            0,
            0,
            1,
            1,
            CursorCompositionMode::AlphaBlend,
            vec![255, 0, 0, 255],
        );
        let probe = CursorProbe {
            x: 10,
            y: 20,
            visible: true,
            shape: CursorShapeCapture::Captured(shape.clone()),
        };

        let snapshot = CursorSnapshot {
            absolute_x: probe.x,
            absolute_y: probe.y,
            visible: probe.visible,
            shape: probe.shape,
        };

        assert_eq!(snapshot.absolute_x, 10);
        assert_eq!(snapshot.absolute_y, 20);
        assert!(matches!(
            snapshot.shape,
            CursorShapeCapture::Captured(inner) if inner.shape_id == shape.shape_id
        ));
    }
}
