//! Evaluate eight adjacent short area bins against a single pair of source vectors.
use super::AreaBin;
use std::arch::aarch64::*;

#[derive(Debug)]
pub(super) struct AreaBlock {
    start: usize,
    indices: [u8; 8],
    weights: [[u16; 8]; 4],
}

pub(super) fn plan(width: u32, bins: &[AreaBin], weights: &[u64]) -> Vec<AreaBlock> {
    if !(32..=8192).contains(&width) {
        return Vec::new();
    }
    bins.chunks_exact(8)
        .map_while(|group| {
            let start = group[0].first.min(width as usize - 32);
            if group
                .iter()
                .any(|bin| bin.weights.len() > 4 || bin.first + bin.weights.len() > start + 32)
            {
                return None;
            }
            Some(AreaBlock {
                start,
                indices: std::array::from_fn(|i| (group[i].first - start) as u8),
                weights: std::array::from_fn(|tap| {
                    std::array::from_fn(|i| {
                        weights[group[i].weights.clone()]
                            .get(tap)
                            .copied()
                            .unwrap_or(0) as u16
                    })
                }),
            })
        })
        .collect()
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn sample_row(
    blocks: &[AreaBlock],
    width: u32,
    row: &[u8],
    output: &mut [u8],
) -> usize {
    for (block, dst) in blocks.iter().zip(output.chunks_exact_mut(8)) {
        // The plan bounds both loads within this row, even at the right edge.
        unsafe {
            let source = row.as_ptr().add(block.start);
            let table = uint8x16x2_t(vld1q_u8(source), vld1q_u8(source.add(16)));
            let first = vld1_u8(block.indices.as_ptr());
            let mut low = vdupq_n_u32(width / 2);
            let mut high = low;
            for (tap, weights) in block.weights.iter().enumerate() {
                let pixels = vmovl_u8(vqtbl2_u8(table, vadd_u8(first, vdup_n_u8(tap as u8))));
                let weights = vld1q_u16(weights.as_ptr());
                low = vmlal_u16(low, vget_low_u16(pixels), vget_low_u16(weights));
                high = vmlal_high_u16(high, pixels, weights);
            }
            // At widths <= 8192 these sums are exactly representable in f32.
            // Noninteger quotients are >= 1/width from an integer, more than one
            // ULP across the entire pixel range, so truncation equals integer division.
            let normalize = |sum| {
                vmovn_u32(vcvtq_u32_f32(vdivq_f32(
                    vcvtq_f32_u32(sum),
                    vdupq_n_f32(width as f32),
                )))
            };
            vst1_u8(
                dst.as_mut_ptr(),
                vmovn_u16(vcombine_u16(normalize(low), normalize(high))),
            );
        }
    }
    blocks.len() * 8
}
