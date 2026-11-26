//! Developer-only measurements using the same model preparation and output path as the worker.
use crate::{
    TextDetectionProcessing,
    engine::{InputImage, make_engine_with_policy},
};
use simd_paddle_ocr_rs::detector::DetResizePolicy;
use snow_memory::RasterBuffer;
use std::{
    ffi::OsString,
    io::{self, Read},
    path::Path,
    time::Instant,
};

pub(super) fn run(args: &[OsString]) -> io::Result<()> {
    let [
        policy,
        stage,
        backend,
        processing,
        detector,
        recognizer,
        dictionary,
        size,
        file,
        repeats,
    ] = args
    else {
        return Err(invalid(
            "expected policy stage backend mode detector recognizer dictionary WIDTHxHEIGHT BGR-file repeats",
        ));
    };
    let policy = match policy.to_str() {
        Some("native") => DetResizePolicy::Native,
        Some("baseline") => DetResizePolicy::Min,
        _ => return Err(invalid("expected native or baseline policy")),
    };
    let detect_only = match stage.to_str() {
        Some("detect") => true,
        Some("ocr") => false,
        _ => return Err(invalid("expected detect or ocr stage")),
    };
    let gpu = match backend.to_str() {
        Some("cpu") => None,
        Some("gpu") => Some(crate::acceleration::initialize().map_err(io::Error::other)?),
        _ => return Err(invalid("expected cpu or gpu backend")),
    };
    let processing = match processing.to_str() {
        Some("accuracy") => TextDetectionProcessing::AccuracyFirst,
        Some("speed") => TextDetectionProcessing::SpeedFirst,
        _ => return Err(invalid("expected accuracy or speed mode")),
    };
    let (width, height) = size
        .to_str()
        .and_then(|s| s.split_once('x'))
        .ok_or_else(|| invalid("expected WIDTHxHEIGHT"))?;
    let (width, height) = (number(width)?, number(height)?);
    let length = width
        .checked_mul(height)
        .and_then(|n| n.checked_mul(3))
        .filter(|&n| n > 0 && n <= 3840 * 2160 * 3)
        .ok_or_else(|| invalid("invalid input size"))?;
    let mut pixels = RasterBuffer::try_zeroed(length)?;
    let mut file = std::fs::File::open(file)?;
    if file.metadata()?.len() != length as u64 {
        return Err(invalid("BGR file size mismatch"));
    }
    file.read_exact(&mut pixels)?;
    let input = InputImage {
        width,
        height,
        pixels,
    };
    let repeats = number(repeats.to_str().unwrap_or_default())?;
    if !(1..=100).contains(&repeats) {
        return Err(invalid("expected 1 to 100 repetitions"));
    }

    let mut engine = make_engine_with_policy(
        Path::new(detector),
        Path::new(recognizer),
        Path::new(dictionary),
        num_cpus::get_physical(),
        processing,
        gpu.clone(),
        policy,
    )
    .map_err(io::Error::other)?;
    let mut detector = if detect_only {
        Some(engine.detector_for_validation().map_err(io::Error::other)?)
    } else {
        None
    };
    let mut durations = Vec::with_capacity(repeats);
    let mut output = serde_json::Value::Null;
    for i in 0..=repeats {
        let started = Instant::now();
        let (elapsed, actual_gpu) = if let Some(detector) = &mut detector {
            let result = detector
                .detect(input.view().map_err(io::Error::other)?)
                .map_err(io::Error::other)?;
            let elapsed = started.elapsed();
            if i == repeats {
                output = serde_json::json!({
                    "shape": [result.resized_width, result.resized_height],
                    "boxes": result.boxes,
                });
            }
            (elapsed, detector.uses_gpu())
        } else {
            let lines = engine.run(&input).map_err(io::Error::other)?;
            let elapsed = started.elapsed();
            if i == repeats {
                output = serde_json::json!(
                    lines
                        .iter()
                        .map(|line| serde_json::json!({
                            "text": line.text, "points": line.points, "score": line.score,
                        }))
                        .collect::<Vec<_>>()
                );
            }
            (elapsed, engine.detected_on_gpu())
        };
        if actual_gpu != gpu.is_some() {
            return Err(io::Error::other("requested detector backend fell back"));
        }
        if i > 0 {
            durations.push(elapsed.as_secs_f64() * 1000.);
        }
    }
    #[cfg(feature = "gpu")]
    let device_peak = gpu.map(|context| context.memory_statistics().peak_bytes);
    #[cfg(not(feature = "gpu"))]
    let device_peak: Option<usize> = None;
    println!(
        "{}",
        serde_json::json!({
            "ms": durations,
            "peak_resident_bytes": peak_resident_bytes(),
            "peak_device_bytes": device_peak,
            "result": output
        })
    );
    Ok(())
}

fn number(value: &str) -> io::Result<usize> {
    value
        .parse()
        .map_err(|_| invalid("invalid numeric argument"))
}

fn invalid(message: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}

#[cfg(windows)]
fn peak_resident_bytes() -> Option<usize> {
    #[repr(C)]
    struct MemoryCounters {
        size: u32,
        page_faults: u32,
        peak_working_set: usize,
        working_set: usize,
        peak_paged_pool: usize,
        paged_pool: usize,
        peak_nonpaged_pool: usize,
        nonpaged_pool: usize,
        pagefile: usize,
        peak_pagefile: usize,
    }

    unsafe extern "system" {
        fn GetCurrentProcess() -> *mut std::ffi::c_void;
        fn K32GetProcessMemoryInfo(
            process: *mut std::ffi::c_void,
            counters: *mut MemoryCounters,
            bytes: u32,
        ) -> i32;
    }
    let mut counters: MemoryCounters = unsafe { std::mem::zeroed() };
    counters.size = size_of::<MemoryCounters>() as u32;
    let ok = unsafe { K32GetProcessMemoryInfo(GetCurrentProcess(), &mut counters, counters.size) };
    (ok != 0).then_some(counters.peak_working_set)
}

#[cfg(not(windows))]
fn peak_resident_bytes() -> Option<usize> {
    None
}
