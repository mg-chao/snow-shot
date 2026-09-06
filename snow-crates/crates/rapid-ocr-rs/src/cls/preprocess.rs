#[cfg(test)]
use ndarray::Array3;
use std::sync::OnceLock;

use crate::{
    config::{RecImage, VisionBackend},
    error::{RapidOcrError, Result},
    vision::{
        backend::resolve_backend_strict,
        image_backend::{resize_image, rotate_180_image},
        resize::{LinearResizeScratch, resize_bgr_inter_linear_into_with_scratch},
    },
};

#[cfg(test)]
pub fn resize_norm_img(img: &RecImage, cls_image_shape: [usize; 3]) -> Result<Array3<f32>> {
    resize_norm_img_with_backend(img, cls_image_shape, VisionBackend::PureRust)
}

#[cfg(test)]
pub fn resize_norm_img_with_backend(
    img: &RecImage,
    cls_image_shape: [usize; 3],
    backend: VisionBackend,
) -> Result<Array3<f32>> {
    let backend = resolve_backend_strict(backend)?;
    resize_norm_img_impl(img, cls_image_shape, backend)
}

#[cfg_attr(not(test), allow(dead_code))]
pub(crate) fn write_resize_norm_img_into_slice(
    img: &RecImage,
    cls_image_shape: [usize; 3],
    backend: VisionBackend,
    dst: &mut [f32],
) -> Result<()> {
    write_resize_norm_img_into_slice_impl(img, cls_image_shape, backend, dst)
}

pub(crate) fn write_resize_norm_img_into_slice_with_scratch(
    img: &RecImage,
    cls_image_shape: [usize; 3],
    backend: VisionBackend,
    dst: &mut [f32],
    tmp_bgr: &mut Vec<u8>,
    resize_scratch: &mut LinearResizeScratch,
) -> Result<()> {
    let backend = resolve_backend_strict(backend)?;
    if backend != VisionBackend::PureRust {
        return write_resize_norm_img_into_slice_impl(img, cls_image_shape, backend, dst);
    }

    let (img_c, img_h, img_w) = validate_cls_shape(cls_image_shape)?;
    let sample_len = img_c
        .checked_mul(img_h)
        .and_then(|v| v.checked_mul(img_w))
        .ok_or_else(|| RapidOcrError::InvalidInput("cls sample size overflow".to_string()))?;
    if dst.len() != sample_len {
        return Err(RapidOcrError::InvalidInput(format!(
            "cls destination slice size mismatch: expected {sample_len}, got {}",
            dst.len()
        )));
    }
    dst.fill(0.0);

    let ratio = img.width() as f32 / img.height() as f32;
    let resized_w = ((img_h as f32 * ratio).ceil() as usize).min(img_w).max(1);
    let src = img.as_bgr_cow();
    resize_bgr_inter_linear_into_with_scratch(
        src.as_ref(),
        img.width(),
        img.height(),
        resized_w,
        img_h,
        tmp_bgr,
        resize_scratch,
    )?;

    let plane_stride = img_h * img_w;
    let plane_stride2 = plane_stride * 2;
    let row_src_stride = resized_w * img_c;
    for y in 0..img_h {
        let row_base = y * img_w;
        // Safety:
        // - `tmp_bgr` contains `resized_w * img_h * img_c` bytes.
        // - destination writes stay within validated CHW destination slice.
        unsafe {
            let mut src_px = tmp_bgr.as_ptr().add(y * row_src_stride);
            let dst_ptr = dst.as_mut_ptr();
            for x in 0..resized_w {
                let row_offset = row_base + x;
                let b = normalize_to_signed_unit(*src_px);
                let g = normalize_to_signed_unit(*src_px.add(1));
                let r = normalize_to_signed_unit(*src_px.add(2));
                *dst_ptr.add(row_offset) = b;
                *dst_ptr.add(plane_stride + row_offset) = g;
                *dst_ptr.add(plane_stride2 + row_offset) = r;
                src_px = src_px.add(img_c);
            }
        }
    }

    Ok(())
}

#[cfg(test)]
fn resize_norm_img_impl(
    img: &RecImage,
    cls_image_shape: [usize; 3],
    backend: VisionBackend,
) -> Result<Array3<f32>> {
    let (img_c, img_h, img_w) = validate_cls_shape(cls_image_shape)?;
    let sample_len = img_c
        .checked_mul(img_h)
        .and_then(|v| v.checked_mul(img_w))
        .ok_or_else(|| RapidOcrError::InvalidInput("cls sample size overflow".to_string()))?;
    let mut out = vec![0.0_f32; sample_len];
    write_resize_norm_img_into_slice_impl(img, cls_image_shape, backend, &mut out)?;
    Array3::from_shape_vec((img_c, img_h, img_w), out).map_err(|e| {
        RapidOcrError::InvalidInput(format!("failed to build cls normalized tensor: {e}"))
    })
}

pub fn rotate_180_with_backend(img: &RecImage, backend: VisionBackend) -> Result<RecImage> {
    let backend = resolve_backend_strict(backend)?;
    rotate_180_image(img, backend)
}

fn write_resize_norm_img_into_slice_impl(
    img: &RecImage,
    cls_image_shape: [usize; 3],
    backend: VisionBackend,
    dst: &mut [f32],
) -> Result<()> {
    let (img_c, img_h, img_w) = validate_cls_shape(cls_image_shape)?;
    let sample_len = img_c
        .checked_mul(img_h)
        .and_then(|v| v.checked_mul(img_w))
        .ok_or_else(|| RapidOcrError::InvalidInput("cls sample size overflow".to_string()))?;
    if dst.len() != sample_len {
        return Err(RapidOcrError::InvalidInput(format!(
            "cls destination slice size mismatch: expected {sample_len}, got {}",
            dst.len()
        )));
    }

    dst.fill(0.0);

    let ratio = img.width() as f32 / img.height() as f32;
    let resized_w = ((img_h as f32 * ratio).ceil() as usize).min(img_w).max(1);
    let resized = resize_image(img, resized_w, img_h, backend)?;
    let src = resized.as_bgr_cow();
    let src = src.as_ref();
    let plane_stride = img_h * img_w;
    let plane_stride2 = plane_stride * 2;
    debug_assert_eq!(src.len(), resized.width() * resized.height() * img_c);

    let row_src_stride = resized.width() * img_c;
    for y in 0..img_h {
        let row_base = y * img_w;
        // Safety:
        // - `y < img_h`, and `row_src_stride = resized.width() * img_c`.
        // - source row pointer stays within resized image memory.
        // - destination writes stay within validated CHW destination slice.
        unsafe {
            let mut src_px = src.as_ptr().add(y * row_src_stride);
            let dst_ptr = dst.as_mut_ptr();
            for x in 0..resized_w {
                let row_offset = row_base + x;
                let b = normalize_to_signed_unit(*src_px);
                let g = normalize_to_signed_unit(*src_px.add(1));
                let r = normalize_to_signed_unit(*src_px.add(2));

                *dst_ptr.add(row_offset) = b;
                *dst_ptr.add(plane_stride + row_offset) = g;
                *dst_ptr.add(plane_stride2 + row_offset) = r;
                src_px = src_px.add(img_c);
            }
        }
    }

    Ok(())
}

#[inline]
fn normalize_to_signed_unit(v: u8) -> f32 {
    signed_unit_lut()[v as usize]
}

fn signed_unit_lut() -> &'static [f32; 256] {
    static LUT: OnceLock<[f32; 256]> = OnceLock::new();
    LUT.get_or_init(|| {
        let mut out = [0.0_f32; 256];
        let mut i = 0usize;
        while i < 256 {
            let mut v = i as f32 * (2.0 / 255.0) - 1.0;
            if v == -0.0 {
                v = 0.0;
            }
            out[i] = v;
            i += 1;
        }
        out
    })
}

fn validate_cls_shape(cls_image_shape: [usize; 3]) -> Result<(usize, usize, usize)> {
    let [img_c, img_h, img_w] = cls_image_shape;
    if img_c != 3 {
        return Err(RapidOcrError::Config(format!(
            "cls_image_shape must start with 3 channels, got {img_c}"
        )));
    }
    if img_h == 0 || img_w == 0 {
        return Err(RapidOcrError::Config(
            "cls image shape dimensions must be greater than zero".to_string(),
        ));
    }
    Ok((img_c, img_h, img_w))
}

#[cfg(test)]
mod tests {
    use super::{resize_norm_img, write_resize_norm_img_into_slice};
    use crate::config::{RecImage, VisionBackend};

    #[test]
    fn write_into_slice_matches_array_output() {
        let image = RecImage::from_bgr_u8(
            9,
            6,
            (0..9 * 6 * 3).map(|v| (v % 255) as u8).collect::<Vec<_>>(),
        )
        .expect("valid image");
        let shape = [3, 48, 192];
        let arr = resize_norm_img(&image, shape).expect("array output should work");

        let mut buf = vec![0.0_f32; arr.len()];
        write_resize_norm_img_into_slice(&image, shape, VisionBackend::PureRust, &mut buf)
            .expect("slice writer should work");

        let arr_slice = arr.as_slice().expect("array should be contiguous");
        assert_eq!(arr_slice.len(), buf.len());
        for (a, b) in arr_slice.iter().zip(buf.iter()) {
            assert!((*a - *b).abs() <= f32::EPSILON);
        }
    }
}
