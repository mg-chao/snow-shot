//! Verify competing translations on native pixels sampled independently of the feature matches.

mod full_overlap;
mod search;
pub(super) use search::RegistrationSearch;

use super::{
    Frame, INLIER_TOLERANCE, MAX_CANDIDATES, MIN_INLIER_TILES, MIN_RESIDUAL_GAIN, StitchAxis,
};

// Sampling budgets apply to both axes and are independent of the screenshot layout.
const PROPOSAL_COLUMNS: usize = 64;
const LANES: usize = 8;
const VERIFICATION_COLUMNS: usize = 128;
const VERIFICATION_ROWS: usize = 256;
const MIN_SUPPORTED_BANDS: usize = LANES / 2;
type Color = [u8; 3];

#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq))]
pub(super) enum Registration {
    Unavailable,
    Ambiguous(Vec<i32>),
    Verified {
        offset: i32,
        error: f32,
        confidence: f32,
        blocks: u32,
    },
}

// Store both orientations in scroll order. Alpha does not contribute to registration.
struct View<'a> {
    pixels: &'a [u8],
    channels: usize,
    along_stride: usize,
    cross_stride: usize,
    height: usize,
    width: usize,
    columns: usize,
    samples: Vec<Color>,
}

impl<'a> View<'a> {
    fn new(frame: &'a Frame, axis: StitchAxis) -> Self {
        let frame_width = frame.width() as usize;
        let frame_height = frame.height() as usize;
        let channels = frame.pixel_format().channels() as usize;
        let row_stride = frame_width * channels;
        let (height, width, along_stride, cross_stride) = match axis {
            StitchAxis::Vertical => (frame_height, frame_width, row_stride, channels),
            StitchAxis::Horizontal => (frame_width, frame_height, channels, row_stride),
        };
        let mut view = Self {
            pixels: frame.pixels(),
            channels,
            along_stride,
            cross_stride,
            height,
            width,
            columns: PROPOSAL_COLUMNS.min(width),
            samples: Vec::new(),
        };
        view.samples = (0..view.height)
            .flat_map(|y| (0..view.columns).map(move |i| (y, i)))
            .map(|(y, i)| view.native(y, ((2 * i + 1) * view.width) / (2 * view.columns)))
            .collect();
        view
    }

    fn native(&self, along: usize, cross: usize) -> Color {
        let i = along * self.along_stride + cross * self.cross_stride;
        if self.channels == 1 {
            [self.pixels[i]; 3]
        } else {
            [self.pixels[i], self.pixels[i + 1], self.pixels[i + 2]]
        }
    }

    // If two copies are visible, shifting this entire input by the difference
    // between two hypotheses gives identical predictions. A repaint visible only
    // in the longer overlap cannot provide evidence for the shorter displacement.
    fn repeats_at(&self, shift: usize) -> bool {
        shift > 0
            && shift <= self.height / 2
            && (0..self.height - shift).all(|y| self.row(y) == self.row(y + shift))
            && (0..self.height - shift)
                .all(|y| (0..self.width).all(|x| self.native(y, x) == self.native(y + shift, x)))
    }

    fn pixel(&self, y: usize, x: usize, native: bool) -> Color {
        if native {
            self.native(y, x)
        } else {
            self.row(y)[x]
        }
    }

    fn signal<const GRADIENT: bool>(&self, y: usize, x: usize, native: bool) -> [i16; 3] {
        let value = self.pixel(y, x, native).map(i16::from);
        if GRADIENT {
            let previous = self.pixel(y - 1, x, native);
            return std::array::from_fn(|c| value[c] - i16::from(previous[c]));
        }
        value
    }

    fn row(&self, y: usize) -> &[Color] {
        &self.samples[y * self.columns..(y + 1) * self.columns]
    }
}

// Small axial-gradient patches propose integer shifts even on images without corners.
// Equal matches each contribute a fraction of a vote; a repeating texture can propose
// several shifts but cannot settle which one to use.
fn patch_signature(view: &View, y: usize, columns: std::ops::Range<usize>) -> u64 {
    let mut signature = 0_u64;
    for x in columns.step_by(2) {
        let base = view.row(y)[x];
        for dy in [1, 3, 5] {
            let next = view.row(y + dy)[x];
            for c in 0..3 {
                let gradient = i16::from(next[c]) - i16::from(base[c]);
                signature = signature.wrapping_mul(16777619) ^ gradient as u64;
            }
        }
    }
    signature
}

fn pixel_proposals(reference: &View, incoming: &View, limit: i32) -> Vec<i32> {
    let mut votes = vec![0.0_f32; 2 * limit as usize + 1];
    let lanes = LANES.min(reference.columns);
    let rows = incoming.height.saturating_sub(5);
    let vote_stride = (rows / 32).max(1);
    for lane in 0..lanes {
        let columns = lane * reference.columns / lanes..(lane + 1) * reference.columns / lanes;
        let signatures: Vec<_> = (0..rows)
            .map(|y| patch_signature(reference, y, columns.clone()))
            .collect();
        for y in (0..rows).step_by(vote_stride) {
            let signature = patch_signature(incoming, y, columns.clone());
            if signature == 0 {
                continue;
            }
            let start = y.saturating_sub(limit as usize);
            let end = (y + limit as usize + 1).min(rows);
            let matches = || (start..end).filter(|&r| signatures[r] == signature);
            let n = matches().count();
            for r in matches() {
                votes[y + limit as usize - r] += 1.0 / n as f32;
            }
        }
    }
    let mut peaks = votes
        .into_iter()
        .enumerate()
        .filter(|(_, w)| *w > 0.0)
        .collect::<Vec<_>>();
    peaks.sort_unstable_by(|a, b| b.1.total_cmp(&a.1).then(a.0.cmp(&b.0)));
    peaks
        .iter()
        .take(MAX_CANDIDATES)
        .map(|(i, _)| *i as i32 - limit)
        .collect()
}

fn candidate_shifts(pixels: &[i32], features: &[i32], limit: i32) -> Vec<i32> {
    let mut offsets = pixels.to_vec();
    for &d in features.iter().filter(|&&d| d != 0) {
        offsets.extend(
            d.saturating_sub(INLIER_TOLERANCE).max(-limit)
                ..=d.saturating_add(INLIER_TOLERANCE).min(limit),
        );
    }
    offsets.sort_unstable();
    offsets.dedup();
    offsets.retain(|&d| d != 0);
    offsets
}

fn overlap(height: usize, d: i32) -> std::ops::Range<usize> {
    d.max(0) as usize..height - (-d).max(0) as usize
}

fn source(y: usize, d: i32) -> usize {
    (y as i64 - i64::from(d)) as usize
}

#[derive(Clone, Copy)]
enum FitGrid {
    Proposal,
    Verification,
}

// Vary the sample inside each cross-axis bin from row to row. A periodic pattern
// aligned with the fixed proposal grid must also explain these native pixels.
fn verification_column(y: usize, x: usize, width: usize) -> usize {
    if width <= VERIFICATION_COLUMNS {
        return x;
    }
    let phase = ((y as u32).wrapping_mul(0x9e37_79b9) ^ (x as u32).wrapping_mul(0x85eb_ca6b)) >> 24;
    let position = (256 * x as u64 + u64::from(phase)) * width as u64;
    (position / (256 * VERIFICATION_COLUMNS as u64)) as usize
}

// Fit a constant exposure change. Equal saturated samples constrain no unique bias.
fn exposure(reference: &View, incoming: &View, d: i32) -> [i16; 3] {
    let mut counts = [[0u16; 511]; 3];
    let rows = overlap(incoming.height, d);
    for y in rows.clone().step_by((rows.len() / 32).max(1)) {
        for x in (0..incoming.columns).step_by(4) {
            let a = reference.row(source(y, d))[x];
            let b = incoming.row(y)[x];
            for c in 0..3 {
                if a[c] != b[c] || (a[c] != 0 && a[c] != 255) {
                    counts[c][(255 + i16::from(b[c]) - i16::from(a[c])) as usize] += 1;
                }
            }
        }
    }
    counts.map(|hist| {
        let mut mode = 255;
        for (i, &count) in hist.iter().enumerate() {
            if count > hist[mode] || (count == hist[mode] && i.abs_diff(255) < mode.abs_diff(255)) {
                mode = i;
            }
        }
        mode as i16 - 255
    })
}

#[inline]
fn distance<const GRADIENT: bool>(a: [i16; 3], b: [i16; 3], bias: [i16; 3]) -> u32 {
    if GRADIENT || bias == [0; 3] {
        return u32::from(a[0].abs_diff(b[0]))
            + u32::from(a[1].abs_diff(b[1]))
            + u32::from(a[2].abs_diff(b[2]));
    }
    (0..3)
        .map(|c| u32::from((a[c] + bias[c]).clamp(0, 255).abs_diff(b[c])))
        .sum()
}

struct Pair<'a> {
    reference: View<'a>,
    raw: View<'a>,
    incoming: View<'a>,
}

#[derive(Clone, Copy, Default)]
#[cfg_attr(test, derive(Debug, PartialEq))]
struct Residual {
    aligned: u32,
    change: u32,
}

struct Fit {
    overlap: OverlapResiduals,
    error: f32,
    blocks: u32,
    supported_bands: usize,
    dominant: bool,
}

#[cfg_attr(test, derive(Debug, PartialEq))]
struct OverlapResiduals {
    rows: Vec<[Residual; LANES]>,
    loss: f32,
    lower_loss: f32,
    absolute: u64,
    worst_row: u64,
}

impl OverlapResiduals {
    fn new(
        rows: Vec<[Residual; LANES]>,
        stripe_residual: [u64; LANES],
        budget: u64,
        absolute: u64,
        worst_row: u64,
    ) -> Self {
        let residual: u64 = stripe_residual.iter().sum();
        Self {
            rows,
            loss: if budget == 0 {
                1.0
            } else {
                residual as f32 / budget as f32
            },
            lower_loss: (residual - stripe_residual.into_iter().max().unwrap()) as f32
                / budget.max(1) as f32,
            absolute,
            worst_row,
        }
    }

    fn preference(&self, other: &Self) -> f32 {
        let mut better = 0u64;
        let mut worse = 0u64;
        for (a, b) in self.rows.iter().flatten().zip(other.rows.iter().flatten()) {
            // Both alignments must be observable at these incoming coordinates.
            let change = a.change.min(b.change);
            better += u64::from(b.aligned.saturating_sub(a.aligned).min(change));
            worse += u64::from(a.aligned.saturating_sub(b.aligned).min(change));
        }
        if better == worse {
            return 0.0;
        }
        (better as f32 - worse as f32) / better.max(worse) as f32
    }

    fn separate_preference(&self, other: &Self) -> f32 {
        // When the two displacements leave disjoint overlaps, each still predicts
        // its own observed pixels. Require contradictions beyond a single repaint.
        if other.lower_loss > self.loss {
            1.0 - self.loss / other.lower_loss
        } else if self.lower_loss > other.loss {
            other.loss / self.lower_loss - 1.0
        } else if self.absolute == 0 && other.absolute > other.worst_row {
            1.0
        } else if other.absolute == 0 && self.absolute > self.worst_row {
            -1.0
        } else {
            0.0
        }
    }

    fn compare(&self, other: &Self, threshold: f32) -> f32 {
        let margin = self.preference(other);
        if margin.abs() >= threshold {
            margin
        } else {
            self.separate_preference(other)
        }
    }
}

impl Pair<'_> {
    fn fit<const GRADIENT: bool>(&self, offset: i32, bias: [i16; 3], grid: FitGrid) -> Fit {
        let apply_exposure = !GRADIENT && bias != [0; 3];
        match (grid, apply_exposure) {
            (FitGrid::Proposal, false) => self.fit_grid::<GRADIENT, false, false>(offset, bias),
            (FitGrid::Proposal, true) => self.fit_grid::<GRADIENT, false, true>(offset, bias),
            (FitGrid::Verification, false) => self.fit_grid::<GRADIENT, true, false>(offset, bias),
            (FitGrid::Verification, true) => self.fit_grid::<GRADIENT, true, true>(offset, bias),
        }
    }

    fn fit_grid<const GRADIENT: bool, const NATIVE: bool, const BIASED: bool>(
        &self,
        offset: i32,
        bias: [i16; 3],
    ) -> Fit {
        let height = self.incoming.height;
        let width = self.incoming.width;
        let (count, stride) = if NATIVE {
            (
                width.min(VERIFICATION_COLUMNS),
                height.div_ceil(VERIFICATION_ROWS),
            )
        } else {
            (self.incoming.columns, (height / 256).max(1))
        };
        let bias = if BIASED { bias } else { [0; 3] };
        let mut rows = vec![[Residual::default(); LANES]; height.div_ceil(stride)];
        let mut stripe_residual = [0_u64; LANES];
        let mut band_gain = [0_u64; LANES];
        let mut absolute = 0u64;
        let mut worst_row = 0u64;
        let mut observed_error = 0u64;
        let mut samples = 0usize;
        // Give each spatial tile a vote against the stationary model. A small
        // moving widget can fit perfectly inside an otherwise stationary viewport.
        // Also check the votes at cross-axis texture: motion of uniform stripes
        // alone cannot explain a stationary, textured surround.
        let mut gain = [0_i64; LANES * LANES];
        let mut cross_gain = [0_i64; LANES * LANES];
        let mut occupied = 0u64;
        let overlap = overlap(height, offset);
        // Anchor the sampling grid in the incoming viewport, independently of the shift.
        for (row, patches) in rows
            .iter_mut()
            .enumerate()
            .take(overlap.end.div_ceil(stride))
            .skip(overlap.start / stride)
        {
            let start = row * stride;
            let y = if NATIVE {
                let span = stride.min(height - start);
                start + (((row as u32).wrapping_mul(0x9e37_79b9) >> 16) as usize % span)
            } else {
                start
            };
            if !overlap.contains(&y) {
                continue;
            }
            let r = source(y, offset);
            if GRADIENT && (y == 0 || r == 0) {
                continue;
            }
            let band = (y - overlap.start) * LANES / overlap.len();
            let mut row_error = 0u64;
            for (lane, patch) in patches.iter_mut().enumerate() {
                let xs = lane * count / LANES..(lane + 1) * count / LANES;
                let tile = y * LANES / height * LANES + lane;
                let mut aligned = 0u32;
                let mut target_change = 0u32;
                let mut source_change = 0u32;
                let mut last = None;
                for x in xs.clone() {
                    let x = if NATIVE {
                        verification_column(y, x, width)
                    } else {
                        x
                    };
                    let next = self.incoming.signal::<GRADIENT>(y, x, NATIVE);
                    let moved = distance::<GRADIENT>(
                        self.reference.signal::<GRADIENT>(r, x, NATIVE),
                        next,
                        bias,
                    );
                    let stationary = distance::<GRADIENT>(
                        self.reference.signal::<GRADIENT>(y, x, NATIVE),
                        next,
                        bias,
                    );
                    let advantage = i64::from(stationary) - i64::from(moved);
                    gain[tile] += advantage;
                    if last.is_some_and(|v| v != next) {
                        cross_gain[tile] += advantage;
                    }
                    last = Some(next);
                    aligned += moved;
                    target_change +=
                        distance::<GRADIENT>(self.raw.signal::<GRADIENT>(y, x, NATIVE), next, bias);
                    source_change += distance::<GRADIENT>(
                        self.raw.signal::<GRADIENT>(r, x, NATIVE),
                        self.incoming.signal::<GRADIENT>(r, x, NATIVE),
                        bias,
                    );
                }
                // Aggregate a patch before limiting its influence. Even pixels that
                // keep their old colour can contradict a displacement of nearby text.
                let change = source_change.min(target_change);
                *patch = Residual { aligned, change };
                row_error += u64::from(aligned);
                if change != 0 {
                    samples += xs.len();
                    observed_error += u64::from(aligned);
                    let loss = u64::from(aligned.min(change));
                    stripe_residual[band] += loss;
                    band_gain[band] += u64::from(change) - loss;
                    if aligned < change {
                        occupied |= 1 << tile;
                    }
                }
            }
            absolute += row_error;
            worst_row = worst_row.max(row_error);
        }
        let residual: u64 = stripe_residual.iter().sum();
        let explained: u64 = band_gain.iter().sum();
        let budget = residual + explained;
        // A few stray pixels should not count as another band of scroll evidence.
        let minimum_band_gain = explained.div_ceil(4 * LANES as u64).max(1);
        let supported_bands = band_gain
            .iter()
            .filter(|&&gain| gain >= minimum_band_gain)
            .count();
        Fit {
            overlap: OverlapResiduals::new(rows, stripe_residual, budget, absolute, worst_row),
            error: if samples == 0 {
                1.0
            } else {
                observed_error as f32 / (765.0 * samples as f32)
            },
            dominant: gain.iter().map(|v| v.signum()).sum::<i64>() > 0
                && cross_gain.iter().map(|v| v.signum()).sum::<i64>() >= 0,
            blocks: occupied.count_ones(),
            supported_bands,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::PixelFormat;

    #[test]
    fn only_voting_rows_contribute_the_final_six_pixel_patch() {
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            for (extent, expected) in [(37, Some(31)), (68, Some(62)), (69, None), (70, Some(64))] {
                let frame = |bright_row| {
                    let (width, height) = match axis {
                        StitchAxis::Vertical => (7, extent),
                        StitchAxis::Horizontal => (extent, 7),
                    };
                    let pixels = (0..height)
                        .flat_map(|y| {
                            (0..width).map(move |x| {
                                let along = if axis == StitchAxis::Vertical { y } else { x };
                                if along == bright_row { 255 } else { 0 }
                            })
                        })
                        .collect();
                    Frame::new(width, height, PixelFormat::Gray8, pixels).unwrap()
                };
                let reference = frame(5);
                let incoming = frame(extent - 1);
                let offsets = pixel_proposals(
                    &View::new(&reference, axis),
                    &View::new(&incoming, axis),
                    extent as i32 - 1,
                );
                assert_eq!(
                    offsets,
                    expected.into_iter().collect::<Vec<_>>(),
                    "{axis:?}, {extent}"
                );
            }
        }
    }
}
