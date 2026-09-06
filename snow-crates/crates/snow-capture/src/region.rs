//! Multi-monitor region capture support.
//!
//! [`MonitorLayout`] snapshots the virtual desktop geometry at startup.
//! [`CaptureRegion`] describes an arbitrary rectangle in virtual desktop
//! coordinates that may span multiple monitors.

use crate::error::{CaptureError, CaptureResult};
use crate::monitor::MonitorId;

/// A rectangle in virtual desktop coordinates.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CaptureRegion {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

impl CaptureRegion {
    pub fn new(x: i32, y: i32, width: u32, height: u32) -> CaptureResult<Self> {
        if width == 0 || height == 0 {
            return Err(CaptureError::InvalidConfig(
                "region width and height must be > 0".into(),
            ));
        }
        Ok(Self {
            x,
            y,
            width,
            height,
        })
    }

    /// Right edge (exclusive) in virtual desktop coordinates.
    pub fn right(&self) -> i32 {
        self.x.saturating_add(self.width as i32)
    }

    /// Bottom edge (exclusive) in virtual desktop coordinates.
    pub fn bottom(&self) -> i32 {
        self.y.saturating_add(self.height as i32)
    }
}

/// Geometry of a single monitor in the virtual desktop.
#[derive(Clone, Debug)]
pub struct MonitorGeometry {
    pub monitor: MonitorId,
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

/// A snapshot of the virtual desktop monitor layout, captured once at
/// session creation. Used to determine which monitors overlap a
/// [`CaptureRegion`] and how to composite the final frame.
#[derive(Clone, Debug)]
pub struct MonitorLayout {
    pub monitors: Vec<MonitorGeometry>,
    /// Bounding box of the entire virtual desktop.
    pub virtual_left: i32,
    pub virtual_top: i32,
    pub virtual_width: u32,
    pub virtual_height: u32,
}

impl MonitorLayout {
    /// Snapshot the current monitor layout from the OS.
    ///
    /// This queries monitor positions once and caches them. Call this
    pub fn snapshot() -> CaptureResult<Self> {
        let monitors = crate::monitor::enumerate_monitors()?;
        Self::snapshot_from_monitors(monitors)
    }

    /// Snapshot monitor geometry for a known monitor set.
    ///
    /// `monitors` should come from the same backend/session that will be
    /// used for capture so monitor keys remain consistent.
    pub fn snapshot_from_monitors(monitors: Vec<MonitorId>) -> CaptureResult<Self> {
        crate::platform::monitor_layout_from_monitors(monitors)
    }

    /// Return the monitors that overlap the given region, along with
    /// the intersection rectangle in virtual desktop coordinates.
    pub fn overlapping_monitors(
        &self,
        region: &CaptureRegion,
    ) -> Vec<(MonitorGeometry, CaptureRegion)> {
        let mut result = Vec::new();
        for mon in &self.monitors {
            if let Some(intersection) = monitor_intersection(region, mon) {
                result.push((mon.clone(), intersection));
            }
        }
        result
    }
}

fn monitor_intersection(
    region: &CaptureRegion,
    monitor: &MonitorGeometry,
) -> Option<CaptureRegion> {
    let ix = region.x.max(monitor.x);
    let iy = region.y.max(monitor.y);
    let ix2 = region
        .right()
        .min(monitor.x.saturating_add(monitor.width as i32));
    let iy2 = region
        .bottom()
        .min(monitor.y.saturating_add(monitor.height as i32));

    if ix < ix2 && iy < iy2 {
        Some(CaptureRegion {
            x: ix,
            y: iy,
            width: (ix2 - ix) as u32,
            height: (iy2 - iy) as u32,
        })
    } else {
        None
    }
}
