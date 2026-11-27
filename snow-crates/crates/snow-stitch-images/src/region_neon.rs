//! Image-similarity ARM64 kernels retain scalar accumulation and border rules.
use super::*;
use std::arch::aarch64::*;
#[cfg(test)]
static EXECUTED: [std::sync::atomic::AtomicUsize; 3] =
    [const { std::sync::atomic::AtomicUsize::new(0) }; 3];
#[inline]
fn executed(backend: usize) {
    #[cfg(test)]
    EXECUTED[backend].fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    #[cfg(not(test))]
    let _ = backend;
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn downsample(frame: &Frame, width: usize, height: usize) -> RasterArray<f32> {
    let mut pixels = RasterArray::<f32>::zeroed(width.saturating_mul(height));
    let source_width = frame.width() as usize;
    let source_height = frame.height() as usize;
    let channels = frame.pixel_format().channels() as usize;
    pixels
        .par_chunks_mut(width)
        .enumerate()
        .for_each(|(y, output)| unsafe {
            let y0 = y * DOWNSAMPLE as usize;
            let mut x = 0;
            if y0 + 4 <= source_height {
                while x + 4 <= width && (x + 4) * 4 <= source_width {
                    let mut sum = vdupq_n_f32(0.0);
                    for dy in 0..4 {
                        let source = frame
                            .pixels()
                            .as_ptr()
                            .add(((y0 + dy) * source_width + x * 4) * channels);
                        let expand = |bytes: uint8x16_t| {
                            let low = vmovl_u8(vget_low_u8(bytes));
                            let high = vmovl_u8(vget_high_u8(bytes));
                            [
                                vcvtq_f32_u32(vmovl_u16(vget_low_u16(low))),
                                vcvtq_f32_u32(vmovl_u16(vget_high_u16(low))),
                                vcvtq_f32_u32(vmovl_u16(vget_low_u16(high))),
                                vcvtq_f32_u32(vmovl_u16(vget_high_u16(high))),
                            ]
                        };
                        let samples = if channels == 1 {
                            expand(vld1q_u8(source))
                        } else {
                            let (red, green, blue) = if channels == 3 {
                                let pixels = vld3q_u8(source);
                                (pixels.0, pixels.1, pixels.2)
                            } else {
                                let pixels = vld4q_u8(source);
                                (pixels.0, pixels.1, pixels.2)
                            };
                            let red = expand(red);
                            let green = expand(green);
                            let blue = expand(blue);
                            std::array::from_fn(|i| {
                                let value = vaddq_f32(
                                    vmulq_n_f32(red[i], 0.299),
                                    vmulq_n_f32(green[i], 0.587),
                                );
                                vaddq_f32(value, vmulq_n_f32(blue[i], 0.114))
                            })
                        };
                        // Transpose four contiguous four-pixel blocks into four lanes,
                        // then add pixels in the original row-major order in each lane.
                        let a = vtrn1q_f32(samples[0], samples[1]);
                        let b = vtrn2q_f32(samples[0], samples[1]);
                        let c = vtrn1q_f32(samples[2], samples[3]);
                        let d = vtrn2q_f32(samples[2], samples[3]);
                        for sample in [
                            vcombine_f32(vget_low_f32(a), vget_low_f32(c)),
                            vcombine_f32(vget_low_f32(b), vget_low_f32(d)),
                            vcombine_f32(vget_high_f32(a), vget_high_f32(c)),
                            vcombine_f32(vget_high_f32(b), vget_high_f32(d)),
                        ] {
                            sum = vaddq_f32(sum, sample);
                        }
                    }
                    vst1q_f32(
                        output.as_mut_ptr().add(x),
                        vdivq_f32(sum, vdupq_n_f32(16.0)),
                    );
                    x += 4;
                }
            }
            if x > 0 {
                executed(0);
            }
            downsample_row_scalar(frame, y, output, x);
        });
    pixels
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn blur(source: &[f32], width: usize, height: usize) -> RasterArray<f32> {
    let mut output = RasterArray::<f32>::zeroed(source.len());
    if width == 0 {
        return output;
    }
    output
        .par_chunks_mut(width)
        .enumerate()
        .for_each(|(y, row)| unsafe {
            if width < 3 || y == 0 || y + 1 == height {
                blur_row_scalar(source, width, height, y, row, 0, width);
                return;
            }
            blur_row_scalar(source, width, height, y, row, 0, 1);
            let top = (y - 1) * width;
            let middle = y * width;
            let bottom = (y + 1) * width;
            let mut x = 1;
            while x + 4 < width {
                let load = |base: usize, offset: isize| {
                    vld1q_f32(source.as_ptr().add(base + x).offset(offset))
                };
                let mut sum = load(top, -1);
                sum = vaddq_f32(sum, vmulq_n_f32(load(top, 0), 2.0));
                sum = vaddq_f32(sum, load(top, 1));
                sum = vaddq_f32(sum, vmulq_n_f32(load(middle, -1), 2.0));
                sum = vaddq_f32(sum, vmulq_n_f32(load(middle, 0), 4.0));
                sum = vaddq_f32(sum, vmulq_n_f32(load(middle, 1), 2.0));
                sum = vaddq_f32(sum, load(bottom, -1));
                sum = vaddq_f32(sum, vmulq_n_f32(load(bottom, 0), 2.0));
                sum = vaddq_f32(sum, load(bottom, 1));
                vst1q_f32(row.as_mut_ptr().add(x), vdivq_f32(sum, vdupq_n_f32(16.0)));
                x += 4;
            }
            if x > 1 {
                executed(1);
            }
            blur_row_scalar(source, width, height, y, row, x, width);
        });
    output
}

#[target_feature(enable = "neon")]
pub(super) unsafe fn sobel(source: &[f32], width: usize, height: usize) -> RasterArray<f32> {
    let mut output = RasterArray::<f32>::zeroed(source.len());
    if width < 3 || height < 3 {
        return output;
    }
    output
        .par_chunks_mut(width)
        .enumerate()
        .for_each(|(y, row)| unsafe {
            if y == 0 || y + 1 == height {
                return;
            }
            let top = (y - 1) * width;
            let middle = y * width;
            let bottom = (y + 1) * width;
            let mut x = 1;
            while x + 4 < width {
                let load = |base: usize, offset: isize| {
                    vld1q_f32(source.as_ptr().add(base + x).offset(offset))
                };
                let tl = load(top, -1);
                let tc = load(top, 0);
                let tr = load(top, 1);
                let ml = load(middle, -1);
                let mr = load(middle, 1);
                let bl = load(bottom, -1);
                let bc = load(bottom, 0);
                let br = load(bottom, 1);
                let mut gx = vaddq_f32(vnegq_f32(tl), tr);
                gx = vsubq_f32(gx, vmulq_n_f32(ml, 2.0));
                gx = vaddq_f32(gx, vmulq_n_f32(mr, 2.0));
                gx = vsubq_f32(gx, bl);
                gx = vaddq_f32(gx, br);
                let mut gy = vsubq_f32(vnegq_f32(tl), vmulq_n_f32(tc, 2.0));
                gy = vsubq_f32(gy, tr);
                gy = vaddq_f32(gy, bl);
                gy = vaddq_f32(gy, vmulq_n_f32(bc, 2.0));
                gy = vaddq_f32(gy, br);
                // A flat axis makes hypot equal to the other magnitude. If either
                // axis already reaches the 255 clamp, the result is also known.
                let ax = vabsq_f32(gx);
                let ay = vabsq_f32(gy);
                let peak = vmaxq_f32(ax, ay);
                let easy = vorrq_u32(
                    vceqzq_f32(vminq_f32(ax, ay)),
                    vcgeq_f32(peak, vdupq_n_f32(255.0)),
                );
                if vminvq_u32(easy) != 0 {
                    vst1q_f32(row.as_mut_ptr().add(x), vminq_f32(peak, vdupq_n_f32(255.0)));
                    x += 4;
                    continue;
                }
                let mut xs = [0.0_f32; 4];
                let mut ys = [0.0_f32; 4];
                vst1q_f32(xs.as_mut_ptr(), gx);
                vst1q_f32(ys.as_mut_ptr(), gy);
                // hypot's scaling and rounding are part of the scalar contract.
                for lane in 0..4 {
                    row[x + lane] = xs[lane].hypot(ys[lane]).min(255.0);
                }
                x += 4;
            }
            if x > 1 {
                executed(2);
            }
            sobel_row_scalar(source, width, height, y, row, x);
        });
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn neon_sobel_matches_hypot_on_flat_single_axis_and_saturated_regions() {
        let (width, height) = (37_usize, 11_usize);
        for pattern in 0..4 {
            let source = (0..width * height)
                .map(|i| {
                    let x = i % width;
                    let y = i / width;
                    match pattern {
                        0 => 73.0,
                        1 => x as f32 * 0.3125,
                        2 => y as f32 * 1.375,
                        _ => {
                            if (x / 3 + y / 2).is_multiple_of(2) {
                                253.75
                            } else {
                                2.125
                            }
                        }
                    }
                })
                .collect::<Vec<_>>();
            let actual = unsafe { sobel(&source, width, height) };
            let expected = sobel_magnitude_scalar(&source, width, height);
            assert!(
                actual
                    .iter()
                    .zip(expected.iter())
                    .all(|(a, b)| a.to_bits() == b.to_bits())
            );
        }
    }

    #[test]
    fn neon_temporal_kernels_execute_and_match_scalar_bits_for_borders_and_tails() {
        assert!(std::arch::is_aarch64_feature_detected!("neon"));
        let before = EXECUTED
            .each_ref()
            .map(|n| n.load(std::sync::atomic::Ordering::Relaxed));
        for width in 1..38 {
            for height in [1, 2, 3, 5, 8, 17] {
                let storage: Vec<f32> = (0..width * height + 1)
                    .map(|i| [0.0, 255.0, 0.1, 127.5, 1e-20, 254.999, 73.25][i % 7])
                    .collect();
                let source = &storage[1..];
                let actual = unsafe { blur(source, width, height) };
                assert_eq!(
                    actual.iter().map(|v| v.to_bits()).collect::<Vec<_>>(),
                    blur_3x3_scalar(source, width, height)
                        .iter()
                        .map(|v| v.to_bits())
                        .collect::<Vec<_>>(),
                    "blur {width}x{height}"
                );
                let actual = unsafe { sobel(source, width, height) };
                assert_eq!(
                    actual.iter().map(|v| v.to_bits()).collect::<Vec<_>>(),
                    sobel_magnitude_scalar(source, width, height)
                        .iter()
                        .map(|v| v.to_bits())
                        .collect::<Vec<_>>(),
                    "sobel {width}x{height}"
                );
                for format in [PixelFormat::Gray8, PixelFormat::Rgb8, PixelFormat::Rgba8] {
                    let pixels = (0..width * height * format.channels() as usize)
                        .map(|i| (i * 73 + i / 11 * 29) as u8)
                        .collect();
                    let frame = Frame::new(width as u32, height as u32, format, pixels).unwrap();
                    let w = width.div_ceil(4);
                    let h = height.div_ceil(4);
                    assert_eq!(
                        &*unsafe { downsample(&frame, w, h) },
                        &*downsample_pixels_scalar(&frame, w, h)
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
    #[ignore = "paired Release-only ARM64 kernel measurement"]
    fn neon_release_benchmark_region() {
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
        let sample = |output: RasterArray<f32>| {
            let output = black_box(output);
            [
                output[0].to_bits(),
                output[output.len() / 2].to_bits(),
                output[output.len() - 1].to_bits(),
            ]
        };
        crate::neon_benchmark::paired(
            "stitch_region_downsample",
            8,
            || sample(downsample_pixels_scalar(black_box(&frame), 320, 180)),
            || sample(unsafe { downsample(black_box(&frame), 320, 180) }),
        );
        let pixels = downsample_pixels_scalar(&frame, 320, 180);
        crate::neon_benchmark::paired(
            "stitch_region_blur",
            16,
            || sample(blur_3x3_scalar(black_box(&pixels), 320, 180)),
            || sample(unsafe { blur(black_box(&pixels), 320, 180) }),
        );
        crate::neon_benchmark::paired(
            "stitch_region_sobel",
            16,
            || sample(sobel_magnitude_scalar(black_box(&pixels), 320, 180)),
            || sample(unsafe { sobel(black_box(&pixels), 320, 180) }),
        );
    }
}
