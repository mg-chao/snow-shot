use visual_region_detector::{BgrImage, Rect, detect_regions, draw_regions, iou};

#[test]
fn rejects_invalid_images() {
    assert!(BgrImage::from_bgr_bytes(0, 10, Vec::new()).is_err());
    assert!(BgrImage::from_bgr_bytes(10, 0, Vec::new()).is_err());
    assert!(BgrImage::from_bgr_bytes(10, 10, vec![0; 10]).is_err());
    assert!(BgrImage::from_bgr_pixels(10, 10, vec![[0; 3]; 9]).is_err());
}

#[test]
fn owned_bgr_pixels_keep_the_input_allocation() -> Result<(), Box<dyn std::error::Error>> {
    let pixels = vec![[20, 90, 240]; 8];
    let allocation = pixels.as_ptr();
    let image = BgrImage::from_bgr_pixels(4, 2, pixels)?;
    assert_eq!(image.pixels().as_ptr(), allocation);
    assert_eq!((image.width(), image.height()), (4, 2));
    Ok(())
}

#[test]
fn blank_and_tiny_inputs_have_no_regions() -> Result<(), Box<dyn std::error::Error>> {
    for (width, height) in [(320, 240), (1, 1), (1, 10), (10, 1), (3, 3)] {
        for value in [0, 30, 128, 255] {
            let image = BgrImage::solid(width, height, [value; 3])?;
            assert!(
                detect_regions(&image)?.is_empty(),
                "{width} x {height}, {value}"
            );
        }
    }
    Ok(())
}

#[test]
fn caret_is_preserved_and_source_is_unchanged() -> Result<(), Box<dyn std::error::Error>> {
    let width = 800u32;
    let height = 400u32;
    let mut pixels = vec![[245; 3]; (width * height) as usize];
    for y in 200..233 {
        for x in 400..402 {
            pixels[(y * width + x) as usize] = [0; 3];
        }
    }
    let image = BgrImage::from_bgr_pixels(width, height, pixels)?;
    let original = image.to_bgr_bytes();
    let regions = detect_regions(&image)?;
    assert!(
        regions
            .iter()
            .any(|region| iou(region.rect, Rect::new(400, 200, 2, 33)) > 0.8)
    );
    let preview = draw_regions(&image, &regions, 2)?;
    assert_eq!(image.to_bgr_bytes(), original);
    assert_ne!(preview.to_bgr_bytes(), original);
    Ok(())
}

#[cfg(feature = "opencv")]
#[test]
fn opencv_adapter_round_trips_bgr_pixels() -> Result<(), Box<dyn std::error::Error>> {
    use opencv::{core, prelude::*};
    use visual_region_detector::opencv_backend;

    let mat = core::Mat::new_rows_cols_with_default(
        3,
        4,
        core::CV_8UC3,
        core::Scalar::new(7., 19., 31., 0.),
    )?;
    let image = opencv_backend::from_mat(&mat)?;
    assert_eq!((image.width(), image.height()), (4, 3));
    assert!(image.pixels().iter().all(|pixel| *pixel == [7, 19, 31]));

    let round_trip = opencv_backend::to_mat(&image)?;
    assert_eq!(round_trip.rows(), 3);
    assert_eq!(round_trip.cols(), 4);
    assert_eq!(round_trip.data_bytes()?, mat.data_bytes()?);
    assert!(opencv_backend::detect_regions(&mat)?.is_empty());

    let regions = [visual_region_detector::Region {
        rect: Rect::new(1, 1, 2, 1),
        kind: "image".into(),
    }];
    let preview = opencv_backend::draw_regions(&mat, &regions, 1)?;
    assert_ne!(preview.data_bytes()?, mat.data_bytes()?);
    let json = opencv_backend::regions_to_jsonable("memory", &mat, &regions)?;
    assert_eq!(json["regions"][0]["type"], "image");
    assert_eq!(json["regions"][0]["x"], 1);
    assert_eq!(visual_region_detector::backend_name(), "opencv");
    Ok(())
}

#[cfg(all(feature = "opencv", feature = "file-io"))]
#[test]
fn opencv_adapter_exports_exact_png_crops() -> Result<(), Box<dyn std::error::Error>> {
    use visual_region_detector::{Region, opencv_backend};

    struct ExportDirectory(std::path::PathBuf);
    impl Drop for ExportDirectory {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }
    let directory = ExportDirectory(std::env::temp_dir().join(format!(
        "snow-region-detector-crops-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)?
            .as_nanos()
    )));
    let source = image::RgbImage::from_fn(4, 3, |x, y| {
        image::Rgb([(x * 31) as u8, (y * 47) as u8, (x + y * 4) as u8])
    });
    let mat = opencv_backend::to_mat(&BgrImage::from_rgb8(&source))?;
    let regions = [Region {
        rect: Rect::new(1, 1, 2, 2),
        kind: "image".into(),
    }];

    let crops = opencv_backend::write_crops(&mat, &regions, &directory.0)?;

    assert_eq!(crops.len(), 1);
    assert_eq!(crops[0]["file"], "0000_image.png");
    assert_eq!(
        image::open(directory.0.join("0000_image.png"))?.to_rgb8(),
        image::imageops::crop_imm(&source, 1, 1, 2, 2).to_image()
    );
    let index: serde_json::Value =
        serde_json::from_slice(&std::fs::read(directory.0.join("index.json"))?)?;
    assert_eq!(index["crops"], serde_json::json!(crops));
    Ok(())
}
