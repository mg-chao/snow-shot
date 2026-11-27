#![allow(clippy::missing_safety_doc)]
use std::panic::{AssertUnwindSafe, catch_unwind};
use visual_region_detector::{BgrImage, detect_regions};

#[repr(C)]
pub struct SnowDetectedRegion {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub category: u32,
}
pub struct SnowDetectedRegions {
    regions: Vec<SnowDetectedRegion>,
}
const CATEGORIES: [&str; 7] = [
    "text",
    "text_in_box",
    "image",
    "avatar",
    "icon",
    "message_box",
    "text_block",
];

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snow_detect_visual_regions(
    pixels: *const u8,
    length: usize,
    width: u32,
    height: u32,
    stride: usize,
    result: *mut *mut SnowDetectedRegions,
) -> i32 {
    if result.is_null() {
        return 1;
    }
    unsafe {
        result.write(std::ptr::null_mut());
    }
    catch_unwind(AssertUnwindSafe(|| {
        let Some(row) = (width as usize).checked_mul(3) else {
            return 1;
        };
        let Some(required) = stride
            .checked_mul(height.saturating_sub(1) as usize)
            .and_then(|n| n.checked_add(row))
        else {
            return 1;
        };
        if pixels.is_null()
            || width == 0
            || height == 0
            || u64::from(width) * u64::from(height) > 100_000_000
            || stride < row
            || length < required
        {
            return 1;
        }
        let input = unsafe { std::slice::from_raw_parts(pixels, required) };
        let mut bgr = Vec::with_capacity(width as usize * height as usize);
        for input_row in input.chunks(stride) {
            bgr.extend(
                input_row[..row]
                    .chunks_exact(3)
                    .map(|pixel| [pixel[0], pixel[1], pixel[2]]),
            );
        }
        let Ok(image) = BgrImage::from_bgr_pixels(width, height, bgr) else {
            return 1;
        };
        let Ok(regions) = detect_regions(&image) else {
            return 2;
        };
        let regions = regions
            .into_iter()
            .filter_map(|r| {
                let category = CATEGORIES.iter().position(|kind| *kind == r.kind)? as u32;
                Some(SnowDetectedRegion {
                    x: r.rect.x,
                    y: r.rect.y,
                    width: r.rect.w,
                    height: r.rect.h,
                    category,
                })
            })
            .collect();
        unsafe {
            result.write(Box::into_raw(Box::new(SnowDetectedRegions { regions })));
        }
        0
    }))
    .unwrap_or(2)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snow_detected_regions_data(
    result: *const SnowDetectedRegions,
    count: *mut usize,
) -> *const SnowDetectedRegion {
    if count.is_null() {
        return std::ptr::null();
    }
    let Some(result) = (unsafe { result.as_ref() }) else {
        unsafe {
            count.write(0);
        }
        return std::ptr::null();
    };
    unsafe {
        count.write(result.regions.len());
    }
    result.regions.as_ptr()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn snow_detected_regions_release(result: *mut SnowDetectedRegions) {
    if !result.is_null() {
        drop(unsafe { Box::from_raw(result) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strided_pixels_match_packed_pixels_without_trailing_padding() {
        let width = 800;
        let height = 400;
        let row = width * 3;
        let stride = row + 8;
        let mut packed = vec![245; row * height];
        for y in 200..233 {
            packed[y * row + 400 * 3..y * row + 402 * 3].fill(0);
        }
        let mut padded = vec![1; stride * (height - 1) + row];
        for (destination, source) in padded.chunks_mut(stride).zip(packed.chunks_exact(row)) {
            destination[..row].copy_from_slice(source);
        }

        let detect = |pixels: &[u8], stride| {
            let mut raw = std::ptr::null_mut();
            assert_eq!(
                unsafe {
                    snow_detect_visual_regions(
                        pixels.as_ptr(),
                        pixels.len(),
                        width as u32,
                        height as u32,
                        stride,
                        &mut raw,
                    )
                },
                0,
            );
            assert!(!raw.is_null());
            let result = unsafe { Box::from_raw(raw) };
            result
                .regions
                .iter()
                .map(|r| (r.x, r.y, r.width, r.height, r.category))
                .collect::<Vec<_>>()
        };
        let expected = detect(&packed, row);
        assert!(!expected.is_empty());
        assert_eq!(detect(&padded, stride), expected);
    }
    #[test]
    fn rejects_invalid_input_and_clears_result() {
        let mut result = std::ptr::dangling_mut();
        assert_eq!(
            unsafe { snow_detect_visual_regions(std::ptr::null(), 0, 1, 1, 3, &mut result) },
            1
        );
        assert!(result.is_null());
        let pixels = [255; 12];
        assert_eq!(
            unsafe {
                snow_detect_visual_regions(pixels.as_ptr(), pixels.len(), 2, 2, 5, &mut result)
            },
            1
        );
    }
}
