#[cfg(not(feature = "opencv-backend"))]
use crate::vision::backend::OPENCV_BACKEND_DISABLED_MESSAGE;
use crate::{
    config::{RecImage, VisionBackend},
    error::{RapidOcrError, Result},
    vision::{backend::resolve_backend_strict, resize::resize_bgr_inter_linear},
};

#[cfg(feature = "opencv-backend")]
use opencv::{
    core::{self, Mat, Size},
    imgproc,
    prelude::*,
};

pub fn resize_image(
    img: &RecImage,
    new_w: usize,
    new_h: usize,
    backend: VisionBackend,
) -> Result<RecImage> {
    if new_w == 0 || new_h == 0 {
        return Err(RapidOcrError::InvalidImage(
            "resize target width/height must be greater than zero".to_string(),
        ));
    }

    let backend = resolve_backend_strict(backend)?;
    match backend {
        VisionBackend::PureRust => resize_image_pure_rust(img, new_w, new_h),
        VisionBackend::OpenCv => resize_image_opencv_dispatch(img, new_w, new_h),
    }
}

pub fn rotate_180_image(img: &RecImage, backend: VisionBackend) -> Result<RecImage> {
    let backend = resolve_backend_strict(backend)?;
    match backend {
        VisionBackend::PureRust => rotate_180_image_pure_rust(img),
        VisionBackend::OpenCv => rotate_180_image_opencv_dispatch(img),
    }
}

fn resize_image_pure_rust(img: &RecImage, new_w: usize, new_h: usize) -> Result<RecImage> {
    let src_bgr = img.as_bgr_cow();
    let resized =
        resize_bgr_inter_linear(src_bgr.as_ref(), img.width(), img.height(), new_w, new_h)?;
    RecImage::from_bgr_u8(new_w, new_h, resized)
}

fn rotate_180_image_pure_rust(img: &RecImage) -> Result<RecImage> {
    let width = img.width();
    let height = img.height();
    let src = img.as_bgr_cow();
    let src = src.as_ref();
    let mut dst = vec![0_u8; src.len()];

    for y in 0..height {
        for x in 0..width {
            let src_idx = (y * width + x) * 3;
            let dst_y = height - 1 - y;
            let dst_x = width - 1 - x;
            let dst_idx = (dst_y * width + dst_x) * 3;
            dst[dst_idx..dst_idx + 3].copy_from_slice(&src[src_idx..src_idx + 3]);
        }
    }

    RecImage::from_bgr_u8(width, height, dst)
}

#[cfg(feature = "opencv-backend")]
fn resize_image_opencv(img: &RecImage, new_w: usize, new_h: usize) -> Result<RecImage> {
    let src_bgr = img.as_bgr_cow();
    let src_1d = Mat::from_slice(src_bgr.as_ref())
        .map_err(|e| RapidOcrError::Config(format!("opencv Mat::from_slice failed: {e}")))?;
    let src = src_1d
        .reshape(3, img.height() as i32)
        .map_err(|e| RapidOcrError::Config(format!("opencv Mat::reshape failed: {e}")))?;

    let mut resized = Mat::default();
    imgproc::resize(
        &src,
        &mut resized,
        Size::new(new_w as i32, new_h as i32),
        0.0,
        0.0,
        imgproc::INTER_LINEAR,
    )
    .map_err(|e| RapidOcrError::Config(format!("opencv resize failed: {e}")))?;

    let data = resized
        .data_bytes()
        .map_err(|e| RapidOcrError::Config(format!("opencv data_bytes failed: {e}")))?;
    let row_step = resized
        .step1(0)
        .map_err(|e| RapidOcrError::Config(format!("opencv step1 failed: {e}")))?;
    let mut out = vec![0_u8; new_w * new_h * 3];
    for y in 0..new_h {
        let src_off = y * row_step;
        let dst_off = y * new_w * 3;
        let src_row = &data[src_off..src_off + new_w * 3];
        let dst_row = &mut out[dst_off..dst_off + new_w * 3];
        dst_row.copy_from_slice(src_row);
    }

    RecImage::from_bgr_u8(new_w, new_h, out)
}

#[cfg(feature = "opencv-backend")]
fn rotate_180_image_opencv(img: &RecImage) -> Result<RecImage> {
    let src_bgr = img.as_bgr_cow();
    let src_1d = Mat::from_slice(src_bgr.as_ref())
        .map_err(|e| RapidOcrError::Config(format!("opencv Mat::from_slice failed: {e}")))?;
    let src = src_1d
        .reshape(3, img.height() as i32)
        .map_err(|e| RapidOcrError::Config(format!("opencv Mat::reshape failed: {e}")))?;

    let mut dst = Mat::default();
    core::rotate(&src, &mut dst, core::ROTATE_180)
        .map_err(|e| RapidOcrError::Config(format!("opencv rotate failed: {e}")))?;

    let out = dst
        .data_bytes()
        .map_err(|e| RapidOcrError::Config(format!("opencv data_bytes failed: {e}")))?;
    RecImage::from_bgr_u8(img.width(), img.height(), out.to_vec())
}

#[cfg(feature = "opencv-backend")]
fn resize_image_opencv_dispatch(img: &RecImage, new_w: usize, new_h: usize) -> Result<RecImage> {
    resize_image_opencv(img, new_w, new_h)
}

#[cfg(not(feature = "opencv-backend"))]
fn resize_image_opencv_dispatch(_img: &RecImage, _new_w: usize, _new_h: usize) -> Result<RecImage> {
    Err(RapidOcrError::Config(
        OPENCV_BACKEND_DISABLED_MESSAGE.to_string(),
    ))
}

#[cfg(feature = "opencv-backend")]
fn rotate_180_image_opencv_dispatch(img: &RecImage) -> Result<RecImage> {
    rotate_180_image_opencv(img)
}

#[cfg(not(feature = "opencv-backend"))]
fn rotate_180_image_opencv_dispatch(_img: &RecImage) -> Result<RecImage> {
    Err(RapidOcrError::Config(
        OPENCV_BACKEND_DISABLED_MESSAGE.to_string(),
    ))
}
