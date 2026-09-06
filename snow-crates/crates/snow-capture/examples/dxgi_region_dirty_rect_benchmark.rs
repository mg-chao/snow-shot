#![allow(clippy::too_many_arguments)]

use std::hint::black_box;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, bail};
use snow_capture::DirtyRect;
use snow_capture::convert::convert_bgra_to_rgba;

const WIDTH: u32 = 1920;
const HEIGHT: u32 = 1080;
const DEFAULT_ROUNDS: usize = 6;
const DEFAULT_ITERATIONS: usize = 2500;
const DEFAULT_MAX_REGRESSION_PCT: f64 = 5.0;

#[derive(Clone)]
struct Workload {
    name: &'static str,
    rects: Vec<DirtyRect>,
}

#[derive(Clone, Copy)]
struct WorkItem {
    src_offset: usize,
    dst_offset: usize,
    width: usize,
    height: usize,
}

#[derive(Clone, Copy)]
struct CaseTiming {
    work_item: Duration,
    direct_scan: Duration,
    direct_hinted: Duration,
}

fn parse_args() -> Result<(usize, usize, f64)> {
    let mut rounds = DEFAULT_ROUNDS;
    let mut iterations = DEFAULT_ITERATIONS;
    let mut max_regression_pct = DEFAULT_MAX_REGRESSION_PCT;

    let args: Vec<String> = std::env::args().collect();
    let mut i = 1usize;
    while i < args.len() {
        match args[i].as_str() {
            "--rounds" => {
                let Some(raw) = args.get(i + 1) else {
                    bail!("--rounds requires a value");
                };
                rounds = raw
                    .parse::<usize>()
                    .with_context(|| format!("failed to parse --rounds value: {raw}"))?;
                i += 2;
            }
            "--iterations" => {
                let Some(raw) = args.get(i + 1) else {
                    bail!("--iterations requires a value");
                };
                iterations = raw
                    .parse::<usize>()
                    .with_context(|| format!("failed to parse --iterations value: {raw}"))?;
                i += 2;
            }
            "--max-regression-pct" => {
                let Some(raw) = args.get(i + 1) else {
                    bail!("--max-regression-pct requires a value");
                };
                max_regression_pct = raw.parse::<f64>().with_context(|| {
                    format!("failed to parse --max-regression-pct value: {raw}")
                })?;
                if !max_regression_pct.is_finite() || max_regression_pct < 0.0 {
                    bail!("--max-regression-pct must be a finite number >= 0");
                }
                i += 2;
            }
            "--help" | "-h" => {
                println!(
                    "Usage: cargo run --release --example dxgi_region_dirty_rect_benchmark -- [options]

Options:
  --rounds <N>                 Number of timing rounds (default: {DEFAULT_ROUNDS})
  --iterations <N>             Iterations per round (default: {DEFAULT_ITERATIONS})
  --max-regression-pct <PCT>   Allowed slowdown before failing (default: {DEFAULT_MAX_REGRESSION_PCT})"
                );
                std::process::exit(0);
            }
            other => bail!("unknown argument: {other}"),
        }
    }

    if rounds == 0 {
        bail!("--rounds must be > 0");
    }
    if iterations == 0 {
        bail!("--iterations must be > 0");
    }

    Ok((rounds, iterations, max_regression_pct))
}

fn row_major_rects(
    start_x: u32,
    start_y: u32,
    cols: u32,
    rows: u32,
    rect_w: u32,
    rect_h: u32,
    gap_x: u32,
    gap_y: u32,
    limit: usize,
) -> Vec<DirtyRect> {
    let mut out = Vec::with_capacity(limit);
    for row in 0..rows {
        for col in 0..cols {
            if out.len() == limit {
                return out;
            }
            let x = start_x + col * (rect_w + gap_x);
            let y = start_y + row * (rect_h + gap_y);
            out.push(DirtyRect {
                x,
                y,
                width: rect_w,
                height: rect_h,
            });
        }
    }
    out
}

fn make_workloads() -> Vec<Workload> {
    vec![
        Workload {
            name: "ui_sparse_tiles_32",
            rects: row_major_rects(24, 18, 8, 4, 32, 28, 24, 18, 32),
        },
        Workload {
            name: "chatty_small_updates_96",
            rects: row_major_rects(8, 8, 16, 6, 10, 8, 8, 6, 96),
        },
        Workload {
            name: "wide_strip_updates_20",
            rects: row_major_rects(32, 24, 2, 10, 640, 18, 96, 10, 20),
        },
        Workload {
            name: "dense_micro_tiles_180",
            rects: row_major_rects(6, 6, 30, 6, 8, 6, 4, 3, 180),
        },
    ]
}

fn dirty_rects_fit_bounds(
    dirty_rects: &[DirtyRect],
    src_width: u32,
    src_height: u32,
    dst_origin_x: u32,
    dst_origin_y: u32,
    dst_width: u32,
    dst_height: u32,
) -> bool {
    for rect in dirty_rects {
        if rect.width == 0 || rect.height == 0 {
            continue;
        }

        let Some(src_right) = rect.x.checked_add(rect.width) else {
            return false;
        };
        let Some(src_bottom) = rect.y.checked_add(rect.height) else {
            return false;
        };
        if src_right > src_width || src_bottom > src_height {
            return false;
        }

        let Some(dst_x) = dst_origin_x.checked_add(rect.x) else {
            return false;
        };
        let Some(dst_y) = dst_origin_y.checked_add(rect.y) else {
            return false;
        };
        let Some(dst_right) = dst_x.checked_add(rect.width) else {
            return false;
        };
        let Some(dst_bottom) = dst_y.checked_add(rect.height) else {
            return false;
        };
        if dst_right > dst_width || dst_bottom > dst_height {
            return false;
        }
    }
    true
}

fn dirty_rect_stats(rects: &[DirtyRect]) -> (usize, usize) {
    let mut non_empty_rects = 0usize;
    let mut dirty_pixels = 0usize;
    for rect in rects {
        let width = rect.width as usize;
        let height = rect.height as usize;
        if width == 0 || height == 0 {
            continue;
        }
        non_empty_rects = non_empty_rects.saturating_add(1);
        dirty_pixels = dirty_pixels.saturating_add(width.saturating_mul(height));
    }
    (non_empty_rects, dirty_pixels)
}

fn build_work_item_checked(
    rect: &DirtyRect,
    width: usize,
    height: usize,
    dst_origin_x: usize,
    dst_origin_y: usize,
    dst_width: usize,
    dst_height: usize,
    src_pitch: usize,
    dst_pitch: usize,
) -> Option<WorkItem> {
    let x = rect.x as usize;
    let y = rect.y as usize;
    let rect_w = rect.width as usize;
    let rect_h = rect.height as usize;

    if rect_w == 0 || rect_h == 0 || x >= width || y >= height {
        return None;
    }

    let end_x = x.saturating_add(rect_w).min(width);
    let end_y = y.saturating_add(rect_h).min(height);
    if end_x <= x || end_y <= y {
        return None;
    }
    let copy_w = end_x - x;
    let copy_h = end_y - y;

    let dst_x = dst_origin_x + x;
    let dst_y = dst_origin_y + y;
    if dst_x + copy_w > dst_width || dst_y + copy_h > dst_height {
        return None;
    }

    let src_offset = y * src_pitch + x * 4;
    let dst_offset = dst_y * dst_pitch + dst_x * 4;
    Some(WorkItem {
        src_offset,
        dst_offset,
        width: copy_w,
        height: copy_h,
    })
}

fn build_work_item_trusted(
    rect: DirtyRect,
    dst_origin_x: usize,
    dst_origin_y: usize,
    src_pitch: usize,
    dst_pitch: usize,
) -> Option<WorkItem> {
    if rect.width == 0 || rect.height == 0 {
        return None;
    }
    let x = rect.x as usize;
    let y = rect.y as usize;
    let width = rect.width as usize;
    let height = rect.height as usize;
    let dst_x = dst_origin_x + x;
    let dst_y = dst_origin_y + y;

    Some(WorkItem {
        src_offset: y * src_pitch + x * 4,
        dst_offset: dst_y * dst_pitch + dst_x * 4,
        width,
        height,
    })
}

fn convert_item(src: &[u8], dst: &mut [u8], src_pitch: usize, dst_pitch: usize, item: WorkItem) {
    let row_bytes = item.width * 4;
    for row in 0..item.height {
        let src_start = item.src_offset + row * src_pitch;
        let dst_start = item.dst_offset + row * dst_pitch;
        convert_bgra_to_rgba(
            &src[src_start..src_start + row_bytes],
            &mut dst[dst_start..dst_start + row_bytes],
            item.width,
        );
    }
}

fn convert_rect_direct(
    src: &[u8],
    dst: &mut [u8],
    src_pitch: usize,
    dst_pitch: usize,
    rect: DirtyRect,
    dst_origin_x: usize,
    dst_origin_y: usize,
) -> bool {
    if rect.width == 0 || rect.height == 0 {
        return false;
    }

    let x = rect.x as usize;
    let y = rect.y as usize;
    let width = rect.width as usize;
    let height = rect.height as usize;
    let dst_x = dst_origin_x + x;
    let dst_y = dst_origin_y + y;

    convert_item(
        src,
        dst,
        src_pitch,
        dst_pitch,
        WorkItem {
            src_offset: y * src_pitch + x * 4,
            dst_offset: dst_y * dst_pitch + dst_x * 4,
            width,
            height,
        },
    );
    true
}

fn convert_rects_direct_scan(
    src: &[u8],
    dst: &mut [u8],
    src_pitch: usize,
    dst_pitch: usize,
    rects: &[DirtyRect],
    dst_origin_x: usize,
    dst_origin_y: usize,
) -> usize {
    let mut converted = 0usize;
    let mut dirty_pixels = 0usize;
    for rect in rects {
        let width = rect.width as usize;
        let height = rect.height as usize;
        if width == 0 || height == 0 {
            continue;
        }
        converted = converted.saturating_add(1);
        dirty_pixels = dirty_pixels.saturating_add(width.saturating_mul(height));
    }
    if converted == 0 || dirty_pixels == 0 {
        return 0;
    }

    let mut converted_out = 0usize;
    for rect in rects {
        if convert_rect_direct(
            src,
            dst,
            src_pitch,
            dst_pitch,
            *rect,
            dst_origin_x,
            dst_origin_y,
        ) {
            converted_out = converted_out.saturating_add(1);
        }
    }
    converted_out
}

fn convert_rects_direct_hinted(
    src: &[u8],
    dst: &mut [u8],
    src_pitch: usize,
    dst_pitch: usize,
    rects: &[DirtyRect],
    dst_origin_x: usize,
    dst_origin_y: usize,
    non_empty_rects: usize,
    dirty_pixels: usize,
) -> usize {
    if non_empty_rects == 0 || dirty_pixels == 0 {
        return 0;
    }

    let mut converted_out = 0usize;
    for rect in rects {
        if convert_rect_direct(
            src,
            dst,
            src_pitch,
            dst_pitch,
            *rect,
            dst_origin_x,
            dst_origin_y,
        ) {
            converted_out = converted_out.saturating_add(1);
        }
    }
    converted_out
}

fn benchmark_workload(workload: &Workload, rounds: usize, iterations: usize) -> CaseTiming {
    let width = WIDTH as usize;
    let height = HEIGHT as usize;
    let src_pitch = width * 4;
    let dst_pitch = width * 4;
    let buffer_len = src_pitch * height;

    let mut src = vec![0u8; buffer_len];
    for (idx, byte) in src.iter_mut().enumerate() {
        *byte = (idx as u8).wrapping_mul(37).wrapping_add(11);
    }

    let trusted_rects = dirty_rects_fit_bounds(&workload.rects, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT);
    let (trusted_non_empty_rects, trusted_dirty_pixels) = dirty_rect_stats(&workload.rects);

    let mut best_work_item = Duration::MAX;
    let mut best_direct_scan = Duration::MAX;
    let mut best_direct_hinted = Duration::MAX;

    for _ in 0..rounds {
        let mut dst_work_item = vec![0u8; buffer_len];
        let mut work_item_checksum = 0u64;
        let mut work_items = Vec::with_capacity(workload.rects.len());
        let work_item_start = Instant::now();
        for iter in 0..iterations {
            work_items.clear();
            for rect in &workload.rects {
                let item = if trusted_rects {
                    build_work_item_trusted(*rect, 0, 0, src_pitch, dst_pitch)
                } else {
                    build_work_item_checked(
                        rect, width, height, 0, 0, width, height, src_pitch, dst_pitch,
                    )
                };
                if let Some(item) = item {
                    work_items.push(item);
                }
            }
            for item in &work_items {
                convert_item(&src, &mut dst_work_item, src_pitch, dst_pitch, *item);
            }
            work_item_checksum = work_item_checksum.wrapping_add(
                dst_work_item[(iter * 131 + work_items.len()) % dst_work_item.len()] as u64,
            );
        }
        black_box(work_item_checksum);
        best_work_item = best_work_item.min(work_item_start.elapsed());

        let mut dst_direct_scan = vec![0u8; buffer_len];
        let mut direct_scan_checksum = 0u64;
        let direct_scan_start = Instant::now();
        for iter in 0..iterations {
            let converted = if trusted_rects {
                convert_rects_direct_scan(
                    &src,
                    &mut dst_direct_scan,
                    src_pitch,
                    dst_pitch,
                    &workload.rects,
                    0,
                    0,
                )
            } else {
                let mut converted = 0usize;
                for rect in &workload.rects {
                    let Some(item) = build_work_item_checked(
                        rect, width, height, 0, 0, width, height, src_pitch, dst_pitch,
                    ) else {
                        continue;
                    };
                    convert_item(&src, &mut dst_direct_scan, src_pitch, dst_pitch, item);
                    converted = converted.saturating_add(1);
                }
                converted
            };
            direct_scan_checksum = direct_scan_checksum.wrapping_add(
                dst_direct_scan[(iter * 97 + converted) % dst_direct_scan.len()] as u64,
            );
        }
        black_box(direct_scan_checksum);
        best_direct_scan = best_direct_scan.min(direct_scan_start.elapsed());

        let mut dst_direct_hinted = vec![0u8; buffer_len];
        let mut direct_hinted_checksum = 0u64;
        let direct_hinted_start = Instant::now();
        for iter in 0..iterations {
            let converted = if trusted_rects {
                convert_rects_direct_hinted(
                    &src,
                    &mut dst_direct_hinted,
                    src_pitch,
                    dst_pitch,
                    &workload.rects,
                    0,
                    0,
                    trusted_non_empty_rects,
                    trusted_dirty_pixels,
                )
            } else {
                let mut converted = 0usize;
                for rect in &workload.rects {
                    let Some(item) = build_work_item_checked(
                        rect, width, height, 0, 0, width, height, src_pitch, dst_pitch,
                    ) else {
                        continue;
                    };
                    convert_item(&src, &mut dst_direct_hinted, src_pitch, dst_pitch, item);
                    converted = converted.saturating_add(1);
                }
                converted
            };
            direct_hinted_checksum = direct_hinted_checksum.wrapping_add(
                dst_direct_hinted[(iter * 89 + converted) % dst_direct_hinted.len()] as u64,
            );
        }
        black_box(direct_hinted_checksum);
        best_direct_hinted = best_direct_hinted.min(direct_hinted_start.elapsed());
    }

    let mut work_item_once = vec![0u8; buffer_len];
    let mut direct_scan_once = vec![0u8; buffer_len];
    let mut direct_hinted_once = vec![0u8; buffer_len];
    for rect in &workload.rects {
        let item = if trusted_rects {
            build_work_item_trusted(*rect, 0, 0, src_pitch, dst_pitch)
        } else {
            build_work_item_checked(
                rect, width, height, 0, 0, width, height, src_pitch, dst_pitch,
            )
        };
        if let Some(item) = item {
            convert_item(&src, &mut work_item_once, src_pitch, dst_pitch, item);
        }
        if trusted_rects {
            let _ = convert_rect_direct(
                &src,
                &mut direct_scan_once,
                src_pitch,
                dst_pitch,
                *rect,
                0,
                0,
            );
            let _ = convert_rect_direct(
                &src,
                &mut direct_hinted_once,
                src_pitch,
                dst_pitch,
                *rect,
                0,
                0,
            );
        } else if let Some(item) = build_work_item_checked(
            rect, width, height, 0, 0, width, height, src_pitch, dst_pitch,
        ) {
            convert_item(&src, &mut direct_scan_once, src_pitch, dst_pitch, item);
            convert_item(&src, &mut direct_hinted_once, src_pitch, dst_pitch, item);
        }
    }
    assert_eq!(
        work_item_once, direct_scan_once,
        "work-item/direct-scan output mismatch"
    );
    assert_eq!(
        work_item_once, direct_hinted_once,
        "work-item/direct-hinted output mismatch"
    );

    CaseTiming {
        work_item: best_work_item,
        direct_scan: best_direct_scan,
        direct_hinted: best_direct_hinted,
    }
}

fn duration_ms(duration: Duration) -> f64 {
    duration.as_secs_f64() * 1000.0
}

fn main() -> Result<()> {
    let (rounds, iterations, max_regression_pct) = parse_args()?;
    let workloads = make_workloads();

    println!(
        "Running DXGI region dirty-rect CPU benchmark: rounds={} iterations={} max_regression_pct={:.2}",
        rounds, iterations, max_regression_pct
    );
    println!(
        "{:<28} {:>12} {:>12} {:>12} {:>10} {:>11}",
        "workload", "work_item_ms", "scan_ms", "hinted_ms", "speedup", "regression"
    );

    let mut regressions = Vec::new();
    for workload in &workloads {
        let timing = benchmark_workload(workload, rounds, iterations);
        let work_item_ms = duration_ms(timing.work_item);
        let direct_scan_ms = duration_ms(timing.direct_scan);
        let direct_hinted_ms = duration_ms(timing.direct_hinted);
        let delta_pct = if direct_scan_ms > 0.0 {
            ((direct_hinted_ms - direct_scan_ms) / direct_scan_ms) * 100.0
        } else {
            0.0
        };
        let speedup = if direct_hinted_ms > 0.0 {
            direct_scan_ms / direct_hinted_ms
        } else {
            0.0
        };
        let regression_flag = if delta_pct > max_regression_pct {
            "FAIL"
        } else if delta_pct > 0.0 {
            "warn"
        } else {
            "ok"
        };
        println!(
            "{:<28} {:>12.3} {:>12.3} {:>12.3} {:>9.2}x {:>11}",
            workload.name, work_item_ms, direct_scan_ms, direct_hinted_ms, speedup, regression_flag
        );

        if delta_pct > max_regression_pct {
            regressions.push(format!(
                "{} regressed by {:.2}% (direct-scan {:.3} ms, direct-hinted {:.3} ms, work-item {:.3} ms)",
                workload.name, delta_pct, direct_scan_ms, direct_hinted_ms, work_item_ms
            ));
        }
    }

    if !regressions.is_empty() {
        bail!(
            "performance regression detected:\n{}",
            regressions.join("\n")
        );
    }
    Ok(())
}
