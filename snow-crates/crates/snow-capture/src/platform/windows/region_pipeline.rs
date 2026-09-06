use anyhow::Context;
use windows::Win32::Graphics::Direct3D11::{
    D3D11_BOX, D3D11_QUERY_DESC, D3D11_QUERY_EVENT, D3D11_TEXTURE2D_DESC, ID3D11Device,
    ID3D11DeviceContext, ID3D11Query, ID3D11Resource, ID3D11Texture2D,
};

use windows::core::Interface;

use crate::backend::CaptureBlitRegion;
use crate::error::{CaptureError, CaptureResult};
use crate::frame::DirtyRect;

use super::surface::{self, StagingSampleDesc};

pub(crate) const REGION_SPIN_INITIAL_POLLS: u32 = 4;
pub(crate) const REGION_SPIN_MIN_POLLS: u32 = 2;
pub(crate) const REGION_SPIN_MAX_POLLS: u32 = 64;
pub(crate) const REGION_SPIN_INCREASE_STEP: u32 = 4;

/// Trait abstracting the slot reset behavior that differs between backends.
///
/// DXGI's `reset_region_pipeline` calls `reset_runtime_state` (preserves GPU
/// textures), while WGC calls `invalidate` (drops them).  Both backends call
/// `invalidate` for the stronger teardown path.
pub(crate) trait RegionSlot: Default {
    fn soft_reset(&mut self);
    fn hard_reset(&mut self);
}

/// Provides access to the common staging-slot fields that the shared region
/// pipeline helpers need.  Each backend implements this for its own slot type.
pub(crate) trait RegionStagingSlotAccess {
    fn staging_resource(&self) -> Option<&ID3D11Resource>;
    fn set_staging(&mut self, tex: ID3D11Texture2D, res: ID3D11Resource);
    fn query(&self) -> Option<&ID3D11Query>;
    fn set_query(&mut self, q: ID3D11Query);
    fn dirty_rects(&self) -> &[DirtyRect];
    fn dirty_gpu_copy_preferred(&self) -> bool;
}

/// Shared region-pipeline bookkeeping embedded by both GPU backends.
///
/// Owns the staging-slot ring, pending/write indices, adaptive spin count,
/// and the cached blit region.  The actual slot type and ring size are
/// parameterised so each backend keeps its own concrete types.
pub(crate) struct RegionPipelineState<S: RegionSlot, const N: usize> {
    pub slots: [S; N],
    pub pending_slot: Option<usize>,
    pub next_write_slot: usize,
    pub adaptive_spin_polls: u32,
    pub blit: Option<CaptureBlitRegion>,
}

impl<S: RegionSlot, const N: usize> RegionPipelineState<S, N> {
    pub fn new() -> Self {
        Self {
            slots: std::array::from_fn(|_| S::default()),
            pending_slot: None,
            next_write_slot: 0,
            adaptive_spin_polls: REGION_SPIN_INITIAL_POLLS,
            blit: None,
        }
    }

    fn clear_pipeline_state(&mut self) {
        self.pending_slot = None;
        self.next_write_slot = 0;
        self.adaptive_spin_polls = REGION_SPIN_INITIAL_POLLS;
        self.blit = None;
    }

    /// Soft-reset the pipeline: clear bookkeeping and soft-reset each slot.
    pub fn reset(&mut self) {
        self.clear_pipeline_state();
        for slot in &mut self.slots {
            slot.soft_reset();
        }
    }

    /// Hard-reset the pipeline: clear bookkeeping and fully invalidate each slot.
    pub fn invalidate(&mut self) {
        self.clear_pipeline_state();
        for slot in &mut self.slots {
            slot.hard_reset();
        }
    }

    /// Ensure the pipeline is configured for `blit`.  If the blit region
    /// changed, soft-resets the pipeline and stores the new region.
    pub fn ensure_blit(&mut self, blit: CaptureBlitRegion) {
        if self.blit == Some(blit) {
            return;
        }
        self.reset();
        self.blit = Some(blit);
    }
}
/// Check whether a D3D11 event query has been signalled.
///
/// The DXGI backend intentionally ignores the `data` value (for
/// `D3D11_QUERY_EVENT`, `GetData` returning `S_OK` already guarantees
/// completion).  The WGC backend additionally checks `data != 0` as a
/// belt-and-suspenders guard.  The `strict` parameter controls this:
/// pass `false` for DXGI semantics, `true` for WGC semantics.
#[inline]
pub(crate) fn query_signaled(
    context: &ID3D11DeviceContext,
    query: &ID3D11Query,
    flags: u32,
    strict: bool,
) -> bool {
    let mut data = 0u32;
    let status = unsafe {
        context.GetData(
            query,
            Some(&mut data as *mut u32 as *mut _),
            std::mem::size_of::<u32>() as u32,
            flags,
        )
    };
    if strict {
        status.is_ok() && data != 0
    } else {
        status.is_ok()
    }
}

/// Check whether the GPU copy for a region slot has completed.
#[inline]
pub(crate) fn region_slot_query_completed<S: RegionStagingSlotAccess>(
    context: &ID3D11DeviceContext,
    slot: &S,
    strict_query: bool,
) -> bool {
    const DO_NOT_FLUSH: u32 = 0x1;
    let Some(q) = slot.query() else {
        return false;
    };
    query_signaled(context, q, DO_NOT_FLUSH, strict_query)
}

/// Conditionally flush the GPU command queue after submitting a region copy.
///
/// Flushes when the write and read slots are the same (no pipelining) or
/// when the read slot's query hasn't completed yet (GPU is behind).
#[inline]
pub(crate) fn maybe_flush_region_after_submit<S: RegionStagingSlotAccess>(
    context: &ID3D11DeviceContext,
    write_slot: usize,
    read_slot: usize,
    read_slot_ref: &S,
    strict_query: bool,
) {
    if write_slot == read_slot || !region_slot_query_completed(context, read_slot_ref, strict_query)
    {
        unsafe {
            context.Flush();
        }
    }
}

/// Spin-wait for a region slot's GPU copy to complete, with adaptive polling.
///
/// Returns the updated adaptive spin poll count.
pub(crate) fn wait_for_region_slot_copy<S: RegionStagingSlotAccess>(
    context: &ID3D11DeviceContext,
    slot: &S,
    adaptive_spin_polls: u32,
    strict_query: bool,
) -> u32 {
    const DO_NOT_FLUSH: u32 = 0x1;
    let Some(q) = slot.query() else {
        return adaptive_spin_polls;
    };

    let mut completed_in_spin = false;
    for _ in 0..adaptive_spin_polls {
        if query_signaled(context, q, DO_NOT_FLUSH, strict_query) {
            completed_in_spin = true;
            break;
        }
        std::hint::spin_loop();
    }

    if completed_in_spin {
        adaptive_spin_polls
            .saturating_sub(1)
            .max(REGION_SPIN_MIN_POLLS)
    } else {
        adaptive_spin_polls
            .saturating_add(REGION_SPIN_INCREASE_STEP)
            .min(REGION_SPIN_MAX_POLLS)
    }
}

/// Copy a source texture region into a staging slot, optionally using
/// dirty-rect GPU copies when the previous frame is still resident.
///
/// After the copy, ends the slot's event query so callers can poll for
/// completion.  Callers are responsible for setting `populated = true`
/// on the slot before or after this call.
pub(crate) fn copy_region_source_to_slot<S: RegionStagingSlotAccess>(
    context: &ID3D11DeviceContext,
    slot: &S,
    source_resource: &ID3D11Resource,
    blit: CaptureBlitRegion,
    can_use_dirty_gpu_copy: bool,
) -> CaptureResult<()> {
    let staging_resource = slot.staging_resource().ok_or_else(|| {
        CaptureError::platform(anyhow::anyhow!(
            "region staging slot missing staging resource"
        ))
    })?;

    let mut used_dirty_copy = false;
    if can_use_dirty_gpu_copy && slot.dirty_gpu_copy_preferred() {
        for rect in slot.dirty_rects() {
            let source_left = blit
                .src_x
                .checked_add(rect.x)
                .ok_or(CaptureError::BufferOverflow)?;
            let source_top = blit
                .src_y
                .checked_add(rect.y)
                .ok_or(CaptureError::BufferOverflow)?;
            let source_right = source_left
                .checked_add(rect.width)
                .ok_or(CaptureError::BufferOverflow)?;
            let source_bottom = source_top
                .checked_add(rect.height)
                .ok_or(CaptureError::BufferOverflow)?;
            let source_box = D3D11_BOX {
                left: source_left,
                top: source_top,
                front: 0,
                right: source_right,
                bottom: source_bottom,
                back: 1,
            };
            unsafe {
                context.CopySubresourceRegion(
                    staging_resource,
                    0,
                    rect.x,
                    rect.y,
                    0,
                    source_resource,
                    0,
                    Some(&source_box),
                );
            }
        }
        used_dirty_copy = true;
    }

    if !used_dirty_copy {
        let src_right = blit
            .src_x
            .checked_add(blit.width)
            .ok_or(CaptureError::BufferOverflow)?;
        let src_bottom = blit
            .src_y
            .checked_add(blit.height)
            .ok_or(CaptureError::BufferOverflow)?;
        let source_box = D3D11_BOX {
            left: blit.src_x,
            top: blit.src_y,
            front: 0,
            right: src_right,
            bottom: src_bottom,
            back: 1,
        };

        unsafe {
            context.CopySubresourceRegion(
                staging_resource,
                0,
                0,
                0,
                0,
                source_resource,
                0,
                Some(&source_box),
            );
        }
    }

    if let Some(q) = slot.query() {
        unsafe {
            context.End(q);
        }
    }

    Ok(())
}

/// Ensure a region staging slot has a valid query object.
///
/// Creates a `D3D11_QUERY_EVENT` query if the slot doesn't already have one.
pub(crate) fn ensure_region_slot_query<S: RegionStagingSlotAccess>(
    device: &ID3D11Device,
    slot: &mut S,
) -> CaptureResult<()> {
    if slot.query().is_some() {
        return Ok(());
    }
    let query_desc = D3D11_QUERY_DESC {
        Query: D3D11_QUERY_EVENT,
        ..Default::default()
    };
    let mut query: Option<ID3D11Query> = None;
    unsafe { device.CreateQuery(&query_desc, Some(&mut query)) }
        .context("CreateQuery for region staging slot failed")
        .map_err(CaptureError::platform)?;
    if let Some(q) = query {
        slot.set_query(q);
    }
    Ok(())
}

/// Ensure a region staging slot has a staging texture matching `desc`.
///
/// If the slot's current texture doesn't match, creates a new one.
/// Also ensures the query object exists.  Returns `Ok(())` on success.
pub(crate) fn ensure_region_slot_texture<S: RegionStagingSlotAccess>(
    device: &ID3D11Device,
    slot: &mut S,
    desc: &D3D11_TEXTURE2D_DESC,
    needs_recreate: bool,
    context_msg: &'static str,
) -> CaptureResult<()> {
    if needs_recreate {
        let mut staging_opt = None;
        let staging = surface::ensure_staging_texture(
            device,
            &mut staging_opt,
            desc,
            StagingSampleDesc::SingleSample,
            context_msg,
        )?;
        let resource: ID3D11Resource = staging
            .cast::<ID3D11Resource>()
            .context("failed to cast region staging texture to ID3D11Resource")
            .map_err(CaptureError::platform)?;
        slot.set_staging(staging.clone(), resource);
    }

    ensure_region_slot_query(device, slot)?;
    Ok(())
}
