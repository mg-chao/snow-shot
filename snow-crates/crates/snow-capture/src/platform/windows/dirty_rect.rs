use crate::frame::DirtyRect;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) struct DirtyCopyStrategy {
    pub(crate) cpu: bool,
    pub(crate) gpu: bool,
    pub(crate) gpu_low_latency: bool,
    pub(crate) dirty_pixels: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct DirtyCopyThresholds {
    pub(crate) max_rects: usize,
    pub(crate) max_area_percent: u64,
    pub(crate) gpu_max_rects: usize,
    pub(crate) gpu_max_area_percent: u64,
    pub(crate) gpu_low_latency_max_rects: usize,
    pub(crate) gpu_low_latency_max_area_percent: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct DirtyRectDenseMergeThresholds {
    pub(crate) min_rects: usize,
    pub(crate) max_vertical_span: u32,
}

#[inline(always)]
pub(crate) fn clamp_dirty_rect(rect: DirtyRect, width: u32, height: u32) -> Option<DirtyRect> {
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

#[inline(always)]
pub(crate) fn dirty_rects_sorted_by_y_then_x(rects: &[DirtyRect]) -> bool {
    if rects.len() <= 1 {
        return true;
    }

    let mut previous = rects[0];
    for rect in &rects[1..] {
        if rect.y < previous.y || (rect.y == previous.y && rect.x < previous.x) {
            return false;
        }
        previous = *rect;
    }
    true
}

#[derive(Clone, Copy)]
pub(crate) struct DirtyRectMergeCandidate {
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

#[inline(always)]
#[cfg(test)]
pub(crate) fn dirty_rects_can_merge(a: DirtyRect, b: DirtyRect) -> bool {
    DirtyRectMergeCandidate::new(a).can_merge(DirtyRectMergeCandidate::new(b))
}

#[inline(always)]
#[cfg(test)]
pub(crate) fn merge_dirty_rects(a: DirtyRect, b: DirtyRect) -> DirtyRect {
    let mut merged = DirtyRectMergeCandidate::new(a);
    merged.merge_in_place(DirtyRectMergeCandidate::new(b));
    merged.rect
}

#[inline(always)]
unsafe fn remove_dirty_rect_candidate_at_unchecked(
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

pub(crate) fn normalize_dirty_rects_after_prepare_in_place(
    pending: &mut Vec<DirtyRect>,
    already_sorted_hint: bool,
    _dense_thresholds: DirtyRectDenseMergeThresholds,
    scratch_candidates: &mut Vec<DirtyRectMergeCandidate>,
) {
    if pending.len() <= 1 {
        return;
    }

    debug_assert!(!already_sorted_hint || dirty_rects_sorted_by_y_then_x(pending));
    if !already_sorted_hint && !dirty_rects_sorted_by_y_then_x(pending) {
        pending.sort_unstable_by(|a, b| a.y.cmp(&b.y).then_with(|| a.x.cmp(&b.x)));
    }

    scratch_candidates.clear();
    scratch_candidates.reserve(pending.len());

    for rect in pending.iter().copied() {
        let mut candidate = DirtyRectMergeCandidate::new(rect);
        loop {
            let mut merged_any = false;
            let mut candidate_bottom = candidate.bottom;
            let mut idx = 0usize;
            while idx < scratch_candidates.len() {
                let existing = scratch_candidates[idx];
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
                    unsafe { remove_dirty_rect_candidate_at_unchecked(scratch_candidates, idx) };
                    merged_any = true;
                } else {
                    idx += 1;
                }
            }

            if !merged_any {
                break;
            }
        }

        let insert_at = scratch_candidates
            .binary_search_by(|probe| {
                probe
                    .rect
                    .y
                    .cmp(&candidate.rect.y)
                    .then_with(|| probe.rect.x.cmp(&candidate.rect.x))
            })
            .unwrap_or_else(|pos| pos);
        scratch_candidates.insert(insert_at, candidate);
    }

    pending.clear();
    pending.extend(scratch_candidates.iter().map(|candidate| candidate.rect));
    scratch_candidates.clear();
}

pub(crate) fn normalize_dirty_rects_in_place(
    rects: &mut Vec<DirtyRect>,
    width: u32,
    height: u32,
    already_sorted_hint: bool,
    dense_thresholds: DirtyRectDenseMergeThresholds,
    _allow_optimized_merge: bool,
    scratch_candidates: &mut Vec<DirtyRectMergeCandidate>,
) {
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

    normalize_dirty_rects_after_prepare_in_place(
        &mut pending,
        already_sorted_hint,
        dense_thresholds,
        scratch_candidates,
    );
    *rects = pending;
}

pub(crate) fn evaluate_dirty_copy_strategy(
    rects: &[DirtyRect],
    width: u32,
    height: u32,
    thresholds: DirtyCopyThresholds,
) -> DirtyCopyStrategy {
    if rects.is_empty() {
        return DirtyCopyStrategy::default();
    }

    let total_pixels = (width as u64).saturating_mul(height as u64);
    if total_pixels == 0 {
        return DirtyCopyStrategy::default();
    }

    if rects.len() > thresholds.max_rects {
        return DirtyCopyStrategy::default();
    }
    let gpu_candidate = rects.len() <= thresholds.gpu_max_rects;
    let low_latency_candidate = rects.len() <= thresholds.gpu_low_latency_max_rects;

    let cpu_limit = total_pixels.saturating_mul(thresholds.max_area_percent);
    let mut dirty_pixels = 0u64;

    if !gpu_candidate {
        for rect in rects {
            dirty_pixels =
                dirty_pixels.saturating_add((rect.width as u64).saturating_mul(rect.height as u64));
            if dirty_pixels.saturating_mul(100) > cpu_limit {
                return DirtyCopyStrategy::default();
            }
        }
        return DirtyCopyStrategy {
            cpu: true,
            gpu: false,
            gpu_low_latency: false,
            dirty_pixels,
        };
    }

    let gpu_limit = total_pixels.saturating_mul(thresholds.gpu_max_area_percent);
    if !low_latency_candidate {
        let mut cpu = true;
        let mut gpu = true;
        for rect in rects {
            dirty_pixels =
                dirty_pixels.saturating_add((rect.width as u64).saturating_mul(rect.height as u64));
            let dirty_percent_scaled = dirty_pixels.saturating_mul(100);
            if gpu && dirty_percent_scaled > gpu_limit {
                gpu = false;
            }
            if cpu && dirty_percent_scaled > cpu_limit {
                cpu = false;
            }
            if !cpu && !gpu {
                break;
            }
        }
        return DirtyCopyStrategy {
            cpu,
            gpu,
            gpu_low_latency: false,
            dirty_pixels,
        };
    }

    let gpu_low_latency_limit =
        total_pixels.saturating_mul(thresholds.gpu_low_latency_max_area_percent);
    let mut cpu = true;
    let mut gpu = true;
    let mut gpu_low_latency = true;
    for rect in rects {
        dirty_pixels =
            dirty_pixels.saturating_add((rect.width as u64).saturating_mul(rect.height as u64));
        let dirty_percent_scaled = dirty_pixels.saturating_mul(100);
        if gpu_low_latency && dirty_percent_scaled > gpu_low_latency_limit {
            gpu_low_latency = false;
        }
        if gpu && dirty_percent_scaled > gpu_limit {
            gpu = false;
        }
        if cpu && dirty_percent_scaled > cpu_limit {
            cpu = false;
        }
        if !cpu && !gpu && !gpu_low_latency {
            break;
        }
    }

    DirtyCopyStrategy {
        cpu,
        gpu,
        gpu_low_latency,
        dirty_pixels,
    }
}

#[cfg(test)]
pub(crate) fn normalize_dirty_rects_reference_in_place(
    rects: &mut Vec<DirtyRect>,
    width: u32,
    height: u32,
    _dense_thresholds: DirtyRectDenseMergeThresholds,
) {
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
