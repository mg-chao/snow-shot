use windows::Win32::Foundation::E_ACCESSDENIED;
use windows::Win32::Media::Audio::{
    AUDCLNT_E_DEVICE_INVALIDATED, AUDCLNT_E_ENDPOINT_CREATE_FAILED,
    AUDCLNT_E_RESOURCES_INVALIDATED, AUDCLNT_E_SERVICE_NOT_RUNNING, AUDCLNT_E_UNSUPPORTED_FORMAT,
};

use crate::error::AudioError;

pub(crate) fn map_hresult(hr: windows::core::HRESULT, context: &str) -> AudioError {
    match hr {
        _ if hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED => {
            AudioError::DeviceLost
        }
        _ if hr == E_ACCESSDENIED => AudioError::AccessDenied,
        _ if hr == AUDCLNT_E_UNSUPPORTED_FORMAT => {
            AudioError::UnsupportedFormat(context.to_string())
        }
        _ if hr == AUDCLNT_E_ENDPOINT_CREATE_FAILED => {
            AudioError::DeviceUnavailable(format!("{context}: endpoint creation failed"))
        }
        _ if hr == AUDCLNT_E_SERVICE_NOT_RUNNING => AudioError::DeviceUnavailable(format!(
            "{context}: Windows Audio service is not running"
        )),
        _ => AudioError::platform(anyhow::anyhow!("{context}: HRESULT 0x{:08x}", hr.0 as u32)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hresult_classification_matches_expected_variants() {
        assert!(matches!(
            map_hresult(AUDCLNT_E_DEVICE_INVALIDATED, "x"),
            AudioError::DeviceLost
        ));
        assert!(matches!(
            map_hresult(AUDCLNT_E_UNSUPPORTED_FORMAT, "x"),
            AudioError::UnsupportedFormat(_)
        ));
        assert!(matches!(
            map_hresult(E_ACCESSDENIED, "x"),
            AudioError::AccessDenied
        ));
    }
}
