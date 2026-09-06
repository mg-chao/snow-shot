use std::hint::black_box;
use std::time::{Duration, Instant};

use anyhow::{Result, bail};
use snow_capture::frame::DirtyRect;

const BENCH_WIDTH: u32 = 1920;
const BENCH_HEIGHT: u32 = 1080;
const DEFAULT_ROUNDS: usize = 8;
const DEFAULT_ITERATIONS: usize = 15_000;
const DEFAULT_MAX_REGRESSION_PCT: f64 = 3.0;
const DENSE_MERGE_BASELINE_MIN_RECTS: usize = 64;
const DENSE_MERGE_BASELINE_MAX_VERTICAL_SPAN: u32 = 96;

#[derive(Clone)]
struct Workload {
    name: &'static str,
    rects: Vec<DirtyRect>,
}

#[derive(Clone, Copy)]
struct CaseTiming {
    baseline: Duration,
    optimized_a: Duration,
    optimized_b: Duration,
    optimized_c: Duration,
}

fn clamp_dirty_rect(rect: DirtyRect, width: u32, height: u32) -> Option<DirtyRect> {
    let x = rect.x.min(width);
    let y = rect.y.min(height);
    if x >= width || y >= height {
        return None;
    }

    let max_w = width - x;
    let max_h = height - y;
    let clamped_w = rect.width.min(max_w);
    let clamped_h = rect.height.min(max_h);
    if clamped_w == 0 || clamped_h == 0 {
        return None;
    }

    Some(DirtyRect {
        x,
        y,
        width: clamped_w,
        height: clamped_h,
    })
}

#[inline(always)]
fn dirty_rect_bounds(rect: DirtyRect) -> (u32, u32) {
    (
        rect.x.saturating_add(rect.width),
        rect.y.saturating_add(rect.height),
    )
}

#[inline(always)]
fn intervals_overlap(a_start: u32, a_end: u32, b_start: u32, b_end: u32) -> bool {
    a_start < b_end && b_start < a_end
}

#[inline(always)]
fn intervals_touch_or_overlap(a_start: u32, a_end: u32, b_start: u32, b_end: u32) -> bool {
    a_start <= b_end && b_start <= a_end
}

#[derive(Clone, Copy)]
struct DirtyRectMergeCandidate {
    rect: DirtyRect,
    right: u32,
    bottom: u32,
}

impl DirtyRectMergeCandidate {
    #[inline(always)]
    fn new(rect: DirtyRect) -> Self {
        let (right, bottom) = dirty_rect_bounds(rect);
        Self {
            rect,
            right,
            bottom,
        }
    }

    #[inline(always)]
    fn can_merge(self, other: Self) -> bool {
        let horizontal_overlap =
            intervals_overlap(self.rect.x, self.right, other.rect.x, other.right);
        let vertical_overlap =
            intervals_overlap(self.rect.y, self.bottom, other.rect.y, other.bottom);
        let horizontal_touch_or_overlap =
            intervals_touch_or_overlap(self.rect.x, self.right, other.rect.x, other.right);
        let vertical_touch_or_overlap =
            intervals_touch_or_overlap(self.rect.y, self.bottom, other.rect.y, other.bottom);

        (horizontal_overlap && vertical_touch_or_overlap)
            || (vertical_overlap && horizontal_touch_or_overlap)
    }

    #[inline(always)]
    fn merge_in_place(&mut self, other: Self) {
        self.rect.x = self.rect.x.min(other.rect.x);
        self.rect.y = self.rect.y.min(other.rect.y);
        self.right = self.right.max(other.right);
        self.bottom = self.bottom.max(other.bottom);
        self.rect.width = self.right.saturating_sub(self.rect.x);
        self.rect.height = self.bottom.saturating_sub(self.rect.y);
    }
}

fn dirty_rects_can_merge(a: DirtyRect, b: DirtyRect) -> bool {
    DirtyRectMergeCandidate::new(a).can_merge(DirtyRectMergeCandidate::new(b))
}

fn merge_dirty_rects(a: DirtyRect, b: DirtyRect) -> DirtyRect {
    let mut merged = DirtyRectMergeCandidate::new(a);
    merged.merge_in_place(DirtyRectMergeCandidate::new(b));
    merged.rect
}

fn normalize_dirty_rects_baseline(rects: &mut Vec<DirtyRect>, width: u32, height: u32) {
    if rects.is_empty() {
        return;
    }

    let mut write = 0usize;
    for read in 0..rects.len() {
        if let Some(clamped) = clamp_dirty_rect(rects[read], width, height) {
            rects[write] = clamped;
            write += 1;
        }
    }
    rects.truncate(write);
    if rects.len() <= 1 {
        return;
    }

    let mut changed = true;
    while changed {
        changed = false;

        let mut i = 0usize;
        while i < rects.len() {
            let mut j = i + 1;
            while j < rects.len() {
                if dirty_rects_can_merge(rects[i], rects[j]) {
                    rects[i] = merge_dirty_rects(rects[i], rects[j]);
                    rects.swap_remove(j);
                    changed = true;
                } else {
                    j += 1;
                }
            }
            i += 1;
        }
    }

    rects.sort_unstable_by(|a, b| a.y.cmp(&b.y).then_with(|| a.x.cmp(&b.x)));
}

fn normalize_dirty_rects_baseline_after_clamp(rects: &mut Vec<DirtyRect>) {
    let mut changed = true;
    while changed {
        changed = false;

        let mut i = 0usize;
        while i < rects.len() {
            let mut j = i + 1;
            while j < rects.len() {
                if dirty_rects_can_merge(rects[i], rects[j]) {
                    rects[i] = merge_dirty_rects(rects[i], rects[j]);
                    rects.swap_remove(j);
                    changed = true;
                } else {
                    j += 1;
                }
            }
            i += 1;
        }
    }

    rects.sort_unstable_by(|a, b| a.y.cmp(&b.y).then_with(|| a.x.cmp(&b.x)));
}

#[inline(always)]
unsafe fn remove_dirty_rect_at_unchecked(rects: &mut Vec<DirtyRect>, idx: usize) {
    let len = rects.len();
    debug_assert!(idx < len);
    let ptr = rects.as_mut_ptr();
    unsafe {
        let tail_len = len - idx - 1;
        if tail_len > 0 {
            std::ptr::copy(ptr.add(idx + 1), ptr.add(idx), tail_len);
        }
        rects.set_len(len - 1);
    }
}

#[inline(always)]
unsafe fn remove_merge_candidate_at_unchecked(
    candidates: &mut Vec<DirtyRectMergeCandidate>,
    idx: usize,
) {
    let len = candidates.len();
    debug_assert!(idx < len);
    let ptr = candidates.as_mut_ptr();
    unsafe {
        let tail_len = len - idx - 1;
        if tail_len > 0 {
            std::ptr::copy(ptr.add(idx + 1), ptr.add(idx), tail_len);
        }
        candidates.set_len(len - 1);
    }
}

fn should_use_baseline_dense_merge(rects: &[DirtyRect]) -> bool {
    if rects.len() < DENSE_MERGE_BASELINE_MIN_RECTS {
        return false;
    }

    let mut min_y = u32::MAX;
    let mut max_y = 0u32;
    for rect in rects {
        min_y = min_y.min(rect.y);
        max_y = max_y.max(rect.y.saturating_add(rect.height));
    }

    max_y.saturating_sub(min_y) <= DENSE_MERGE_BASELINE_MAX_VERTICAL_SPAN
}

fn normalize_dirty_rects_optimized_a(rects: &mut Vec<DirtyRect>, width: u32, height: u32) {
    if rects.is_empty() {
        return;
    }

    let mut pending = std::mem::take(rects);
    let mut write = 0usize;
    for read in 0..pending.len() {
        if let Some(clamped) = clamp_dirty_rect(pending[read], width, height) {
            pending[write] = clamped;
            write += 1;
        }
    }
    pending.truncate(write);
    if pending.len() <= 1 {
        *rects = pending;
        return;
    }

    if should_use_baseline_dense_merge(&pending) {
        *rects = pending;
        normalize_dirty_rects_baseline_after_clamp(rects);
        return;
    }

    pending.sort_unstable_by(|a, b| a.y.cmp(&b.y).then_with(|| a.x.cmp(&b.x)));

    rects.reserve(pending.len());
    for rect in pending {
        let mut candidate = rect;
        loop {
            let mut merged_any = false;
            let candidate_bottom = candidate.y.saturating_add(candidate.height);
            let mut idx = 0usize;

            while idx < rects.len() {
                let existing = rects[idx];
                let existing_bottom = existing.y.saturating_add(existing.height);
                if existing_bottom < candidate.y {
                    idx += 1;
                    continue;
                }
                if existing.y > candidate_bottom {
                    break;
                }

                if dirty_rects_can_merge(candidate, existing) {
                    candidate = merge_dirty_rects(candidate, existing);
                    rects.remove(idx);
                    merged_any = true;
                } else {
                    idx += 1;
                }
            }

            if !merged_any {
                break;
            }
        }

        let insert_at = rects
            .binary_search_by(|probe| {
                probe
                    .y
                    .cmp(&candidate.y)
                    .then_with(|| probe.x.cmp(&candidate.x))
            })
            .unwrap_or_else(|pos| pos);
        rects.insert(insert_at, candidate);
    }
}

fn normalize_dirty_rects_optimized_b(rects: &mut Vec<DirtyRect>, width: u32, height: u32) {
    if rects.is_empty() {
        return;
    }

    let mut pending = std::mem::take(rects);
    let mut write = 0usize;
    for read in 0..pending.len() {
        if let Some(clamped) = clamp_dirty_rect(pending[read], width, height) {
            pending[write] = clamped;
            write += 1;
        }
    }
    pending.truncate(write);
    if pending.len() <= 1 {
        *rects = pending;
        return;
    }

    if should_use_baseline_dense_merge(&pending) {
        *rects = pending;
        normalize_dirty_rects_baseline_after_clamp(rects);
        return;
    }

    pending.sort_unstable_by(|a, b| a.y.cmp(&b.y).then_with(|| a.x.cmp(&b.x)));

    rects.reserve(pending.len());
    for rect in pending {
        let mut candidate = rect;
        loop {
            let mut merged_any = false;
            let candidate_bottom = candidate.y.saturating_add(candidate.height);
            let mut idx = 0usize;
            while idx < rects.len() {
                let existing = rects[idx];
                let existing_bottom = existing.y.saturating_add(existing.height);
                if existing_bottom < candidate.y {
                    idx += 1;
                    continue;
                }
                if existing.y > candidate_bottom {
                    break;
                }

                if dirty_rects_can_merge(candidate, existing) {
                    candidate = merge_dirty_rects(candidate, existing);
                    // SAFETY: `idx` is bounded by the loop condition (`idx < rects.len()`).
                    unsafe { remove_dirty_rect_at_unchecked(rects, idx) };
                    merged_any = true;
                } else {
                    idx += 1;
                }
            }

            if !merged_any {
                break;
            }
        }

        let insert_at = rects
            .binary_search_by(|probe| {
                probe
                    .y
                    .cmp(&candidate.y)
                    .then_with(|| probe.x.cmp(&candidate.x))
            })
            .unwrap_or_else(|pos| pos);
        rects.insert(insert_at, candidate);
    }
}

fn normalize_dirty_rects_optimized_c(rects: &mut Vec<DirtyRect>, width: u32, height: u32) {
    if rects.is_empty() {
        return;
    }

    let mut pending = std::mem::take(rects);
    let mut write = 0usize;
    for read in 0..pending.len() {
        if let Some(clamped) = clamp_dirty_rect(pending[read], width, height) {
            pending[write] = clamped;
            write += 1;
        }
    }
    pending.truncate(write);
    if pending.len() <= 1 {
        *rects = pending;
        return;
    }

    if should_use_baseline_dense_merge(&pending) {
        *rects = pending;
        normalize_dirty_rects_baseline_after_clamp(rects);
        return;
    }

    pending.sort_unstable_by(|a, b| a.y.cmp(&b.y).then_with(|| a.x.cmp(&b.x)));

    let mut merged: Vec<DirtyRectMergeCandidate> = Vec::with_capacity(pending.len());
    for rect in pending {
        let mut candidate = DirtyRectMergeCandidate::new(rect);
        loop {
            let mut merged_any = false;
            let mut candidate_bottom = candidate.bottom;
            let mut idx = 0usize;
            while idx < merged.len() {
                let existing = merged[idx];
                if existing.bottom < candidate.rect.y {
                    idx += 1;
                    continue;
                }
                if existing.rect.y > candidate_bottom {
                    break;
                }

                if candidate.can_merge(existing) {
                    candidate.merge_in_place(existing);
                    candidate_bottom = candidate.bottom;
                    // SAFETY: `idx` is bounded by the loop condition (`idx < merged.len()`).
                    unsafe { remove_merge_candidate_at_unchecked(&mut merged, idx) };
                    merged_any = true;
                } else {
                    idx += 1;
                }
            }

            if !merged_any {
                break;
            }
        }

        let insert_at = merged
            .binary_search_by(|probe| {
                probe
                    .rect
                    .y
                    .cmp(&candidate.rect.y)
                    .then_with(|| probe.rect.x.cmp(&candidate.rect.x))
            })
            .unwrap_or_else(|pos| pos);
        merged.insert(insert_at, candidate);
    }

    rects.reserve(merged.len());
    rects.extend(merged.into_iter().map(|candidate| candidate.rect));
}

fn workload_sparse_grid() -> Workload {
    let mut rects = Vec::new();
    for y in (0..BENCH_HEIGHT).step_by(90) {
        for x in (0..BENCH_WIDTH).step_by(120) {
            rects.push(DirtyRect {
                x,
                y,
                width: 24,
                height: 20,
            });
        }
    }
    Workload {
        name: "sparse-grid",
        rects,
    }
}

fn workload_cascading_merges() -> Workload {
    let mut rects = Vec::new();
    for i in 0..200u32 {
        let y = (i % 4) * 2;
        rects.push(DirtyRect {
            x: i * 6,
            y,
            width: 7,
            height: 6,
        });
        rects.push(DirtyRect {
            x: i * 6 + 6,
            y: y + 4,
            width: 8,
            height: 6,
        });
    }
    Workload {
        name: "cascading-merges",
        rects,
    }
}

fn workload_mixed_noise() -> Workload {
    let mut rects = Vec::new();
    let mut state = 0x8f31_d2a4_u64;
    for _ in 0..260 {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        let x = ((state >> 16) as u32) % BENCH_WIDTH;
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        let y = ((state >> 20) as u32) % BENCH_HEIGHT;
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        let w = 8 + (((state >> 24) as u32) % 64);
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        let h = 6 + (((state >> 28) as u32) % 56);
        rects.push(DirtyRect {
            x,
            y,
            width: w,
            height: h,
        });
    }
    Workload {
        name: "mixed-noise",
        rects,
    }
}

fn bench_variant(
    seed_rects: &[DirtyRect],
    rounds: usize,
    iterations: usize,
    normalize: fn(&mut Vec<DirtyRect>, u32, u32),
) -> Duration {
    let mut best = Duration::MAX;
    let mut work = Vec::with_capacity(seed_rects.len());

    for _ in 0..rounds {
        let mut checksum = 0u64;
        let start = Instant::now();
        for _ in 0..iterations {
            work.clear();
            work.extend_from_slice(seed_rects);
            normalize(&mut work, BENCH_WIDTH, BENCH_HEIGHT);
            checksum = checksum.wrapping_add(work.len() as u64);
            if let Some(first) = work.first() {
                checksum = checksum.wrapping_add(first.width as u64);
            }
        }
        black_box(checksum);
        best = best.min(start.elapsed());
    }

    best
}

fn ns_per_iter(duration: Duration, iterations: usize) -> f64 {
    duration.as_secs_f64() * 1_000_000_000.0 / iterations as f64
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
                rounds = raw.parse::<usize>()?;
                i += 2;
            }
            "--iterations" => {
                let Some(raw) = args.get(i + 1) else {
                    bail!("--iterations requires a value");
                };
                iterations = raw.parse::<usize>()?;
                i += 2;
            }
            "--max-regression-pct" => {
                let Some(raw) = args.get(i + 1) else {
                    bail!("--max-regression-pct requires a value");
                };
                max_regression_pct = raw.parse::<f64>()?;
                i += 2;
            }
            "--help" | "-h" => {
                println!(
                    "Usage: cargo run --release --example wgc_dirty_rect_benchmark -- [options]
  --rounds <n>               Benchmark rounds per workload (default: {DEFAULT_ROUNDS})
  --iterations <n>           Iterations per workload per round (default: {DEFAULT_ITERATIONS})
  --max-regression-pct <f>   Allowed cached-merge slowdown vs baseline-safe before failing (default: {DEFAULT_MAX_REGRESSION_PCT})"
                );
                std::process::exit(0);
            }
            other => bail!("unknown argument: {other}"),
        }
    }

    if rounds == 0 {
        bail!("--rounds must be >= 1");
    }
    if iterations == 0 {
        bail!("--iterations must be >= 1");
    }
    if max_regression_pct < 0.0 {
        bail!("--max-regression-pct must be >= 0");
    }

    Ok((rounds, iterations, max_regression_pct))
}

fn verify_equivalence(workload: &Workload) -> Result<()> {
    let mut baseline = workload.rects.clone();
    let mut optimized_a = workload.rects.clone();
    let mut optimized_b = workload.rects.clone();
    let mut optimized_c = workload.rects.clone();
    normalize_dirty_rects_baseline(&mut baseline, BENCH_WIDTH, BENCH_HEIGHT);
    normalize_dirty_rects_optimized_a(&mut optimized_a, BENCH_WIDTH, BENCH_HEIGHT);
    normalize_dirty_rects_optimized_b(&mut optimized_b, BENCH_WIDTH, BENCH_HEIGHT);
    normalize_dirty_rects_optimized_c(&mut optimized_c, BENCH_WIDTH, BENCH_HEIGHT);

    if baseline != optimized_a {
        bail!(
            "normalized output mismatch for workload `{}` (baseline {} rects vs optimized-a {} rects)",
            workload.name,
            baseline.len(),
            optimized_a.len()
        );
    }

    if baseline != optimized_b {
        bail!(
            "normalized output mismatch for workload `{}` (baseline {} rects vs optimized-b {} rects)",
            workload.name,
            baseline.len(),
            optimized_b.len()
        );
    }

    if baseline != optimized_c {
        bail!(
            "normalized output mismatch for workload `{}` (baseline {} rects vs optimized-c {} rects)",
            workload.name,
            baseline.len(),
            optimized_c.len()
        );
    }

    Ok(())
}

fn run_case(workload: &Workload, rounds: usize, iterations: usize) -> CaseTiming {
    let baseline = bench_variant(
        &workload.rects,
        rounds,
        iterations,
        normalize_dirty_rects_baseline,
    );
    let optimized_a = bench_variant(
        &workload.rects,
        rounds,
        iterations,
        normalize_dirty_rects_optimized_a,
    );
    let optimized_b = bench_variant(
        &workload.rects,
        rounds,
        iterations,
        normalize_dirty_rects_optimized_b,
    );
    let optimized_c = bench_variant(
        &workload.rects,
        rounds,
        iterations,
        normalize_dirty_rects_optimized_c,
    );
    CaseTiming {
        baseline,
        optimized_a,
        optimized_b,
        optimized_c,
    }
}

fn main() -> Result<()> {
    let (rounds, iterations, max_regression_pct) = parse_args()?;
    let workloads = vec![
        workload_sparse_grid(),
        workload_cascading_merges(),
        workload_mixed_noise(),
    ];

    println!(
        "Running WGC dirty-rect benchmark: rounds={} iterations={} max_regression_pct={:.2}",
        rounds, iterations, max_regression_pct
    );
    println!(
        "{:<20} {:>12} {:>12} {:>12} {:>12} {:>12} {:>12}",
        "workload",
        "baseline(ns)",
        "safe(ns)",
        "unsafe(ns)",
        "cached(ns)",
        "safe/cached",
        "cached/baseline"
    );

    let mut regressions = Vec::new();
    for workload in &workloads {
        verify_equivalence(workload)?;
        let timing = run_case(workload, rounds, iterations);
        let baseline_ns = ns_per_iter(timing.baseline, iterations);
        let safe_ns = ns_per_iter(timing.optimized_a, iterations);
        let unsafe_ns = ns_per_iter(timing.optimized_b, iterations);
        let cached_ns = ns_per_iter(timing.optimized_c, iterations);
        let safe_vs_cached = if cached_ns > 0.0 {
            safe_ns / cached_ns
        } else {
            f64::INFINITY
        };
        let cached_vs_baseline = if baseline_ns > 0.0 {
            cached_ns / baseline_ns
        } else {
            f64::INFINITY
        };
        println!(
            "{:<20} {:>12.1} {:>12.1} {:>12.1} {:>12.1} {:>11.2}x {:>11.2}x",
            workload.name,
            baseline_ns,
            safe_ns,
            unsafe_ns,
            cached_ns,
            safe_vs_cached,
            cached_vs_baseline
        );

        let delta_pct = if safe_ns > 0.0 {
            ((cached_ns - safe_ns) / safe_ns) * 100.0
        } else {
            0.0
        };
        if delta_pct > max_regression_pct {
            regressions.push(format!(
                "{} regressed by {:.2}% (safe {:.1} ns -> cached {:.1} ns)",
                workload.name, delta_pct, safe_ns, cached_ns
            ));
        }
    }

    if regressions.is_empty() {
        println!("Regression guard passed.");
        Ok(())
    } else {
        bail!(
            "dirty-rect normalization regression detected:\n{}",
            regressions.join("\n")
        )
    }
}
