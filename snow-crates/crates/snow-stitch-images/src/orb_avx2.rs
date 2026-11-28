use super::{
    CIRCLE, Image, Keypoint, PATCH_SIZE, ResizeBlock, ResizeCoefficient, cv_round, fast_atan2, umax,
};
#[cfg(target_arch = "x86")]
use std::arch::x86::*;
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::*;
use std::sync::OnceLock;

#[target_feature(enable = "avx2")]
pub(super) unsafe fn resize_blocks(
    blocks: &[ResizeBlock],
    vertical: ResizeCoefficient,
    top: &[u8],
    bottom: &[u8],
    output: &mut [u8],
) -> usize {
    let weight_y = _mm256_set1_epi32(i32::from(vertical.second_weight));
    let rounding = _mm256_set1_epi32(1 << 15);
    for (block, dst) in blocks.iter().zip(output.chunks_exact_mut(8)) {
        // The plan places both samples for all eight lanes inside a single row load.
        unsafe {
            let first = _mm_loadl_epi64(block.first.as_ptr().cast());
            let second = _mm_loadl_epi64(block.second.as_ptr().cast());
            let weight_x = _mm_loadu_si128(block.weights.as_ptr().cast());
            let horizontal = |row: &[u8]| {
                let bytes = _mm_loadu_si128(row.as_ptr().add(block.start).cast());
                let a = _mm_cvtepu8_epi16(_mm_shuffle_epi8(bytes, first));
                let b = _mm_cvtepu8_epi16(_mm_shuffle_epi8(bytes, second));
                // Interpolate in 8.8 fixed point. The result fits in u16.
                _mm256_cvtepu16_epi32(_mm_add_epi16(
                    _mm_slli_epi16::<8>(a),
                    _mm_mullo_epi16(_mm_sub_epi16(b, a), weight_x),
                ))
            };
            let a = horizontal(top);
            let b = horizontal(bottom);
            let blend = _mm256_add_epi32(
                _mm256_slli_epi32::<8>(a),
                _mm256_mullo_epi32(_mm256_sub_epi32(b, a), weight_y),
            );
            let rounded = _mm256_srli_epi32::<16>(_mm256_add_epi32(blend, rounding));
            let words = _mm_packus_epi32(
                _mm256_castsi256_si128(rounded),
                _mm256_extracti128_si256::<1>(rounded),
            );
            _mm_storel_epi64(dst.as_mut_ptr().cast(), _mm_packus_epi16(words, words));
        }
    }
    blocks.len() * 8
}

#[target_feature(enable = "avx2")]
unsafe fn fast_responses(center: __m256i, samples: &[__m256i; 16], threshold: __m256i) -> __m256i {
    // Adjacent nine-sample arcs share eight pixels. Reduce those eight in
    // pairs and quads, then choose the better of their two possible endpoints.
    let pair_min: [_; 8] =
        std::array::from_fn(|i| _mm256_min_epu8(samples[2 * i + 1], samples[(2 * i + 2) % 16]));
    let pair_max: [_; 8] =
        std::array::from_fn(|i| _mm256_max_epu8(samples[2 * i + 1], samples[(2 * i + 2) % 16]));
    let quad_min: [_; 8] =
        std::array::from_fn(|i| _mm256_min_epu8(pair_min[i], pair_min[(i + 1) % 8]));
    let quad_max: [_; 8] =
        std::array::from_fn(|i| _mm256_max_epu8(pair_max[i], pair_max[(i + 1) % 8]));
    let mut brightest_min = _mm256_setzero_si256();
    let mut darkest_max = _mm256_set1_epi8(-1);
    for i in 0..8 {
        let a = samples[2 * i];
        let b = samples[(2 * i + 9) % 16];
        let low = _mm256_min_epu8(quad_min[i], quad_min[(i + 2) % 8]);
        let high = _mm256_max_epu8(quad_max[i], quad_max[(i + 2) % 8]);
        brightest_min = _mm256_max_epu8(brightest_min, _mm256_min_epu8(low, _mm256_max_epu8(a, b)));
        darkest_max = _mm256_min_epu8(darkest_max, _mm256_max_epu8(high, _mm256_min_epu8(a, b)));
    }
    let strength = _mm256_max_epu8(
        _mm256_subs_epu8(brightest_min, center),
        _mm256_subs_epu8(center, darkest_max),
    );
    let rejected = _mm256_cmpeq_epi8(
        _mm256_subs_epu8(strength, threshold),
        _mm256_setzero_si256(),
    );
    _mm256_andnot_si256(rejected, _mm256_subs_epu8(strength, _mm256_set1_epi8(1)))
}

#[target_feature(enable = "avx2")]
pub(super) unsafe fn score_fast_row(
    width: u32,
    pixels: &[u8],
    row: &mut [u8],
    y: u32,
    threshold: i16,
    border: u32,
) {
    let stride = width as usize;
    let mut x = border as usize;
    let end = stride - border as usize;
    let limit = _mm256_set1_epi8(threshold as i8);
    while x + 32 <= end {
        // The three-pixel border covers the circle around both end lanes.
        unsafe {
            let src = pixels.as_ptr().add(y as usize * stride + x);
            let load = |p: *const u8| _mm256_loadu_si256(p.cast());
            let center = load(src);
            let north = load(src.sub(3 * stride));
            let south = load(src.add(3 * stride));
            let east = load(src.add(3));
            let west = load(src.sub(3));
            // A nine-pixel arc contains a member of each opposing pair.
            let dark = _mm256_max_epu8(_mm256_min_epu8(north, south), _mm256_min_epu8(east, west));
            let bright =
                _mm256_min_epu8(_mm256_max_epu8(north, south), _mm256_max_epu8(east, west));
            let contrast = _mm256_max_epu8(
                _mm256_subs_epu8(center, dark),
                _mm256_subs_epu8(bright, center),
            );
            let passes = _mm256_subs_epu8(contrast, limit);
            if _mm256_testz_si256(passes, passes) == 0 {
                let samples = CIRCLE
                    .map(|(dx, dy)| load(src.offset(dy as isize * stride as isize + dx as isize)));
                _mm256_storeu_si256(
                    row.as_mut_ptr().add(x).cast(),
                    fast_responses(center, &samples, limit),
                );
            }
        }
        x += 32;
    }
    super::score_fast_row_scalar(width, pixels, row, y, threshold, x as u32..end as u32);
}

pub(super) struct MomentWeights {
    x: [[i8; 32]; 16],
    y: [[i8; 32]; 16],
}

pub(super) fn moment_weights() -> &'static MomentWeights {
    static WEIGHTS: OnceLock<MomentWeights> = OnceLock::new();
    WEIGHTS.get_or_init(|| {
        const { assert!(PATCH_SIZE == 31 && super::EDGE_THRESHOLD >= 16) };
        let mut weights = MomentWeights {
            x: [[0; 32]; 16],
            y: [[0; 32]; 16],
        };
        for (dy, &radius) in umax().iter().take(16).enumerate() {
            for lane in 15 - radius..=15 + radius {
                weights.x[dy][lane] = lane as i8 - 15;
                weights.y[dy][lane] = dy as i8;
            }
        }
        weights
    })
}

#[target_feature(enable = "avx2")]
pub(super) unsafe fn orient(image: &Image, point: &mut Keypoint, weights: &MomentWeights) {
    let x = cv_round(point.x) as usize;
    let y = cv_round(point.y) as usize;
    // The last byte in each load has zero weight and lies inside the FAST border.
    unsafe {
        let start = image.pixels.as_ptr().add(y * image.width + x - 15);
        let load = |p: *const u8| _mm256_loadu_si256(p.cast());
        let ones = _mm256_set1_epi16(1);
        let mut mx = _mm256_madd_epi16(
            _mm256_maddubs_epi16(load(start), load(weights.x[0].as_ptr().cast())),
            ones,
        );
        let mut my = _mm256_setzero_si256();
        for dy in 1..16 {
            let upper = load(start.sub(dy * image.width));
            let lower = load(start.add(dy * image.width));
            let wx = load(weights.x[dy].as_ptr().cast());
            let wy = load(weights.y[dy].as_ptr().cast());
            // Each pair is at most 2 * 15 * 255; combining two rows still fits i16.
            let sum = _mm256_add_epi16(
                _mm256_maddubs_epi16(lower, wx),
                _mm256_maddubs_epi16(upper, wx),
            );
            let diff = _mm256_sub_epi16(
                _mm256_maddubs_epi16(lower, wy),
                _mm256_maddubs_epi16(upper, wy),
            );
            mx = _mm256_add_epi32(mx, _mm256_madd_epi16(sum, ones));
            my = _mm256_add_epi32(my, _mm256_madd_epi16(diff, ones));
        }
        let sum = |v| {
            let mut lanes = [0_i32; 8];
            _mm256_storeu_si256(lanes.as_mut_ptr().cast(), v);
            lanes.into_iter().sum::<i32>()
        };
        point.angle = fast_atan2(sum(my) as f32, sum(mx) as f32);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn avx2_orientation_matches_scalar_at_borders_and_for_extreme_contrasts() {
        if !std::arch::is_x86_feature_detected!("avx2") {
            return;
        }
        let (width, height) = (83, 77);
        for pattern in 0..5 {
            let pixels = (0..width * height)
                .map(|i| match pattern {
                    0 => 255,
                    1 => 255 * u8::from(i % width < width / 2),
                    2 => 255 * u8::from(i / width < height / 2),
                    3 => (i * 3 / 4) as u8,
                    _ => (i * 73 + i / 9 * 53) as u8,
                })
                .collect();
            let image = Image::new(width, height, pixels);
            for (x, y) in [(15, 15), (width - 17, height - 16), (43, 38)] {
                let mut expected = Keypoint {
                    x: x as f32,
                    y: y as f32,
                    response: 0.0,
                    angle: -1.0,
                    octave: 0,
                };
                let mut actual = expected;
                super::super::orient(&image, &mut expected, &umax());
                unsafe { orient(&image, &mut actual, moment_weights()) };
                assert_eq!(actual.angle.to_bits(), expected.angle.to_bits());
            }
        }
    }
}
