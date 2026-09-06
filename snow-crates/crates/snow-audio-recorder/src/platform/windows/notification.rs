use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use windows::Win32::Media::Audio::{
    DEVICE_STATE, EDataFlow, ERole, IMMDeviceEnumerator, IMMNotificationClient,
    IMMNotificationClient_Impl, eCapture, eRender,
};
use windows::core::{PCWSTR, implement};

use crate::error::AudioResult;

use super::com::{EventHandle, platform_err};
#[derive(Default)]
pub(crate) struct NotificationState {
    render_default_changed: AtomicBool,
    capture_default_changed: AtomicBool,
    topology_changed: AtomicBool,
}

#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct NotificationChanges {
    pub render_default_changed: bool,
    pub capture_default_changed: bool,
    pub topology_changed: bool,
}

impl NotificationState {
    pub fn take_render_default_changed(&self) -> bool {
        self.render_default_changed.swap(false, Ordering::AcqRel)
    }

    pub fn take_capture_default_changed(&self) -> bool {
        self.capture_default_changed.swap(false, Ordering::AcqRel)
    }

    pub fn take_topology_changed(&self) -> bool {
        self.topology_changed.swap(false, Ordering::AcqRel)
    }

    pub fn take_changes(&self) -> NotificationChanges {
        NotificationChanges {
            render_default_changed: self.take_render_default_changed(),
            capture_default_changed: self.take_capture_default_changed(),
            topology_changed: self.take_topology_changed(),
        }
    }
}

pub(crate) struct NotificationClientGuard {
    enumerator: IMMDeviceEnumerator,
    client: IMMNotificationClient,
    state: Arc<NotificationState>,
}

impl NotificationClientGuard {
    pub fn register(
        enumerator: &IMMDeviceEnumerator,
        control_event: Arc<EventHandle>,
    ) -> AudioResult<Self> {
        let state = Arc::new(NotificationState::default());
        let client_impl = NotificationClient {
            state: Arc::clone(&state),
            control_event,
        };
        let client: IMMNotificationClient = client_impl.into();

        unsafe { enumerator.RegisterEndpointNotificationCallback(&client) }
            .map_err(platform_err)?;

        Ok(Self {
            enumerator: enumerator.clone(),
            client,
            state,
        })
    }

    pub fn state(&self) -> &Arc<NotificationState> {
        &self.state
    }
}

impl Drop for NotificationClientGuard {
    fn drop(&mut self) {
        unsafe {
            let _ = self
                .enumerator
                .UnregisterEndpointNotificationCallback(&self.client);
        }
    }
}

#[implement(windows::Win32::Media::Audio::IMMNotificationClient)]
struct NotificationClient {
    state: Arc<NotificationState>,
    control_event: Arc<EventHandle>,
}

impl NotificationClient {
    fn signal_control(&self) {
        let _ = self.control_event.set();
    }

    fn mark_topology_changed(&self) {
        self.state.topology_changed.store(true, Ordering::Release);
        self.signal_control();
    }

    fn mark_default_changed(&self, flow: EDataFlow) {
        if flow == eRender {
            self.state
                .render_default_changed
                .store(true, Ordering::Release);
        } else if flow == eCapture {
            self.state
                .capture_default_changed
                .store(true, Ordering::Release);
        } else {
            self.state
                .render_default_changed
                .store(true, Ordering::Release);
            self.state
                .capture_default_changed
                .store(true, Ordering::Release);
        }

        self.signal_control();
    }
}

#[allow(non_snake_case)]
impl IMMNotificationClient_Impl for NotificationClient_Impl {
    fn OnDeviceStateChanged(
        &self,
        _pwstrdeviceid: &PCWSTR,
        _dwnewstate: DEVICE_STATE,
    ) -> windows::core::Result<()> {
        self.mark_topology_changed();
        Ok(())
    }

    fn OnDeviceAdded(&self, _pwstrdeviceid: &PCWSTR) -> windows::core::Result<()> {
        self.mark_topology_changed();
        Ok(())
    }

    fn OnDeviceRemoved(&self, _pwstrdeviceid: &PCWSTR) -> windows::core::Result<()> {
        self.mark_topology_changed();
        Ok(())
    }

    fn OnDefaultDeviceChanged(
        &self,
        flow: EDataFlow,
        _role: ERole,
        _pwstrdefaultdeviceid: &PCWSTR,
    ) -> windows::core::Result<()> {
        self.mark_default_changed(flow);
        Ok(())
    }

    fn OnPropertyValueChanged(
        &self,
        _pwstrdeviceid: &PCWSTR,
        _key: &windows::Win32::Foundation::PROPERTYKEY,
    ) -> windows::core::Result<()> {
        self.mark_topology_changed();
        Ok(())
    }
}
