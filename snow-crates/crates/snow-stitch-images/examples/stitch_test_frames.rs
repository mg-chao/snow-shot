use std::{
    fs,
    path::{Path, PathBuf},
    process::ExitCode,
};

use anyhow::{Context, Result, bail, ensure};
use image::{ImageReader, RgbImage};
use snow_stitch_images::{Frame, PixelFormat, StitchError, StitchOptions, stitch_iter};

const INPUT_DIRECTORY: &str = "test-frames";
const OUTPUT_DIRECTORY: &str = "artifacts/stitch-test-frames";
const DEFAULT_FIXED_TOP_HEIGHT: u32 = 120;
const DEFAULT_FIXED_BOTTOM_HEIGHT: u32 = 225;
const DEFAULT_SLICE_HEIGHT: u32 = 500;
const BROWSER_FIXED_TOP_HEIGHT: u32 = 140;
const BROWSER_FIXED_BOTTOM_HEIGHT: u32 = 72;
const BROWSER_SLICE_HEIGHT: u32 = 600;
const SCROLL_STEP: u32 = 125;

#[derive(Clone, Copy)]
struct FixtureConfig {
    fixed_top_height: u32,
    fixed_bottom_height: u32,
    slice_height: u32,
}

const DEFAULT_CONFIG: FixtureConfig = FixtureConfig {
    fixed_top_height: DEFAULT_FIXED_TOP_HEIGHT,
    fixed_bottom_height: DEFAULT_FIXED_BOTTOM_HEIGHT,
    slice_height: DEFAULT_SLICE_HEIGHT,
};

fn fixture_config(path: &Path) -> FixtureConfig {
    let name = path.file_name().and_then(|name| name.to_str());
    match name {
        Some(
            "browser-1.png" | "browser-2.png" | "browser-3.png" | "browser-4.png" | "browser-5.png"
            | "browser-6.png" | "browser-7.png" | "browser-8.png",
        ) => FixtureConfig {
            fixed_top_height: BROWSER_FIXED_TOP_HEIGHT,
            fixed_bottom_height: BROWSER_FIXED_BOTTOM_HEIGHT,
            slice_height: BROWSER_SLICE_HEIGHT,
        },
        Some("browser-9.png") => FixtureConfig {
            fixed_top_height: 440,
            fixed_bottom_height: BROWSER_FIXED_BOTTOM_HEIGHT,
            slice_height: BROWSER_SLICE_HEIGHT,
        },
        Some("browser-10.png") => FixtureConfig {
            fixed_top_height: 234,
            fixed_bottom_height: BROWSER_FIXED_BOTTOM_HEIGHT,
            slice_height: BROWSER_SLICE_HEIGHT,
        },
        Some("test-3.png") => FixtureConfig {
            fixed_top_height: DEFAULT_FIXED_TOP_HEIGHT,
            fixed_bottom_height: 0,
            slice_height: DEFAULT_SLICE_HEIGHT,
        },
        Some("test-4.png") => FixtureConfig {
            fixed_top_height: DEFAULT_FIXED_TOP_HEIGHT,
            fixed_bottom_height: 100,
            slice_height: DEFAULT_SLICE_HEIGHT,
        },
        Some("test-5.png") => FixtureConfig {
            fixed_top_height: 180,
            fixed_bottom_height: 220,
            slice_height: DEFAULT_SLICE_HEIGHT,
        },
        Some("test-7.png") => FixtureConfig {
            fixed_top_height: 245,
            fixed_bottom_height: 0,
            slice_height: 350,
        },
        _ => DEFAULT_CONFIG,
    }
}

struct SplitFrames {
    image: RgbImage,
    row_bytes: usize,
    fixed_top_bytes: usize,
    fixed_bottom_start: usize,
    slice_bytes: usize,
    frame_height: u32,
    last_start: u32,
    next_start: Option<u32>,
}

impl SplitFrames {
    fn new(image: RgbImage, config: FixtureConfig) -> Result<Self> {
        let scrolling_height = image
            .height()
            .checked_sub(config.fixed_top_height)
            .and_then(|height| height.checked_sub(config.fixed_bottom_height))
            .context("fixed regions exceed the input image height")?;
        ensure!(
            scrolling_height >= config.slice_height,
            "scrolling area is {}px high, but the slice is {}px high",
            scrolling_height,
            config.slice_height
        );

        let row_bytes = usize::try_from(image.width())
            .ok()
            .and_then(|width| width.checked_mul(3))
            .context("row byte count overflow")?;
        let fixed_top_bytes = usize::try_from(config.fixed_top_height)
            .ok()
            .and_then(|height| height.checked_mul(row_bytes))
            .context("fixed top byte count overflow")?;
        let fixed_bottom_start = usize::try_from(
            config
                .fixed_top_height
                .checked_add(scrolling_height)
                .context("fixed bottom start overflow")?,
        )
        .ok()
        .and_then(|row| row.checked_mul(row_bytes))
        .context("fixed bottom byte offset overflow")?;
        let slice_bytes = usize::try_from(config.slice_height)
            .ok()
            .and_then(|height| height.checked_mul(row_bytes))
            .context("slice byte count overflow")?;
        let frame_height = config
            .fixed_top_height
            .checked_add(config.slice_height)
            .and_then(|height| height.checked_add(config.fixed_bottom_height))
            .context("frame height overflow")?;

        Ok(Self {
            image,
            row_bytes,
            fixed_top_bytes,
            fixed_bottom_start,
            slice_bytes,
            frame_height,
            last_start: scrolling_height - config.slice_height,
            next_start: Some(0),
        })
    }

    fn frame_at(&self, scroll_start: u32) -> Result<Frame, StitchError> {
        let source = self.image.as_raw();
        let scroll_offset = usize::try_from(scroll_start)
            .ok()
            .and_then(|row| row.checked_mul(self.row_bytes))
            .ok_or(StitchError::Arithmetic {
                operation: "calculating scrolling frame offset",
            })?;
        let scrolling_start =
            self.fixed_top_bytes
                .checked_add(scroll_offset)
                .ok_or(StitchError::Arithmetic {
                    operation: "calculating scrolling frame start",
                })?;
        let scrolling_end =
            scrolling_start
                .checked_add(self.slice_bytes)
                .ok_or(StitchError::Arithmetic {
                    operation: "calculating scrolling frame end",
                })?;
        let fixed_bottom_end = source.len();

        if scrolling_end > self.fixed_bottom_start || self.fixed_bottom_start > fixed_bottom_end {
            return Err(StitchError::InvalidFrame {
                message: "generated scrolling frame is outside its source image".to_owned(),
            });
        }

        let capacity = self
            .fixed_top_bytes
            .checked_add(self.slice_bytes)
            .and_then(|length| length.checked_add(fixed_bottom_end - self.fixed_bottom_start))
            .ok_or(StitchError::Arithmetic {
                operation: "calculating generated frame buffer length",
            })?;
        let mut pixels = Vec::with_capacity(capacity);
        pixels.extend_from_slice(&source[..self.fixed_top_bytes]);
        pixels.extend_from_slice(&source[scrolling_start..scrolling_end]);
        pixels.extend_from_slice(&source[self.fixed_bottom_start..fixed_bottom_end]);
        Frame::new(
            self.image.width(),
            self.frame_height,
            PixelFormat::Rgb8,
            pixels,
        )
    }
}

impl Iterator for SplitFrames {
    type Item = Result<Frame, StitchError>;

    fn next(&mut self) -> Option<Self::Item> {
        let scroll_start = self.next_start?;
        self.next_start = if scroll_start == self.last_start {
            None
        } else {
            Some(
                scroll_start
                    .saturating_add(SCROLL_STEP)
                    .min(self.last_start),
            )
        };
        Some(self.frame_at(scroll_start))
    }
}

fn png_paths(directory: &Path) -> Result<Vec<PathBuf>> {
    if !directory.is_dir() {
        bail!("input directory does not exist: {}", directory.display());
    }

    let mut paths = Vec::new();
    for entry in fs::read_dir(directory)
        .with_context(|| format!("could not read {}", directory.display()))?
    {
        let path = entry
            .with_context(|| format!("could not enumerate {}", directory.display()))?
            .path();
        if path.is_file()
            && path
                .extension()
                .is_some_and(|extension| extension.eq_ignore_ascii_case("png"))
        {
            paths.push(path);
        }
    }
    paths.sort();
    ensure!(
        !paths.is_empty(),
        "no PNG files found in {}",
        directory.display()
    );
    Ok(paths)
}

fn load_rgb(path: &Path) -> Result<RgbImage> {
    let mut reader =
        ImageReader::open(path).with_context(|| format!("could not open {}", path.display()))?;
    reader.no_limits();
    Ok(reader
        .decode()
        .with_context(|| format!("could not decode {}", path.display()))?
        .into_rgb8())
}

fn run() -> Result<()> {
    let input_directory = Path::new(INPUT_DIRECTORY);
    let output_directory = Path::new(OUTPUT_DIRECTORY);
    fs::create_dir_all(output_directory)
        .with_context(|| format!("could not create {}", output_directory.display()))?;

    for input_path in png_paths(input_directory)? {
        let image = load_rgb(&input_path)?;
        let frame_width = image.width();
        let frames = SplitFrames::new(image, fixture_config(&input_path))?;
        let result = stitch_iter(frames, StitchOptions::default())
            .with_context(|| format!("could not stitch {}", input_path.display()))?;
        let output_path = output_directory.join(
            input_path
                .file_name()
                .context("input path unexpectedly has no file name")?,
        );
        result
            .image
            .encode(&output_path)
            .with_context(|| format!("could not write {}", output_path.display()))?;
        println!(
            "stitched {} -> {} ({}x{})",
            input_path.display(),
            output_path.display(),
            frame_width,
            result.image.height()
        );
    }
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("error: {error:#}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use image::Rgb;

    use super::*;

    #[test]
    fn generated_frames_keep_fixed_regions_and_anchor_the_last_slice() {
        let image = RgbImage::from_fn(2, 8, |x, y| Rgb([x as u8, y as u8, 0]));
        let frames = SplitFrames::new(
            image,
            FixtureConfig {
                fixed_top_height: 1,
                fixed_bottom_height: 1,
                slice_height: 4,
            },
        )
        .unwrap()
        .collect::<Result<Vec<_>, _>>()
        .unwrap();

        assert_eq!(frames.len(), 2);
        assert_eq!(frames[0].geometry().height, 6);
        assert_eq!(frames[0].pixels()[3..6], [1, 0, 0]);
        assert_eq!(frames[0].pixels()[9..12], [1, 1, 0]);
        assert_eq!(frames[1].pixels()[9..12], [1, 3, 0]);
        assert_eq!(&frames[1].pixels()[30..36], &[0, 7, 0, 1, 7, 0]);
    }
}
