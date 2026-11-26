//! Flat surfaces and outlined control reconstruction.

use crate::backend::{self, MorphOp};
use crate::color::{median_color, quant_key};
use crate::error::Result;
use crate::geometry::Rect;
use crate::grid::{Image, Mask};
use crate::mask::{cc, fill_holes, morphology};
use crate::selection::merge;
use snow_memory::RasterArray;

pub(crate) fn estimate_surfaces(bgr: &Image) -> Result<(Image, Vec<Rect>, Mask)> {
    let (w, h) = (bgr.w, bgr.h);
    // 32 levels per channel produces 15-bit keys.
    let mut keys = RasterArray::<u16>::zeroed(bgr.data.len());
    let mut counts = vec![0; 32768];
    for (key, pixel) in keys.iter_mut().zip(&bgr.data) {
        let index = quant_key(pixel, 8);
        *key = index as u16;
        counts[index] += 1;
    }
    let mut candidates = Vec::new();
    for (key, count) in counts.iter().enumerate() {
        if *count < 200.max(h * w / 10000) {
            continue;
        }
        let mask = Mask {
            w,
            h,
            data: keys
                .iter()
                .map(|&k| u8::from(usize::from(k) == key))
                .collect(),
        };
        let (labels, stats) = cc(&mask)?;
        for c in stats {
            let r = c.rect;
            if c.area < 200 || r.w < 10 || r.h < 10 || f64::from(c.area) < 0.3 * r.area() as f64 {
                continue;
            }
            let positions = || (r.y..r.y2()).flat_map(|y| (r.x..r.x2()).map(move |x| (x, y)));
            let filled = fill_holes(Mask {
                w: r.w,
                h: r.h,
                data: positions()
                    .map(|(x, y)| if *labels.at(x, y) == c.id { 255 } else { 0 })
                    .collect(),
            });
            if (filled.data.iter().filter(|v| **v > 0).count() as f64) < 0.65 * r.area() as f64 {
                continue;
            }
            let color = median_color(
                positions()
                    .filter(|&(x, y)| *labels.at(x, y) == c.id)
                    .map(|(x, y)| *bgr.at(x, y)),
            );
            candidates.push((r, filled, color));
        }
    }
    drop(keys);
    let mut bg = backend::median_blur_image(bgr, 31)?;
    let mut border = Mask::new(w, h, 0);
    let mut surfaces = Vec::new();
    candidates.sort_by_key(|c| -c.0.area());
    for (r, mask, color) in candidates {
        let mut padded = Mask::new(r.w + 4, r.h + 4, 0);
        for y in 0..r.h {
            for x in 0..r.w {
                let m = *mask.at(x, y);
                padded.set(x + 2, y + 2, m);
                if m > 0 {
                    bg.set(r.x + x, r.y + y, color.map(|v| v as u8));
                }
            }
        }
        let edge = morphology(&padded, MorphOp::Gradient, 5, 5)?;
        for y in 0..r.h {
            for x in 0..r.w {
                border.set(
                    r.x + x,
                    r.y + y,
                    *border.at(r.x + x, r.y + y) | *edge.at(x + 2, y + 2),
                );
            }
        }
        if f64::from(r.w) < 0.9 * f64::from(w) && f64::from(r.h) < 0.5 * f64::from(h) {
            surfaces.push(r);
        }
    }
    Ok((bg, surfaces, border))
}

pub(crate) fn outlined_controls(bgr: &Image) -> Result<(Vec<Rect>, Mask)> {
    let (controls, outlines) = backend::outlined_controls(bgr)?;
    Ok((merge(controls, 0.8), outlines))
}
