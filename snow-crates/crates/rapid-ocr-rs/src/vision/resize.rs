use crate::error::{RapidOcrError, Result};
use rayon::prelude::*;

fn check_dims(
    src_w: usize,
    src_h: usize,
    dst_w: usize,
    dst_h: usize,
    src_len: usize,
) -> Result<()> {
    if src_w == 0 || src_h == 0 {
        return Err(RapidOcrError::InvalidImage(
            "source width/height must be greater than zero".to_string(),
        ));
    }
    if dst_w == 0 || dst_h == 0 {
        return Err(RapidOcrError::InvalidImage(
            "destination width/height must be greater than zero".to_string(),
        ));
    }
    let expect = src_w
        .checked_mul(src_h)
        .and_then(|v| v.checked_mul(3))
        .ok_or_else(|| RapidOcrError::InvalidImage("source size overflow".to_string()))?;
    if src_len != expect {
        return Err(RapidOcrError::InvalidImage(format!(
            "invalid source BGR length: expected {expect}, got {src_len}"
        )));
    }
    Ok(())
}

const INTER_RESIZE_COEF_BITS: i32 = 11;
const INTER_RESIZE_COEF_SCALE: i32 = 1 << INTER_RESIZE_COEF_BITS;

#[derive(Debug, Default)]
pub(crate) struct LinearResizeScratch {
    xofs: Vec<i32>,
    ialpha: Vec<i16>,
    yofs: Vec<i32>,
    ibeta: Vec<i16>,
    hrow0: Vec<i32>,
    hrow1: Vec<i32>,
}

fn cv_round_ties_even_f32(v: f32) -> i32 {
    if !v.is_finite() {
        return 0;
    }
    let r = v.round_ties_even();
    if r < i32::MIN as f32 {
        i32::MIN
    } else if r > i32::MAX as f32 {
        i32::MAX
    } else {
        r as i32
    }
}

fn saturate_cast_i16_from_f32(v: f32) -> i16 {
    cv_round_ties_even_f32(v).clamp(i16::MIN as i32, i16::MAX as i32) as i16
}

fn clip_i32(x: i32, lo: i32, hi_exclusive: i32) -> i32 {
    if x < lo {
        lo
    } else if x >= hi_exclusive {
        hi_exclusive - 1
    } else {
        x
    }
}

fn hresize_row_bgr_u8(
    src_row: &[u8],
    src_w: usize,
    xofs: &[i32],
    ialpha: &[i16],
    dst_row: &mut [i32],
) {
    debug_assert_eq!(dst_row.len(), xofs.len() * 3);
    // Safety:
    // - `xofs` values are pre-clamped to valid source x range.
    // - all computed indices stay in-bounds for `src_row`, `ialpha`, and `dst_row`.
    unsafe {
        let src_ptr = src_row.as_ptr();
        let alpha_ptr = ialpha.as_ptr();
        let dst_ptr = dst_row.as_mut_ptr();
        for dx in 0..xofs.len() {
            let sx = *xofs.get_unchecked(dx) as usize;
            let sx1 = (sx + 1).min(src_w.saturating_sub(1));
            let a0 = *alpha_ptr.add(dx * 2) as i32;
            let a1 = *alpha_ptr.add(dx * 2 + 1) as i32;

            let src0 = sx * 3;
            let src1 = sx1 * 3;
            let dst = dx * 3;

            *dst_ptr.add(dst) = *src_ptr.add(src0) as i32 * a0 + *src_ptr.add(src1) as i32 * a1;
            *dst_ptr.add(dst + 1) =
                *src_ptr.add(src0 + 1) as i32 * a0 + *src_ptr.add(src1 + 1) as i32 * a1;
            *dst_ptr.add(dst + 2) =
                *src_ptr.add(src0 + 2) as i32 * a0 + *src_ptr.add(src1 + 2) as i32 * a1;
        }
    }
}

#[inline]
fn should_parallelize_resize(dst_w: usize, dst_h: usize) -> bool {
    dst_w
        .checked_mul(dst_h)
        .is_some_and(|v| v >= 768 * 512 && rayon::current_num_threads() > 1)
}

#[derive(Clone, Copy)]
struct ResizeDims {
    src_w: usize,
    src_h: usize,
    dst_w: usize,
}

struct ResizeKernelTables<'a> {
    xofs: &'a [i32],
    ialpha: &'a [i16],
    yofs: &'a [i32],
    ibeta: &'a [i16],
}

struct ResizeRowsScratch<'a> {
    hrow0: &'a mut Vec<i32>,
    hrow1: &'a mut Vec<i32>,
}

fn resize_rows_into(
    src: &[u8],
    dims: ResizeDims,
    kernel: &ResizeKernelTables<'_>,
    start_dy: usize,
    dst: &mut [u8],
    scratch: &mut ResizeRowsScratch<'_>,
) {
    let row_stride = dims.dst_w * 3;
    let row_count = dst.len() / row_stride;
    if row_count == 0 {
        return;
    }

    if scratch.hrow0.len() < row_stride {
        scratch.hrow0.resize(row_stride, 0);
    }
    if scratch.hrow1.len() < row_stride {
        scratch.hrow1.resize(row_stride, 0);
    }
    let hrow0 = &mut scratch.hrow0[..row_stride];
    let hrow1 = &mut scratch.hrow1[..row_stride];

    for row in 0..row_count {
        let dy = start_dy + row;
        let sy0 = clip_i32(kernel.yofs[dy], 0, dims.src_h as i32) as usize;
        let sy1 = clip_i32(kernel.yofs[dy] + 1, 0, dims.src_h as i32) as usize;
        let src_row0 = &src[sy0 * dims.src_w * 3..(sy0 + 1) * dims.src_w * 3];
        let src_row1 = &src[sy1 * dims.src_w * 3..(sy1 + 1) * dims.src_w * 3];
        hresize_row_bgr_u8(src_row0, dims.src_w, kernel.xofs, kernel.ialpha, hrow0);
        hresize_row_bgr_u8(src_row1, dims.src_w, kernel.xofs, kernel.ialpha, hrow1);

        let b0 = kernel.ibeta[dy * 2] as i32;
        let b1 = kernel.ibeta[dy * 2 + 1] as i32;
        let dst_row = &mut dst[row * row_stride..(row + 1) * row_stride];

        // Safety:
        // - `row0`, `row1`, and `dst_row` all have identical length.
        // - loop bounds guarantee in-range pointer access.
        unsafe {
            let row0_ptr = hrow0.as_ptr();
            let row1_ptr = hrow1.as_ptr();
            let dst_ptr = dst_row.as_mut_ptr();
            for x in 0..row_stride {
                let v = (((b0 * (*row0_ptr.add(x) >> 4)) >> 16)
                    + ((b1 * (*row1_ptr.add(x) >> 4)) >> 16)
                    + 2)
                    >> 2;
                *dst_ptr.add(x) = v.clamp(0, 255) as u8;
            }
        }
    }
}

pub(crate) fn resize_bgr_inter_linear(
    src: &[u8],
    src_w: usize,
    src_h: usize,
    dst_w: usize,
    dst_h: usize,
) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    resize_bgr_inter_linear_into(src, src_w, src_h, dst_w, dst_h, &mut out)?;
    Ok(out)
}

pub(crate) fn resize_bgr_inter_linear_into(
    src: &[u8],
    src_w: usize,
    src_h: usize,
    dst_w: usize,
    dst_h: usize,
    out: &mut Vec<u8>,
) -> Result<()> {
    let mut scratch = LinearResizeScratch::default();
    resize_bgr_inter_linear_into_with_scratch(src, src_w, src_h, dst_w, dst_h, out, &mut scratch)
}

pub(crate) fn resize_bgr_inter_linear_into_with_scratch(
    src: &[u8],
    src_w: usize,
    src_h: usize,
    dst_w: usize,
    dst_h: usize,
    out: &mut Vec<u8>,
    scratch: &mut LinearResizeScratch,
) -> Result<()> {
    check_dims(src_w, src_h, dst_w, dst_h, src.len())?;
    if src_w == dst_w && src_h == dst_h {
        out.clear();
        out.extend_from_slice(src);
        return Ok(());
    }
    if src_w == dst_w.saturating_mul(2) && src_h == dst_h.saturating_mul(2) {
        resize_bgr_area_fast_2x2(src, src_w, dst_w, dst_h, out);
        return Ok(());
    }

    // Match OpenCV's internal order:
    // inv_scale = dst/src, then scale = 1.0 / inv_scale.
    let inv_scale_x = dst_w as f64 / src_w as f64;
    let inv_scale_y = dst_h as f64 / src_h as f64;
    let scale_x = 1.0 / inv_scale_x;
    let scale_y = 1.0 / inv_scale_y;

    scratch.xofs.resize(dst_w, 0);
    scratch.ialpha.resize(dst_w * 2, 0);
    scratch.yofs.resize(dst_h, 0);
    scratch.ibeta.resize(dst_h * 2, 0);

    let xofs = &mut scratch.xofs;
    let ialpha = &mut scratch.ialpha;
    for dx in 0..dst_w {
        let mut fx = ((dx as f64 + 0.5) * scale_x - 0.5) as f32;
        let mut sx = fx.floor() as i32;
        fx -= sx as f32;

        if sx < 0 {
            fx = 0.0;
            sx = 0;
        }
        if sx >= src_w as i32 - 1 {
            fx = 0.0;
            sx = src_w as i32 - 1;
        }

        xofs[dx] = sx;
        ialpha[dx * 2] = saturate_cast_i16_from_f32((1.0 - fx) * INTER_RESIZE_COEF_SCALE as f32);
        ialpha[dx * 2 + 1] = saturate_cast_i16_from_f32(fx * INTER_RESIZE_COEF_SCALE as f32);
    }

    let yofs = &mut scratch.yofs;
    let ibeta = &mut scratch.ibeta;
    for dy in 0..dst_h {
        let mut fy = ((dy as f64 + 0.5) * scale_y - 0.5) as f32;
        let sy = fy.floor() as i32;
        fy -= sy as f32;

        yofs[dy] = sy;
        ibeta[dy * 2] = saturate_cast_i16_from_f32((1.0 - fy) * INTER_RESIZE_COEF_SCALE as f32);
        ibeta[dy * 2 + 1] = saturate_cast_i16_from_f32(fy * INTER_RESIZE_COEF_SCALE as f32);
    }

    out.resize(dst_w * dst_h * 3, 0);
    let dims = ResizeDims {
        src_w,
        src_h,
        dst_w,
    };
    let kernel = ResizeKernelTables {
        xofs: &scratch.xofs,
        ialpha: &scratch.ialpha,
        yofs: &scratch.yofs,
        ibeta: &scratch.ibeta,
    };
    if should_parallelize_resize(dst_w, dst_h) {
        let row_stride = dst_w * 3;
        let chunk_rows = (dst_h / rayon::current_num_threads().max(1)).max(64);
        let chunk_bytes = row_stride * chunk_rows;

        out.par_chunks_mut(chunk_bytes).enumerate().for_each_init(
            || (Vec::<i32>::new(), Vec::<i32>::new()),
            |(hrow0, hrow1), (chunk_idx, dst_chunk)| {
                let start_dy = chunk_idx * chunk_rows;
                let mut rows_scratch = ResizeRowsScratch { hrow0, hrow1 };
                resize_rows_into(src, dims, &kernel, start_dy, dst_chunk, &mut rows_scratch);
            },
        );
    } else {
        let mut rows_scratch = ResizeRowsScratch {
            hrow0: &mut scratch.hrow0,
            hrow1: &mut scratch.hrow1,
        };
        resize_rows_into(src, dims, &kernel, 0, out, &mut rows_scratch);
    }

    Ok(())
}

fn resize_bgr_area_fast_2x2(
    src: &[u8],
    src_w: usize,
    dst_w: usize,
    dst_h: usize,
    out: &mut Vec<u8>,
) {
    out.resize(dst_w * dst_h * 3, 0);
    let src_stride = src_w * 3;
    let dst_stride = dst_w * 3;

    for dy in 0..dst_h {
        let src_y = dy * 2;
        let row0 = src_y * src_stride;
        let row1 = row0 + src_stride;
        let dst_row = dy * dst_stride;
        for dx in 0..dst_w {
            let src_x = dx * 2 * 3;
            let dst_x = dx * 3;
            for c in 0..3 {
                let sum = src[row0 + src_x + c] as u16
                    + src[row0 + src_x + 3 + c] as u16
                    + src[row1 + src_x + c] as u16
                    + src[row1 + src_x + 3 + c] as u16;
                out[dst_row + dst_x + c] = ((sum + 2) >> 2) as u8;
            }
        }
    }
}

#[cfg(all(test, feature = "opencv-backend"))]
mod tests {
    use super::resize_bgr_inter_linear;
    use crate::config::RecImage;
    use opencv::{
        core::{Mat, Size},
        imgproc,
        prelude::*,
    };
    use std::path::PathBuf;

    #[test]
    fn linear_resize_matches_opencv_on_gradient() {
        let src_w = 17usize;
        let src_h = 13usize;
        let dst_w = 23usize;
        let dst_h = 29usize;
        let mut src = vec![0_u8; src_w * src_h * 3];
        for y in 0..src_h {
            for x in 0..src_w {
                let i = (y * src_w + x) * 3;
                src[i] = ((x * 7 + y * 3) % 256) as u8;
                src[i + 1] = ((x * 5 + y * 11) % 256) as u8;
                src[i + 2] = ((x * 13 + y * 2) % 256) as u8;
            }
        }

        let ours =
            resize_bgr_inter_linear(&src, src_w, src_h, dst_w, dst_h).expect("resize should work");

        let src_1d = Mat::from_slice(&src).expect("mat from slice");
        let src_mat = src_1d.reshape(3, src_h as i32).expect("reshape");
        let mut dst = Mat::default();
        imgproc::resize(
            &src_mat,
            &mut dst,
            Size::new(dst_w as i32, dst_h as i32),
            0.0,
            0.0,
            imgproc::INTER_LINEAR,
        )
        .expect("opencv resize");
        let opencv = dst.data_bytes().expect("opencv bytes");

        let mut max_abs = 0_i32;
        for (a, b) in ours.iter().zip(opencv.iter()) {
            max_abs = max_abs.max((*a as i32 - *b as i32).abs());
        }
        assert!(
            max_abs <= 1,
            "max abs pixel diff should be <= 1, got {max_abs}"
        );
    }

    #[test]
    fn linear_resize_matches_opencv_on_real_upscale() {
        let mut p = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        p.push("test");
        p.push("test_files");
        p.push("te.png");
        let img = RecImage::from_path(&p).expect("te image should load");
        let src = img.as_bgr_bytes();

        let dst_w = 6432usize;
        let dst_h = 736usize;
        let ours = resize_bgr_inter_linear(&src, img.width(), img.height(), dst_w, dst_h)
            .expect("resize should work");

        let src_1d = Mat::from_slice(&src).expect("mat from slice");
        let src_mat = src_1d.reshape(3, img.height() as i32).expect("reshape");
        let mut dst = Mat::default();
        imgproc::resize(
            &src_mat,
            &mut dst,
            Size::new(dst_w as i32, dst_h as i32),
            0.0,
            0.0,
            imgproc::INTER_LINEAR,
        )
        .expect("opencv resize");
        let opencv = dst.data_bytes().expect("opencv bytes");

        let mut max_abs = 0_i32;
        for (a, b) in ours.iter().zip(opencv.iter()) {
            max_abs = max_abs.max((*a as i32 - *b as i32).abs());
        }
        assert!(
            max_abs <= 1,
            "real upscale max abs diff should be <= 1, got {max_abs}"
        );
    }

    #[test]
    fn linear_resize_matches_opencv_exact_for_2x_downscale() {
        let src_w = 18usize;
        let src_h = 14usize;
        let dst_w = src_w / 2;
        let dst_h = src_h / 2;
        let mut src = vec![0_u8; src_w * src_h * 3];
        for y in 0..src_h {
            for x in 0..src_w {
                let i = (y * src_w + x) * 3;
                src[i] = ((x * 17 + y * 9) % 256) as u8;
                src[i + 1] = ((x * 3 + y * 23) % 256) as u8;
                src[i + 2] = ((x * 29 + y * 5) % 256) as u8;
            }
        }

        let ours =
            resize_bgr_inter_linear(&src, src_w, src_h, dst_w, dst_h).expect("resize should work");

        let src_1d = Mat::from_slice(&src).expect("mat from slice");
        let src_mat = src_1d.reshape(3, src_h as i32).expect("reshape");
        let mut dst = Mat::default();
        imgproc::resize(
            &src_mat,
            &mut dst,
            Size::new(dst_w as i32, dst_h as i32),
            0.0,
            0.0,
            imgproc::INTER_LINEAR,
        )
        .expect("opencv resize");
        let opencv = dst.data_bytes().expect("opencv bytes");

        assert_eq!(ours, opencv);
    }
}
