use windows::Win32::Devices::FunctionDiscovery::PKEY_Device_FriendlyName;
use windows::Win32::Media::Audio::{
    DEVICE_STATE, DEVICE_STATE_ACTIVE, DEVICE_STATE_DISABLED, DEVICE_STATE_NOTPRESENT,
    DEVICE_STATE_UNPLUGGED, EDataFlow, IMMDevice, IMMDeviceCollection, IMMDeviceEnumerator,
    MMDeviceEnumerator, eCapture, eConsole, eRender,
};
use windows::Win32::System::Com::STGM_READ;
use windows::Win32::System::Com::StructuredStorage::{
    PROPVARIANT, PropVariantClear, PropVariantToStringAlloc,
};
use windows::Win32::System::Com::{CLSCTX_ALL, CoCreateInstance};
use windows::core::HSTRING;

use crate::device::{AudioDeviceInfo, DeviceFlow, DeviceSelector};
use crate::error::{AudioError, AudioResult};

use super::com::{platform_err, pwstr_to_string_and_free};

pub(crate) fn create_device_enumerator() -> AudioResult<IMMDeviceEnumerator> {
    let enumerator =
        unsafe { CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL) }.map_err(platform_err)?;
    Ok(enumerator)
}

pub(crate) fn enumerate_devices(
    enumerator: &IMMDeviceEnumerator,
    flow: DeviceFlow,
) -> AudioResult<Vec<AudioDeviceInfo>> {
    let flow_native = flow_to_native(flow);
    let default_id = default_device_id(enumerator, flow_native).ok();

    let state_mask = DEVICE_STATE(
        DEVICE_STATE_ACTIVE.0
            | DEVICE_STATE_DISABLED.0
            | DEVICE_STATE_NOTPRESENT.0
            | DEVICE_STATE_UNPLUGGED.0,
    );

    let collection =
        unsafe { enumerator.EnumAudioEndpoints(flow_native, state_mask) }.map_err(platform_err)?;

    collect_device_infos(&collection, flow, default_id.as_deref())
}

pub(crate) fn resolve_device(
    enumerator: &IMMDeviceEnumerator,
    selector: &DeviceSelector,
    expected_flow: DeviceFlow,
) -> AudioResult<IMMDevice> {
    match selector {
        DeviceSelector::DefaultRender => {
            if expected_flow != DeviceFlow::Render {
                return Err(AudioError::InvalidConfig(
                    "DefaultRender selector used with capture flow".into(),
                ));
            }
            unsafe { enumerator.GetDefaultAudioEndpoint(eRender, eConsole) }.map_err(platform_err)
        }
        DeviceSelector::DefaultCapture => {
            if expected_flow != DeviceFlow::Capture {
                return Err(AudioError::InvalidConfig(
                    "DefaultCapture selector used with render flow".into(),
                ));
            }
            unsafe { enumerator.GetDefaultAudioEndpoint(eCapture, eConsole) }.map_err(platform_err)
        }
        DeviceSelector::Id(id) => {
            let id_h = HSTRING::from(id);
            unsafe { enumerator.GetDevice(&id_h) }.map_err(platform_err)
        }
    }
}

pub(crate) fn device_id(device: &IMMDevice) -> AudioResult<String> {
    let id = unsafe { device.GetId() }.map_err(platform_err)?;
    pwstr_to_string_and_free(id)
}

pub(crate) fn default_device_id(
    enumerator: &IMMDeviceEnumerator,
    flow: EDataFlow,
) -> AudioResult<String> {
    let device =
        unsafe { enumerator.GetDefaultAudioEndpoint(flow, eConsole) }.map_err(platform_err)?;
    device_id(&device)
}

fn flow_to_native(flow: DeviceFlow) -> EDataFlow {
    match flow {
        DeviceFlow::Render => eRender,
        DeviceFlow::Capture => eCapture,
    }
}

fn collect_device_infos(
    collection: &IMMDeviceCollection,
    flow: DeviceFlow,
    default_id: Option<&str>,
) -> AudioResult<Vec<AudioDeviceInfo>> {
    let count = unsafe { collection.GetCount() }.map_err(platform_err)?;
    let mut out = Vec::with_capacity(count as usize);

    for idx in 0..count {
        let device = unsafe { collection.Item(idx) }.map_err(platform_err)?;
        let id = device_id(&device)?;
        let name = friendly_name_or_id(&device, &id);
        let state = unsafe { device.GetState() }.map_err(platform_err)?;
        let is_active = (state.0 & DEVICE_STATE_ACTIVE.0) != 0;
        let is_default = default_id.is_some_and(|candidate| candidate == id.as_str());

        out.push(AudioDeviceInfo {
            id,
            name,
            is_default,
            is_active,
            flow,
        });
    }

    Ok(out)
}

fn friendly_name_or_id(device: &IMMDevice, fallback_id: &str) -> String {
    match friendly_name(device) {
        Ok(name) if !name.is_empty() => name,
        _ => fallback_id.to_string(),
    }
}

fn friendly_name(device: &IMMDevice) -> AudioResult<String> {
    let store = unsafe { device.OpenPropertyStore(STGM_READ) }.map_err(platform_err)?;

    let mut value: PROPVARIANT =
        unsafe { store.GetValue(&PKEY_Device_FriendlyName) }.map_err(platform_err)?;

    let result = (|| {
        let pwstr = unsafe { PropVariantToStringAlloc(&value) }.map_err(platform_err)?;
        pwstr_to_string_and_free(pwstr)
    })();

    unsafe {
        let _ = PropVariantClear(&mut value);
    }

    result
}
