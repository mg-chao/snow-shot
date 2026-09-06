use std::path::Path;
use std::time::Instant;

use anyhow::{Context, Result};
use snow_capture::{
    CaptureRegion, CaptureSystem, CaptureTarget, backend::CaptureBackendKind, frame::Frame,
};

fn save_frame_png(frame: &Frame, path: &Path) -> anyhow::Result<()> {
    image::save_buffer(
        path,
        frame.as_rgba_bytes(),
        frame.width(),
        frame.height(),
        image::ColorType::Rgba8,
    )
    .map_err(|e| anyhow::anyhow!("failed to write PNG to {}: {e}", path.display()))
}

fn capture_region_to_png(
    kind: CaptureBackendKind,
    label: &str,
    region: &CaptureRegion,
    output_path: &str,
) -> Result<()> {
    let target = CaptureTarget::Region(*region);

    let begin = Instant::now();
    let system = CaptureSystem::builder()
        .with_backend_kind(kind)
        .build()
        .with_context(|| format!("failed to initialize {label} capture system"))?;
    let mut session = system
        .open_session(target, Default::default())
        .with_context(|| format!("failed to initialize {label} capture session"))?;
    let init_ms = begin.elapsed().as_secs_f64() * 1000.0;
    println!("Initialized {label} capture session in {init_ms:.3} ms");

    let begin = Instant::now();
    let frame = session
        .capture()
        .with_context(|| format!("failed to capture region using {label}"))?;
    let cap_ms = begin.elapsed().as_secs_f64() * 1000.0;

    println!(
        "Captured {label} region frame: {}x{} in {cap_ms:.3} ms",
        frame.width(),
        frame.height(),
    );

    save_frame_png(&frame, Path::new(output_path))?;
    println!("Saved {label} region capture to {output_path}\n");
    Ok(())
}

fn main() -> Result<()> {
    let region =
        CaptureRegion::new(-500, 0, 1000, 1000).context("failed to create capture region")?;

    println!(
        "Capturing region: ({}, {}) to ({}, {})\n",
        region.x,
        region.y,
        region.x as i64 + region.width as i64,
        region.y as i64 + region.height as i64,
    );

    capture_region_to_png(
        CaptureBackendKind::Gdi,
        "GDI",
        &region,
        "./region-capture-gdi.png",
    )?;
    capture_region_to_png(
        CaptureBackendKind::DxgiDuplication,
        "DXGI",
        &region,
        "./region-capture-dxgi.png",
    )?;
    capture_region_to_png(
        CaptureBackendKind::WindowsGraphicsCapture,
        "WGC",
        &region,
        "./region-capture-wgc.png",
    )?;

    Ok(())
}
