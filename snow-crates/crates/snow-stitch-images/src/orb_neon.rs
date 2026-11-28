//! ARM64 counterparts of the runtime-dispatched ORB kernels.
use super::*;
use std::arch::aarch64::*;

#[cfg(test)]
static EXECUTED: [std::sync::atomic::AtomicUsize; 4] =
    [const { std::sync::atomic::AtomicUsize::new(0) }; 4];
#[inline]
fn executed(backend: usize) {
    #[cfg(test)]
    EXECUTED[backend].fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    #[cfg(not(test))]
    let _ = backend;
}

#[target_feature(enable = "neon")]
unsafe fn bytes_to_f32(bytes: uint8x8_t) -> [float32x4_t; 2] {
    let words = vmovl_u8(bytes);
    [
        vcvtq_f32_u32(vmovl_u16(vget_low_u16(words))),
        vcvtq_f32_u32(vmovl_u16(vget_high_u16(words))),
    ]
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn grayscale_chunk(source: &[u8], output: &mut [u8], channels: usize) {
    unsafe {
        let mut x = 0;
        while x + 8 <= output.len() {
            let (r, g, b) = if channels == 3 {
                let pixels = vld3_u8(source.as_ptr().add(x * 3));
                (pixels.0, pixels.1, pixels.2)
            } else {
                let pixels = vld4_u8(source.as_ptr().add(x * 4));
                (pixels.0, pixels.1, pixels.2)
            };
            // Equal RGB components round back to the original byte for every
            // luminance. Document backgrounds and antialiased text often fill
            // an entire vector with such pixels.
            if vmaxv_u8(vorr_u8(veor_u8(r, g), veor_u8(g, b))) == 0 {
                vst1_u8(output.as_mut_ptr().add(x), r);
                x += 8;
                continue;
            }
            let r = bytes_to_f32(r);
            let g = bytes_to_f32(g);
            let b = bytes_to_f32(b);
            let convert = |half| {
                let mut value = vmulq_n_f32(r[half], 0.299);
                value = vaddq_f32(value, vmulq_n_f32(g[half], 0.587));
                value = vaddq_f32(value, vmulq_n_f32(b[half], 0.114));
                vmovn_u32(vcvtq_u32_f32(vrndaq_f32(value)))
            };
            vst1_u8(
                output.as_mut_ptr().add(x),
                vmovn_u16(vcombine_u16(convert(0), convert(1))),
            );
            x += 8;
        }
        if x > 0 {
            executed(0);
        }
        for (target, pixel) in output[x..]
            .iter_mut()
            .zip(source[x * channels..].chunks_exact(channels))
        {
            *target = grayscale_pixel(pixel);
        }
    }
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn grayscale(frame: &Frame) -> Image {
    let channels = frame.pixel_format().channels() as usize;
    if channels == 1 {
        return grayscale_scalar(frame);
    }
    let mut pixels = RasterBuffer::zeroed(frame.width() as usize * frame.height() as usize);
    pixels
        .par_chunks_mut(32 * 1024)
        .enumerate()
        .for_each(|(index, output)| {
            let start = index * 32 * 1024 * channels;
            unsafe {
                grayscale_chunk(
                    &frame.pixels()[start..start + output.len() * channels],
                    output,
                    channels,
                )
            };
        });
    Image::from_buffer(frame.width() as usize, frame.height() as usize, pixels)
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn resize_blocks(
    blocks: &[ResizeBlock],
    vertical: ResizeCoefficient,
    top: &[u8],
    bottom: &[u8],
    output: &mut [u8],
) -> usize {
    for (block, dst) in blocks.iter().zip(output.chunks_exact_mut(8)) {
        // The plan bounds the full load on both rows, including the last complete block.
        unsafe {
            let first = vld1_u8(block.first.as_ptr());
            let second = vld1_u8(block.second.as_ptr());
            let weights = vld1q_u16(block.weights.as_ptr());
            let horizontal = |row: &[u8]| {
                let bytes = vld1q_u8(row.as_ptr().add(block.start));
                let left = vmovl_u8(vqtbl1_u8(bytes, first));
                let right = vmovl_u8(vqtbl1_u8(bytes, second));
                // (256 - w) * left + w * right fits in 16 bits, including w = 256.
                vmlaq_u16(vshlq_n_u16::<8>(left), vsubq_u16(right, left), weights)
            };
            let a = horizontal(top);
            let b = horizontal(bottom);
            let blend = |a, b| {
                let sum = vmlal_n_u16(
                    vmull_n_u16(a, vertical.first_weight),
                    b,
                    vertical.second_weight,
                );
                vrshrn_n_u32::<16>(sum)
            };
            let lo = blend(vget_low_u16(a), vget_low_u16(b));
            let hi = blend(vget_high_u16(a), vget_high_u16(b));
            vst1_u8(dst.as_mut_ptr(), vmovn_u16(vcombine_u16(lo, hi)));
        }
    }
    blocks.len() * 8
}

pub(super) fn fast_polarity_masks(differences: &[i16; 16], threshold: i16) -> (u32, u32) {
    unsafe {
        let weights = vld1q_u16([1, 2, 4, 8, 16, 32, 64, 128].as_ptr());
        let first = vld1q_s16(differences.as_ptr());
        let second = vld1q_s16(differences.as_ptr().add(8));
        let mask = |a, b| u32::from(vaddvq_u16(vandq_u16(vcgtq_s16(a, b), weights)));
        let dark = vdupq_n_s16(threshold);
        let bright = vdupq_n_s16(-threshold);
        (
            mask(first, dark) | (mask(second, dark) << 8),
            mask(bright, first) | (mask(bright, second) << 8),
        )
    }
}

pub(super) fn strongest_fast_arc(differences: &[i16; 16]) -> i16 {
    unsafe {
        let first = vld1q_s16(differences.as_ptr());
        let second = vld1q_s16(differences.as_ptr().add(8));
        let mut minima = [first, second];
        let mut maxima = minima;
        macro_rules! reduce {
            ($shift:literal) => {{
                let [a, b] = minima;
                minima = [
                    vminq_s16(a, vextq_s16::<$shift>(a, b)),
                    vminq_s16(b, vextq_s16::<$shift>(b, a)),
                ];
                let [a, b] = maxima;
                maxima = [
                    vmaxq_s16(a, vextq_s16::<$shift>(a, b)),
                    vmaxq_s16(b, vextq_s16::<$shift>(b, a)),
                ];
            }};
        }
        reduce!(1);
        reduce!(2);
        reduce!(4);
        let dark = vmaxq_s16(vminq_s16(minima[0], second), vminq_s16(minima[1], first));
        let bright = vminq_s16(vmaxq_s16(maxima[0], second), vmaxq_s16(maxima[1], first));
        vmaxvq_s16(vmaxq_s16(dark, vnegq_s16(bright)))
    }
}

// Score sixteen centers with the same exact circular FAST-9 criterion as `score`.
// Unsigned min/max on the circle intensities avoids widening all sixteen lanes.
#[inline]
#[target_feature(enable = "neon")]
unsafe fn fast_responses(
    center: uint8x16_t,
    samples: &[uint8x16_t; 16],
    threshold: uint8x16_t,
) -> uint8x16_t {
    let pair_min: [_; 8] =
        std::array::from_fn(|i| vminq_u8(samples[i * 2 + 1], samples[(i * 2 + 2) % 16]));
    let pair_max: [_; 8] =
        std::array::from_fn(|i| vmaxq_u8(samples[i * 2 + 1], samples[(i * 2 + 2) % 16]));
    let quad_min: [_; 8] = std::array::from_fn(|i| vminq_u8(pair_min[i], pair_min[(i + 1) % 8]));
    let quad_max: [_; 8] = std::array::from_fn(|i| vmaxq_u8(pair_max[i], pair_max[(i + 1) % 8]));
    let mut lower = vdupq_n_u8(0);
    let mut upper = vdupq_n_u8(255);
    for i in 0..8 {
        // The arcs starting at 2i and 2i+1 share eight samples.
        // Combine their optional endpoints before reducing across arcs:
        // max(min(a, c), min(b, c)) = min(max(a, b), c).
        let a = samples[i * 2];
        let b = samples[(i * 2 + 9) % 16];
        let low = vminq_u8(quad_min[i], quad_min[(i + 2) % 8]);
        let high = vmaxq_u8(quad_max[i], quad_max[(i + 2) % 8]);
        lower = vmaxq_u8(lower, vminq_u8(low, vmaxq_u8(a, b)));
        upper = vminq_u8(upper, vmaxq_u8(high, vminq_u8(a, b)));
    }
    let strongest = vmaxq_u8(vqsubq_u8(center, upper), vqsubq_u8(lower, center));
    vandq_u8(
        vcgtq_u8(strongest, threshold),
        vqsubq_u8(strongest, vdupq_n_u8(1)),
    )
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn fast_scores(
    width: u32,
    height: u32,
    pixels: &[u8],
    threshold: i16,
    border: u32,
) -> RasterBuffer {
    let threshold_value = threshold as u8;
    let mut scores = RasterBuffer::zeroed(pixels.len());
    scores
        .par_chunks_mut(width as usize)
        .enumerate()
        .for_each(|(y, row)| {
            if !(border as usize..(height - border) as usize).contains(&y) {
                return;
            }
            unsafe {
                let w = width as usize;
                let mut x = border as usize;
                while x + 16 <= w - border as usize {
                    // Each lane scores one pixel. The border and loop bound keep every
                    // circle load (including the last lane) inside the source image.
                    let center_index = y * w + x;
                    let source = pixels.as_ptr().add(center_index);
                    let center = vld1q_u8(source);
                    let threshold = vdupq_n_u8(threshold as u8);
                    let mut samples = [vdupq_n_u8(0); 16];
                    // A nine-point arc intersects both opposite cardinal pairs.
                    // Min/max keeps the polarity of each pair while testing all lanes.
                    let north = vld1q_u8(source.sub(3 * w));
                    let south = vld1q_u8(source.add(3 * w));
                    let west = vld1q_u8(source.sub(3));
                    let east = vld1q_u8(source.add(3));
                    let darkest = vmaxq_u8(vminq_u8(north, south), vminq_u8(west, east));
                    let brightest = vminq_u8(vmaxq_u8(north, south), vmaxq_u8(west, east));
                    let contrast =
                        vmaxq_u8(vqsubq_u8(center, darkest), vqsubq_u8(brightest, center));
                    if vmaxvq_u8(contrast) > threshold_value {
                        samples[0] = south;
                        samples[4] = east;
                        samples[8] = north;
                        samples[12] = west;
                        for index in [1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15] {
                            let (dx, dy) = CIRCLE[index];
                            samples[index] =
                                vld1q_u8(source.offset(dy as isize * w as isize + dx as isize));
                        }
                        let response = fast_responses(center, &samples, threshold);
                        vst1q_u8(row.as_mut_ptr().add(x), response);
                    }
                    x += 16;
                }
                if x > border as usize {
                    executed(1);
                }
                for target in x as u32..width - border {
                    if may_pass_fast_9_16(width, pixels, target, y as u32, threshold) {
                        row[target as usize] =
                            score(circle_values(width, pixels, target, y as u32), threshold)
                                .unwrap_or(0);
                    }
                }
            }
        });
    scores
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn suppress_nonmax_row(
    width: u32,
    scores: &[u8],
    y: u32,
    border: u32,
    keypoints: &mut Vec<FastKeypoint>,
) {
    let mut x = border;
    while x + 16 <= width - border {
        // SAFETY: The caller supplies a full score raster and an interior row.
        // The loop bound leaves the right-hand neighbor of the last lane in bounds.
        let (responses, neighbors) = unsafe {
            let center = scores.as_ptr().add((y * width + x) as usize);
            let above = center.sub(width as usize);
            let below = center.add(width as usize);
            let mut neighbors = vmaxq_u8(vld1q_u8(center.sub(1)), vld1q_u8(center.add(1)));
            for row in [above, below] {
                neighbors = vmaxq_u8(neighbors, vld1q_u8(row.sub(1)));
                neighbors = vmaxq_u8(neighbors, vld1q_u8(row));
                neighbors = vmaxq_u8(neighbors, vld1q_u8(row.add(1)));
            }
            (vld1q_u8(center), neighbors)
        };
        // A zero response and any tied neighbor must both be suppressed.
        let keep = vcgtq_u8(responses, neighbors);
        if vmaxvq_u8(keep) != 0 {
            let mut lanes = [0; 16];
            unsafe { vst1q_u8(lanes.as_mut_ptr(), vandq_u8(keep, responses)) };
            for (lane, response) in lanes.into_iter().enumerate() {
                if response != 0 {
                    keypoints.push(FastKeypoint {
                        x: x + lane as u32,
                        y,
                        response,
                    });
                }
            }
        }
        x += 16;
    }
    suppress_nonmax_row_scalar(width, scores, y, x..width - border, keypoints);
}

#[target_feature(enable = "neon")]
unsafe fn load_four_bytes(source: *const u8) -> float32x4_t {
    // A four-byte load avoids reading past the final reflected-border interior.
    unsafe {
        let packed = std::ptr::read_unaligned(source.cast::<u32>());
        let bytes = vcreate_u8(u64::from(packed));
        vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(bytes))))
    }
}

// Signed moment weights for the two 16-byte loads spanning the 31-pixel disc.
// The center belongs to the first half. It has zero weight in the second.
pub(super) struct MomentWeights {
    x: [[[i8; 16]; 2]; 16],
    y: [[[i8; 16]; 2]; 16],
}

impl MomentWeights {
    pub(super) fn new(radii: &[usize]) -> Self {
        const { assert!(PATCH_SIZE == 31) };
        Self {
            x: std::array::from_fn(|y| {
                std::array::from_fn(|half| {
                    std::array::from_fn(|i| {
                        let x = (half * 15 + i) as i8 - 15;
                        if usize::from(x.unsigned_abs()) <= radii[y] {
                            x
                        } else {
                            0
                        }
                    })
                })
            }),
            y: std::array::from_fn(|y| {
                std::array::from_fn(|half| {
                    std::array::from_fn(|i| {
                        let x = (half * 15 + i) as i8 - 15;
                        if (half == 0 || i > 0) && usize::from(x.unsigned_abs()) <= radii[y] {
                            y as i8
                        } else {
                            0
                        }
                    })
                })
            }),
        }
    }
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn orient(image: &Image, point: &mut Keypoint, weights: &MomentWeights) {
    let x = cv_round(point.x) as usize;
    let y = cv_round(point.y) as usize;
    // The detector's border contains the whole disc, including its enclosing square.
    let center = y * image.width + x;
    unsafe {
        let mut mx = vdupq_n_s32(0);
        let mut my = vdupq_n_s32(0);
        let dot = |acc, a, b| {
            vpadalq_s16(
                vpadalq_s16(acc, vmull_s8(vget_low_s8(a), vget_low_s8(b))),
                vmull_high_s8(a, b),
            )
        };
        let load = |i| {
            vreinterpretq_s8_u8(veorq_u8(
                vld1q_u8(image.pixels.as_ptr().add(i)),
                vdupq_n_u8(128),
            ))
        };
        // Subtracting 128 from each intensity cancels in the symmetric moments.
        for half in 0..2 {
            let start = center - 15 + half * 15;
            mx = dot(mx, load(start), vld1q_s8(weights.x[0][half].as_ptr()));
            for dy in 1..16 {
                let lower = load(start + dy * image.width);
                let upper = load(start - dy * image.width);
                let wx = vld1q_s8(weights.x[dy][half].as_ptr());
                let wy = vld1q_s8(weights.y[dy][half].as_ptr());
                mx = dot(dot(mx, lower, wx), upper, wx);
                my = dot(dot(my, lower, wy), upper, vnegq_s8(wy));
            }
        }
        point.angle = fast_atan2(vaddvq_s32(my) as f32, vaddvq_s32(mx) as f32);
    }
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn blur(image: &Image) -> Image {
    blur_with_rows(
        image,
        |row, y| unsafe {
            if image.width <= 6 {
                horizontal_blur_row_scalar(&image.pixels, image.width, row, y);
                return;
            }
            for (x, target) in row.iter_mut().take(3).enumerate() {
                *target = horizontal_blur_reflected(&image.pixels, image.width, x, y);
            }
            let mut x = 3;
            if image.width >= 12 {
                let source = image.pixels.as_ptr().add(y * image.width);
                let mut first = load_four_bytes(source);
                let mut middle = load_four_bytes(source.add(4));
                // Consecutive four-pixel windows share eight input bytes.
                while x - 3 + 12 <= image.width {
                    let last = load_four_bytes(source.add(x - 3 + 8));
                    let taps = [
                        first,
                        vextq_f32::<1>(first, middle),
                        vextq_f32::<2>(first, middle),
                        vextq_f32::<3>(first, middle),
                        middle,
                        vextq_f32::<1>(middle, last),
                        vextq_f32::<2>(middle, last),
                    ];
                    let mut value = vmulq_n_f32(taps[0], BLUR_KERNEL[0]);
                    for tap in 1..7 {
                        value = vaddq_f32(value, vmulq_n_f32(taps[tap], BLUR_KERNEL[tap]));
                    }
                    vst1q_f32(row.as_mut_ptr().add(x), value);
                    x += 4;
                    first = middle;
                    middle = last;
                }
            }
            if x > 3 {
                executed(2);
            }
            for (x, target) in row.iter_mut().enumerate().skip(x) {
                *target = horizontal_blur_reflected(&image.pixels, image.width, x, y);
            }
        },
        |rows, output| unsafe {
            let rounded = |x| {
                let load = |row: usize| vld1q_f32(rows[row].as_ptr().add(x));
                let mut value = vmulq_n_f32(load(3), BLUR_KERNEL[3]);
                for offset in 1..4 {
                    value = vaddq_f32(
                        value,
                        vmulq_n_f32(
                            vaddq_f32(load(3 - offset), load(3 + offset)),
                            BLUR_KERNEL[3 + offset],
                        ),
                    );
                }
                // The scalar blur rounds ties to even before narrowing to a pixel.
                vqmovn_u32(vcvtnq_u32_f32(value))
            };
            let mut x = 0;
            while x + 8 <= output.len() {
                let pixels = vqmovn_u16(vcombine_u16(rounded(x), rounded(x + 4)));
                vst1_u8(output.as_mut_ptr().add(x), pixels);
                x += 8;
            }
            if x > 0 {
                executed(2);
            }
            for (x, target) in output.iter_mut().enumerate().skip(x) {
                *target = vertical_blur_pixel(&rows, x);
            }
        },
    )
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn descriptor(image: &Image, keypoint: Keypoint) -> [u8; 32] {
    const RADIANS_PER_DEGREE: f32 = (std::f64::consts::PI / 180.0) as f32;
    let scale = 1.0 / scale_for_level(keypoint.octave);
    let center_x = cv_round(keypoint.x * scale) as isize;
    let center_y = cv_round(keypoint.y * scale) as isize;
    let radians = keypoint.angle * RADIANS_PER_DEGREE;
    let cosine = radians.cos();
    let sine = radians.sin();
    let (pattern_x, pattern_y) = orb_pattern_xy();
    if image.pixels.len() > i32::MAX as usize {
        return descriptor_scalar(image, keypoint);
    }
    let center = vdupq_n_s32((center_y * image.width as isize + center_x) as i32);
    let pixels = image.pixels.as_slice();
    let mut bytes = [0; 32];
    unsafe {
        let weights = vld1_u8([1, 2, 4, 8, 16, 32, 64, 128].as_ptr());
        for (index, byte) in bytes.iter_mut().enumerate() {
            let mut samples = [0_u8; 16];
            for (group, dst) in samples.chunks_exact_mut(4).enumerate() {
                let start = 16 * index + group * 4;
                let x = vcvtq_f32_s32(vld1q_s32(pattern_x.as_ptr().add(start)));
                let y = vcvtq_f32_s32(vld1q_s32(pattern_y.as_ptr().add(start)));
                let rx = vcvtnq_s32_f32(vsubq_f32(vmulq_n_f32(x, cosine), vmulq_n_f32(y, sine)));
                let ry = vcvtnq_s32_f32(vaddq_f32(vmulq_n_f32(x, sine), vmulq_n_f32(y, cosine)));
                let at = vmlaq_n_s32(vaddq_s32(center, rx), ry, image.width as i32);
                let mut offsets = [0; 4];
                vst1q_s32(offsets.as_mut_ptr(), at);
                for (dst, offset) in dst.iter_mut().zip(offsets) {
                    *dst = pixels[offset as usize];
                }
            }
            let group = vld1q_u8(samples.as_ptr());
            let (low, high) = (vget_low_u8(group), vget_high_u8(group));
            let bits = vclt_u8(vuzp1_u8(low, high), vuzp2_u8(low, high));
            *byte = vaddv_u8(vand_u8(bits, weights));
        }
    }
    executed(3);
    bytes
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn orientation_matches_scalar_at_patch_edges_and_for_uniform_intensities() {
        let radii = umax();
        let weights = MomentWeights::new(&radii);
        for seed in [0, 1, 127, 255] {
            let image = Image::new(
                65,
                47,
                (0..65 * 47)
                    .map(|i| match seed {
                        0 | 255 => seed as u8,
                        _ => ((i * seed + i / 7) ^ (i >> 1)) as u8,
                    })
                    .collect(),
            );
            for y in 15..image.height - 15 {
                for x in [15, 17, image.width - 16] {
                    let mut expected = Keypoint {
                        x: x as f32,
                        y: y as f32,
                        octave: 0,
                        response: 0.0,
                        angle: -1.0,
                    };
                    let mut actual = expected;
                    super::super::orient(&image, &mut expected, &radii);
                    unsafe { super::orient(&image, &mut actual, &weights) };
                    assert_eq!(actual.angle, expected.angle);
                }
            }
        }
    }

    #[test]
    fn neutral_and_mixed_vectors_preserve_grayscale_for_every_intensity() {
        for channels in [3, 4] {
            let mut pixels = Vec::new();
            for value in 0..=255u8 {
                for lane in 0..16 {
                    let blue = if lane == 15 {
                        value.wrapping_add(73)
                    } else {
                        value
                    };
                    pixels.extend_from_slice(&[value, value, blue]);
                    if channels == 4 {
                        pixels.push(255 - value);
                    }
                }
            }
            let expected: Vec<_> = pixels.chunks_exact(channels).map(grayscale_pixel).collect();
            let mut gray = vec![0; expected.len()];
            unsafe { grayscale_chunk(&pixels, &mut gray, channels) };
            assert_eq!(gray, expected);
        }
    }

    #[test]
    fn cardinal_prefilter_preserves_every_nine_pixel_arc() {
        let (width, height) = (40_u32, 25_u32);
        let y = 12_i32;
        for x in [18_i32, 19] {
            // Adjacent lanes in different SIMD blocks.
            for bright_or_dark in [192, 64] {
                for start in 0..CIRCLE.len() {
                    let mut pixels = vec![128_u8; (width * height) as usize];
                    for i in 0..9 {
                        let (dx, dy) = CIRCLE[(start + i) % CIRCLE.len()];
                        pixels[((y + dy) * width as i32 + x + dx) as usize] = bright_or_dark;
                    }
                    for threshold in [20, 63, 64] {
                        let scores = unsafe { fast_scores(width, height, &pixels, threshold, 3) };
                        assert_eq!(
                            scores[(y * width as i32 + x) as usize],
                            if threshold < 64 { 63 } else { 0 },
                            "arc {start}, sample {bright_or_dark}, threshold {threshold}"
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn neon_orb_kernels_execute_and_match_scalar_for_formats_tails_arcs_and_angles() {
        assert!(std::arch::is_aarch64_feature_detected!("neon"));
        let before = EXECUTED
            .each_ref()
            .map(|n| n.load(std::sync::atomic::Ordering::Relaxed));
        for channels in [3, 4] {
            for len in 0..67 {
                let storage: Vec<u8> = (0..len * channels + 1)
                    .map(|i| (i * 73 + i / 7 * 19 + 11) as u8)
                    .collect();
                let source = &storage[1..];
                let expected: Vec<u8> =
                    source.chunks_exact(channels).map(grayscale_pixel).collect();
                let mut output = vec![0; len + 1];
                unsafe { grayscale_chunk(source, &mut output[1..], channels) };
                assert_eq!(&output[1..], expected);
                super::super::grayscale_chunk(source, &mut output[1..], channels);
                assert_eq!(&output[1..], expected);
            }
        }
        for format in [PixelFormat::Gray8, PixelFormat::Rgb8, PixelFormat::Rgba8] {
            let frame = Frame::new(
                337,
                101,
                format,
                (0..337 * 101 * format.channels() as usize)
                    .map(|i| (i * 73 + i / 5) as u8)
                    .collect(),
            )
            .unwrap();
            assert_eq!(unsafe { grayscale(&frame) }, grayscale_scalar(&frame));
            assert_eq!(super::super::grayscale(&frame), grayscale_scalar(&frame));
        }
        let mut state = 17_u32;
        for _ in 0..4096 {
            let differences = std::array::from_fn(|_| {
                state = state.wrapping_mul(1664525).wrapping_add(1013904223);
                (state % 511) as i16 - 255
            });
            assert_eq!(
                strongest_fast_arc(&differences),
                strongest_fast_arc_scalar(&differences)
            );
            for threshold in [0, 1, 20, 127, 254, 255] {
                let expected = differences
                    .iter()
                    .enumerate()
                    .fold((0, 0), |(a, b), (i, d)| {
                        (
                            a | (u32::from(*d > threshold) << i),
                            b | (u32::from(-*d > threshold) << i),
                        )
                    });
                assert_eq!(fast_polarity_masks(&differences, threshold), expected);
            }
        }
        for start in 0..16 {
            for threshold in [0_i16, 20, 254] {
                let mut differences = [0; 16];
                for offset in 0..9 {
                    differences[(start + offset) % 16] = threshold + 1;
                }
                assert_eq!(score(differences, threshold), Some(threshold as u8));
            }
        }
        for width in [7, 8, 13, 14, 15, 16, 17, 22, 31, 32, 33, 65, 127] {
            let height = 19;
            let pixels: Vec<u8> = (0..width * height)
                .map(|i| (i * 73 + i / 11 * 29) as u8)
                .collect();
            for threshold in [0, 20, 254, 255] {
                assert_eq!(
                    unsafe { fast_scores(width, height, &pixels, threshold, 3) },
                    fast_scores_scalar(width, height, &pixels, threshold, 3)
                );
            }
        }
        for width in 1..42 {
            for height in [1, 2, 3, 7, 8, 17] {
                let image = Image::new(
                    width,
                    height,
                    (0..width * height)
                        .map(|i| (i * 73 + i / 11 * 29) as u8)
                        .collect(),
                );
                assert_eq!(
                    unsafe { blur(&image) },
                    blur_scalar(&image),
                    "blur {width}x{height}"
                );
            }
        }
        let image = Image::new(
            128,
            112,
            (0..128 * 112)
                .map(|i| (i * 73 + i / 11 * 29) as u8)
                .collect(),
        );
        for octave in [0, 1, 3, 7] {
            let scale = scale_for_level(octave);
            for (x, y) in [(31.0, 31.0), (63.0, 55.0), (96.0, 80.0)] {
                for angle in [
                    0.0, 0.5, 17.25, 44.999, 45.0, 89.5, 137.0, 179.999, 225.0, 315.75, 359.5,
                ] {
                    let keypoint = Keypoint {
                        x: x * scale,
                        y: y * scale,
                        response: 1.0,
                        angle,
                        octave,
                    };
                    assert_eq!(
                        unsafe { descriptor(&image, keypoint) },
                        descriptor_scalar(&image, keypoint)
                    );
                }
            }
        }
        for (index, n) in EXECUTED.iter().enumerate() {
            assert!(
                n.load(std::sync::atomic::Ordering::Relaxed) > before[index],
                "NEON backend {index} executed no vector blocks"
            );
        }
    }
    #[test]
    fn neon_fast_responses_preserve_every_arc_mask_and_threshold_ties() {
        for strength in [20_u8, 21, 255] {
            for dark in [false, true] {
                let center = if dark { 255 } else { 0 };
                let active = if dark { 255 - strength } else { strength };
                for first_mask in (0_u32..=u16::MAX.into()).step_by(16) {
                    let samples = std::array::from_fn(|bit| {
                        let lanes: [u8; 16] = std::array::from_fn(|lane| {
                            if (first_mask + lane as u32) & (1 << bit) != 0 {
                                active
                            } else {
                                center
                            }
                        });
                        unsafe { vld1q_u8(lanes.as_ptr()) }
                    });
                    let mut actual = [0; 16];
                    unsafe {
                        vst1q_u8(
                            actual.as_mut_ptr(),
                            fast_responses(vdupq_n_u8(center), &samples, vdupq_n_u8(20)),
                        );
                    }
                    for (lane, response) in actual.into_iter().enumerate() {
                        let mask = first_mask + lane as u32;
                        let has_arc = (0..16).any(|start| {
                            (0..9).all(|offset| mask & (1 << ((start + offset) % 16)) != 0)
                        });
                        let expected = if has_arc && strength > 20 {
                            strength - 1
                        } else {
                            0
                        };
                        assert_eq!(
                            response, expected,
                            "mask {mask:#06x}, dark {dark}, strength {strength}"
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn neon_fast_score_maps_preserve_borders_unaligned_rows_and_tails() {
        for width in (7..=40).chain([61, 62, 63, 64, 65, 66, 77, 96, 127]) {
            for border in [3, 30, 31] {
                if width <= 2 * border {
                    continue;
                }
                let height = 2 * border + 17;
                for seed in 0..3_u32 {
                    let mut state = seed;
                    let storage: Vec<u8> = (0..width * height + 1)
                        .map(|i| {
                            state = state.wrapping_mul(1664525).wrapping_add(1013904223);
                            match seed {
                                0 => (state >> 24) as u8,
                                1 => ((i / width / 4 * 37) ^ (i % width / 4 * 71)) as u8,
                                _ => {
                                    if state & 0x8000_0000 == 0 {
                                        0
                                    } else {
                                        255
                                    }
                                }
                            }
                        })
                        .collect();
                    for threshold in [0, 1, 20, 127, 254, 255] {
                        let pixels = &storage[1..];
                        assert_eq!(
                            unsafe { fast_scores(width, height, pixels, threshold, border) },
                            fast_scores_scalar(width, height, pixels, threshold, border),
                            "{width}x{height}, border {border}, seed {seed}, threshold {threshold}"
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn neon_nonmax_suppression_preserves_ties_borders_tails_and_order() {
        for border in [3, 31] {
            for width in 2 * border + 1..=2 * border + 65 {
                let height = border * 2 + 5;
                for levels in [1, 2, 4, 256] {
                    let mut state = 17_u32;
                    let scores: Vec<u8> = (0..width * height)
                        .map(|_| {
                            state = state.wrapping_mul(1664525).wrapping_add(1013904223);
                            ((state >> 24) % levels) as u8
                        })
                        .collect();
                    let mut actual = Vec::new();
                    let mut expected = Vec::new();
                    for y in border..height - border {
                        unsafe { suppress_nonmax_row(width, &scores, y, border, &mut actual) };
                        suppress_nonmax_row_scalar(
                            width,
                            &scores,
                            y,
                            border..width - border,
                            &mut expected,
                        );
                    }
                    assert_eq!(
                        actual, expected,
                        "width {width}, border {border}, levels {levels}"
                    );
                }
            }
        }
    }

    #[test]
    #[ignore = "paired Release-only ARM64 kernel measurement"]
    fn neon_release_benchmark_orb() {
        use std::hint::black_box;
        let frame = Frame::new(
            1280,
            720,
            PixelFormat::Rgba8,
            (0..1280 * 720 * 4)
                .map(|i| (i * 73 + i / 11 * 29) as u8)
                .collect(),
        )
        .unwrap();
        crate::neon_benchmark::paired(
            "stitch_orb_grayscale",
            8,
            || grayscale_scalar(black_box(&frame)),
            || unsafe { grayscale(black_box(&frame)) },
        );
        let image = grayscale_scalar(&frame);
        crate::neon_benchmark::paired(
            "stitch_orb_blur",
            8,
            || blur_scalar(black_box(&image)),
            || unsafe { blur(black_box(&image)) },
        );
        let small = Image::new(512, 384, image.pixels[..512 * 384].to_vec());
        crate::neon_benchmark::paired(
            "stitch_orb_fast_scores",
            4,
            || fast_scores_scalar(512, 384, black_box(&small.pixels), 20, 3),
            || unsafe { fast_scores(512, 384, black_box(&small.pixels), 20, 3) },
        );
        let scores = fast_scores_scalar(512, 384, &small.pixels, 20, 3);
        crate::neon_benchmark::paired(
            "stitch_orb_nonmax_suppression",
            8,
            || {
                let mut points = Vec::new();
                for y in 3..381 {
                    suppress_nonmax_row_scalar(512, black_box(&scores), y, 3..509, &mut points);
                }
                points
            },
            || {
                let mut points = Vec::new();
                for y in 3..381 {
                    unsafe { suppress_nonmax_row(512, black_box(&scores), y, 3, &mut points) };
                }
                points
            },
        );
        let differences = std::array::from_fn(|i| (i as i16 * 73 % 511) - 255);
        crate::neon_benchmark::paired(
            "stitch_fast_arc",
            65536,
            || strongest_fast_arc_scalar(black_box(&differences)),
            || strongest_fast_arc(black_box(&differences)),
        );
        let keypoints: Vec<_> = (0..256)
            .map(|i| Keypoint {
                x: 40.0 + (i % 32) as f32 * 13.0,
                y: 40.0 + (i / 32) as f32 * 13.0,
                response: 1.0,
                angle: i as f32 * 1.337,
                octave: 0,
            })
            .collect();
        crate::neon_benchmark::paired(
            "stitch_orb_descriptors",
            8,
            || {
                keypoints
                    .iter()
                    .map(|keypoint| descriptor_scalar(black_box(&small), *keypoint))
                    .collect::<Vec<_>>()
            },
            || {
                keypoints
                    .iter()
                    .map(|keypoint| unsafe { descriptor(black_box(&small), *keypoint) })
                    .collect::<Vec<_>>()
            },
        );
    }
}
