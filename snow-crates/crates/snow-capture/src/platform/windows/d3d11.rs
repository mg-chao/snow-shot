use anyhow::{Context, Result};
use windows::Win32::Foundation::HMODULE;
use windows::Win32::Graphics::Direct3D::{
    D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_UNKNOWN, D3D_FEATURE_LEVEL_11_0,
};
use windows::Win32::Graphics::Direct3D11::{
    D3D11_CREATE_DEVICE_BGRA_SUPPORT, D3D11_CREATE_DEVICE_SINGLETHREADED, D3D11_SDK_VERSION,
    D3D11CreateDevice, ID3D11Device, ID3D11DeviceContext, ID3D11Resource, ID3D11Texture2D,
};
use windows::Win32::Graphics::Dxgi::IDXGIAdapter;
use windows::core::Interface;

use crate::error::{CaptureError, CaptureResult};

/// Create a D3D11 device on the given adapter.
///
/// When `single_threaded` is true the device is created with
/// `D3D11_CREATE_DEVICE_SINGLETHREADED`, which removes internal driver
/// locking overhead.  This is safe for backends that only access the
/// device from a single thread (e.g. DXGI duplication), but must NOT
/// be used for WGC whose `CreateFreeThreaded` frame pool accesses the
/// device from multiple threads.
pub(crate) fn create_d3d11_device_for_adapter(
    adapter: &IDXGIAdapter,
    single_threaded: bool,
) -> Result<(ID3D11Device, ID3D11DeviceContext)> {
    create_d3d11_device(Some(adapter), single_threaded)
}

/// Create a D3D11 device on the default hardware adapter.
pub(crate) fn create_d3d11_device_default(
    single_threaded: bool,
) -> Result<(ID3D11Device, ID3D11DeviceContext)> {
    create_d3d11_device(None, single_threaded)
}

fn create_d3d11_device(
    adapter: Option<&IDXGIAdapter>,
    single_threaded: bool,
) -> Result<(ID3D11Device, ID3D11DeviceContext)> {
    let mut device: Option<ID3D11Device> = None;
    let mut context: Option<ID3D11DeviceContext> = None;
    let feature_levels = [D3D_FEATURE_LEVEL_11_0];

    let mut flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if single_threaded {
        flags |= D3D11_CREATE_DEVICE_SINGLETHREADED;
    }

    unsafe {
        D3D11CreateDevice(
            adapter,
            if adapter.is_some() {
                D3D_DRIVER_TYPE_UNKNOWN
            } else {
                D3D_DRIVER_TYPE_HARDWARE
            },
            HMODULE::default(),
            flags,
            Some(&feature_levels),
            D3D11_SDK_VERSION,
            Some(&mut device),
            None,
            Some(&mut context),
        )
    }
    .context("D3D11CreateDevice failed")?;

    let device = device.context("D3D11CreateDevice did not return a device")?;
    let context = context.context("D3D11CreateDevice did not return a device context")?;
    Ok((device, context))
}

/// Cast an `ID3D11Texture2D` to its base `ID3D11Resource` interface and
/// pass it to `f`.  Tries a zero-cost `from_raw_borrowed` first, falling
/// back to a COM `QueryInterface` cast.
pub(crate) fn with_texture_resource<T>(
    texture: &ID3D11Texture2D,
    cast_context: &'static str,
    f: impl FnOnce(&ID3D11Resource) -> CaptureResult<T>,
) -> CaptureResult<T> {
    let raw = texture.as_raw();
    // SAFETY: ID3D11Texture2D inherits from ID3D11Resource, so the raw
    // COM pointer is valid when viewed through the base interface.
    if let Some(resource) = unsafe { ID3D11Resource::from_raw_borrowed(&raw) } {
        return f(resource);
    }

    let owned_resource: ID3D11Resource = texture
        .cast()
        .context(cast_context)
        .map_err(CaptureError::platform)?;
    f(&owned_resource)
}
