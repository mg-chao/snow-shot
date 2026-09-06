use anyhow::Context;
use windows::Win32::Foundation::{CloseHandle, HANDLE, RPC_E_CHANGED_MODE};
use windows::Win32::System::Com::{
    COINIT_MULTITHREADED, CoInitializeEx, CoTaskMemFree, CoUninitialize,
};
use windows::Win32::System::Threading::{CreateEventW, ResetEvent, SetEvent};

use crate::error::{AudioError, AudioResult};

/// Convert any `anyhow`-compatible error into `AudioError::Platform`.
///
/// Shared by all Windows platform modules to avoid duplicating this
/// trivial adapter in every file.
pub(crate) fn platform_err<E>(err: E) -> AudioError
where
    E: Into<anyhow::Error>,
{
    AudioError::platform(err)
}

pub(crate) struct CoInitGuard {
    should_uninit: bool,
}

impl CoInitGuard {
    pub fn init_multithreaded() -> AudioResult<Self> {
        let hr = unsafe { CoInitializeEx(None, COINIT_MULTITHREADED) };
        if hr == RPC_E_CHANGED_MODE {
            return Ok(Self {
                should_uninit: false,
            });
        }

        hr.ok()
            .context("failed to initialize COM with CoInitializeEx(COINIT_MULTITHREADED)")
            .map_err(platform_err)?;

        Ok(Self {
            should_uninit: true,
        })
    }
}

impl Drop for CoInitGuard {
    fn drop(&mut self) {
        if self.should_uninit {
            unsafe {
                CoUninitialize();
            }
        }
    }
}

#[derive(Debug)]
pub(crate) struct EventHandle {
    raw: HANDLE,
}

unsafe impl Send for EventHandle {}
unsafe impl Sync for EventHandle {}

impl EventHandle {
    pub fn new_manual_reset(initial_state: bool) -> AudioResult<Self> {
        let handle =
            unsafe { CreateEventW(None, true, initial_state, None) }.map_err(platform_err)?;
        Ok(Self { raw: handle })
    }

    pub fn new_auto_reset(initial_state: bool) -> AudioResult<Self> {
        let handle =
            unsafe { CreateEventW(None, false, initial_state, None) }.map_err(platform_err)?;
        Ok(Self { raw: handle })
    }

    pub fn raw(&self) -> HANDLE {
        self.raw
    }

    pub fn set(&self) -> AudioResult<()> {
        unsafe { SetEvent(self.raw) }.map_err(platform_err)
    }

    pub fn reset(&self) -> AudioResult<()> {
        unsafe { ResetEvent(self.raw) }.map_err(platform_err)
    }
}

impl Drop for EventHandle {
    fn drop(&mut self) {
        unsafe {
            let _ = CloseHandle(self.raw);
        }
    }
}

pub(crate) fn pwstr_to_string_and_free(value: windows::core::PWSTR) -> AudioResult<String> {
    if value.is_null() {
        return Err(AudioError::platform(anyhow::anyhow!(
            "received null PWSTR from COM"
        )));
    }

    let output = unsafe { value.to_string() }
        .map_err(platform_err)
        .map(|s| s.trim_end_matches('\0').to_string())?;

    unsafe {
        CoTaskMemFree(Some(value.0 as *const _));
    }

    Ok(output)
}
