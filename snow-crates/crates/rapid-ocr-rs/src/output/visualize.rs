use image::{Rgb, RgbImage};

use crate::{Quad, config::RecImage, types::WordBox};

pub fn draw_ocr_result(img: &RecImage, boxes: &[Quad]) -> RgbImage {
    let mut canvas = to_rgb_image(img);
    for (idx, box_) in boxes.iter().enumerate() {
        let color = palette(idx);
        draw_quad(&mut canvas, *box_, color, 2);
    }
    canvas
}

pub fn draw_word_boxes(img: &RecImage, lines: &[Vec<WordBox>]) -> RgbImage {
    let mut canvas = to_rgb_image(img);
    let mut color_idx = 0usize;
    for line in lines {
        for word in line {
            let color = palette(color_idx);
            draw_quad(&mut canvas, word.bbox, color, 1);
            color_idx += 1;
        }
    }
    canvas
}

fn to_rgb_image(img: &RecImage) -> RgbImage {
    let bgr = img.as_bgr_cow();
    let bgr = bgr.as_ref();
    let mut rgb = vec![0u8; bgr.len()];
    for (src, dst) in bgr.chunks_exact(3).zip(rgb.chunks_exact_mut(3)) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
    }
    RgbImage::from_raw(img.width() as u32, img.height() as u32, rgb)
        .unwrap_or_else(|| RgbImage::new(img.width() as u32, img.height() as u32))
}

fn draw_quad(img: &mut RgbImage, quad: Quad, color: Rgb<u8>, thickness: i32) {
    for edge in 0..4 {
        let p0 = quad[edge];
        let p1 = quad[(edge + 1) % 4];
        draw_line(
            img,
            p0[0].round() as i32,
            p0[1].round() as i32,
            p1[0].round() as i32,
            p1[1].round() as i32,
            color,
            thickness,
        );
    }
}

fn draw_line(
    img: &mut RgbImage,
    mut x0: i32,
    mut y0: i32,
    x1: i32,
    y1: i32,
    color: Rgb<u8>,
    thickness: i32,
) {
    let dx = (x1 - x0).abs();
    let sx = if x0 < x1 { 1 } else { -1 };
    let dy = -(y1 - y0).abs();
    let sy = if y0 < y1 { 1 } else { -1 };
    let mut err = dx + dy;

    loop {
        draw_thick_point(img, x0, y0, color, thickness);
        if x0 == x1 && y0 == y1 {
            break;
        }
        let e2 = 2 * err;
        if e2 >= dy {
            err += dy;
            x0 += sx;
        }
        if e2 <= dx {
            err += dx;
            y0 += sy;
        }
    }
}

fn draw_thick_point(img: &mut RgbImage, x: i32, y: i32, color: Rgb<u8>, thickness: i32) {
    let radius = (thickness - 1).max(0);
    for oy in -radius..=radius {
        for ox in -radius..=radius {
            let px = x + ox;
            let py = y + oy;
            if px < 0 || py < 0 {
                continue;
            }
            let px = px as u32;
            let py = py as u32;
            if px < img.width() && py < img.height() {
                img.put_pixel(px, py, color);
            }
        }
    }
}

fn palette(idx: usize) -> Rgb<u8> {
    const COLORS: [[u8; 3]; 8] = [
        [255, 0, 0],
        [0, 170, 0],
        [0, 102, 255],
        [255, 140, 0],
        [128, 0, 255],
        [255, 20, 147],
        [0, 191, 191],
        [255, 215, 0],
    ];
    let c = COLORS[idx % COLORS.len()];
    Rgb(c)
}

#[cfg(test)]
mod tests {
    use crate::config::RecImage;

    use super::draw_ocr_result;

    #[test]
    fn draw_ocr_result_shape_matches_input() {
        let img = RecImage::from_bgr_u8(16, 8, vec![0; 16 * 8 * 3]).expect("valid image");
        let vis = draw_ocr_result(&img, &[[[1.0, 1.0], [14.0, 1.0], [14.0, 6.0], [1.0, 6.0]]]);
        assert_eq!(vis.width(), 16);
        assert_eq!(vis.height(), 8);
    }
}
