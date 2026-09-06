use std::sync::Arc;

use crate::backend::{AudioBackend, AudioBackendKind};
#[cfg(not(target_os = "windows"))]
use crate::device::AudioDeviceInfo;
#[cfg(not(target_os = "windows"))]
use crate::error::AudioError;
use crate::error::AudioResult;
#[cfg(not(target_os = "windows"))]
use crate::session::AudioStreamConfig;

#[cfg(target_os = "windows")]
pub(crate) mod windows;

#[cfg(not(target_os = "windows"))]
struct UnsupportedBackend;

#[cfg(not(target_os = "windows"))]
impl AudioBackend for UnsupportedBackend {
    fn enumerate_devices(
        &self,
        _flow: crate::device::DeviceFlow,
    ) -> AudioResult<Vec<AudioDeviceInfo>> {
        Err(AudioError::platform(anyhow::anyhow!(
            "audio capture is only supported on Windows"
        )))
    }

    fn create_engine(
        &self,
        _config: AudioStreamConfig,
    ) -> AudioResult<Box<dyn crate::backend::AudioRecorderEngine>> {
        Err(AudioError::platform(anyhow::anyhow!(
            "audio capture is only supported on Windows"
        )))
    }
}

#[cfg(target_os = "windows")]
pub(crate) fn build_backend(kind: AudioBackendKind) -> AudioResult<Arc<dyn AudioBackend>> {
    Ok(Arc::new(windows::WasapiBackend::new(kind)?))
}

#[cfg(not(target_os = "windows"))]
pub(crate) fn build_backend(_kind: AudioBackendKind) -> AudioResult<Arc<dyn AudioBackend>> {
    Ok(Arc::new(UnsupportedBackend))
}
