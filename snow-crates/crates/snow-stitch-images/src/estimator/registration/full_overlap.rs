use std::collections::BTreeMap;

use super::{Color, LANES, OverlapResiduals, Pair, Residual, View, distance, overlap, source};

type LaneSums = [u32; LANES];
pub(super) type ChangesByBias = BTreeMap<[i16; 3], Vec<LaneSums>>;

// Visit the native buffer in physical row order on either scroll axis, while accumulating
// the same eight cross-axis patches used by the sampled comparison.
fn lane_sums(
    reference: &View,
    incoming: &View,
    offset: i32,
    difference: impl Fn(Color, Color) -> u32,
) -> Vec<LaneSums> {
    let mut sums = vec![[0; LANES]; incoming.height];
    let included = overlap(incoming.height, offset);
    let rows = &mut sums[included.clone()];
    let first_source = source(included.start, offset);
    let columns = |lane| lane * incoming.width / LANES..(lane + 1) * incoming.width / LANES;
    let at = |row, x| {
        difference(
            reference.native(first_source + row, x),
            incoming.native(included.start + row, x),
        )
    };
    if incoming.along_stride == incoming.channels {
        for lane in 0..LANES {
            for x in columns(lane) {
                for (row, patches) in rows.iter_mut().enumerate() {
                    patches[lane] += at(row, x);
                }
            }
        }
    } else {
        for (row, patches) in rows.iter_mut().enumerate() {
            for (lane, patch) in patches.iter_mut().enumerate() {
                *patch = columns(lane).map(|x| at(row, x)).sum();
            }
        }
    }
    sums
}

fn differences(reference: &View, incoming: &View, offset: i32, bias: [i16; 3]) -> Vec<LaneSums> {
    if bias == [0; 3] {
        lane_sums(reference, incoming, offset, |a, b| {
            u32::from(a[0].abs_diff(b[0]))
                + u32::from(a[1].abs_diff(b[1]))
                + u32::from(a[2].abs_diff(b[2]))
        })
    } else {
        lane_sums(reference, incoming, offset, |a, b| {
            distance::<false>(a.map(i16::from), b.map(i16::from), bias)
        })
    }
}

impl Pair<'_> {
    pub(super) fn full_overlap(
        &self,
        offset: i32,
        bias: [i16; 3],
        changes: &mut ChangesByBias,
    ) -> OverlapResiduals {
        // A raw-to-incoming patch sum depends only on its row and exposure bias.
        // A displacement selects two rows from this same table.
        let change_rows = changes
            .entry(bias)
            .or_insert_with(|| differences(&self.raw, &self.incoming, 0, bias));
        let aligned_rows = differences(&self.reference, &self.incoming, offset, bias);
        let included = overlap(self.incoming.height, offset);
        let mut rows = vec![[Residual::default(); LANES]; self.incoming.height];
        let mut stripe_residual = [0; LANES];
        let mut budget = 0;
        let mut absolute = 0;
        let mut worst_row = 0;
        for y in included.clone() {
            let band = (y - included.start) * LANES / included.len();
            let r = source(y, offset);
            let mut row_error = 0;
            for (lane, patch) in rows[y].iter_mut().enumerate() {
                let aligned = aligned_rows[y][lane];
                let change = change_rows[y][lane].min(change_rows[r][lane]);
                *patch = Residual { aligned, change };
                stripe_residual[band] += u64::from(aligned.min(change));
                budget += u64::from(change);
                row_error += u64::from(aligned);
            }
            absolute += row_error;
            worst_row = worst_row.max(row_error);
        }
        OverlapResiduals::new(rows, stripe_residual, budget, absolute, worst_row)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Frame, PixelFormat, StitchAxis, estimator::registration::FitGrid};

    #[test]
    fn native_residuals_match_a_verification_grid_covering_every_pixel() {
        for format in [PixelFormat::Gray8, PixelFormat::Rgb8, PixelFormat::Rgba8] {
            let make = |seed: usize| {
                let pixels = (0..31 * 23 * format.channels() as usize)
                    .map(|i| ((i * 37 + (i / 7) * seed) % 256) as u8)
                    .collect();
                Frame::new(31, 23, format, pixels).unwrap()
            };
            let [reference, raw, incoming] = [1, 3, 17].map(make);
            for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
                let pair = Pair {
                    reference: View::new(&reference, axis),
                    raw: View::new(&raw, axis),
                    incoming: View::new(&incoming, axis),
                };
                let mut changes = ChangesByBias::new();
                for offset in [-19, -1, 0, 8, 17] {
                    for bias in [[0; 3], [23, -13, 47], [-255, 255, 1]] {
                        assert_eq!(
                            pair.full_overlap(offset, bias, &mut changes),
                            pair.fit::<false>(offset, bias, FitGrid::Verification)
                                .overlap,
                            "{format:?}, {axis:?}, {offset}, {bias:?}"
                        );
                    }
                }
            }
        }
    }
}
