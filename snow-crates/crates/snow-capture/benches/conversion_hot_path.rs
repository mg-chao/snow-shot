use std::time::Duration;

use criterion::{BenchmarkId, Criterion, Throughput, black_box, criterion_group, criterion_main};
use half::f16;
use snow_capture::convert::{
    HdrFrameContext, SurfaceConversionOptions, SurfacePixelFormat, convert_surface_to_rgba, warmup,
};

#[derive(Clone, Copy)]
struct Scenario {
    name: &'static str,
    format: SurfacePixelFormat,
    width: usize,
    height: usize,
    src_pitch: usize,
    dst_pitch: usize,
    options: SurfaceConversionOptions,
}

fn align_up(value: usize, align: usize) -> usize {
    if align <= 1 {
        return value;
    }
    (value + align - 1) & !(align - 1)
}

fn source_bytes_per_pixel(format: SurfacePixelFormat) -> usize {
    match format {
        SurfacePixelFormat::Bgra8 | SurfacePixelFormat::Rgba8 => 4,
        SurfacePixelFormat::Rgba16Float => 8,
    }
}

fn required_surface_bytes(pitch: usize, row_bytes: usize, height: usize) -> usize {
    pitch
        .checked_mul(height.saturating_sub(1))
        .and_then(|base| base.checked_add(row_bytes))
        .expect("surface byte count overflow")
}

fn fill_source_buffer_hdr_f16(scenario: Scenario) -> Vec<u8> {
    let row_bytes = scenario.width * 8;
    let len = required_surface_bytes(scenario.src_pitch, row_bytes, scenario.height);
    let mut out = vec![0u8; len];

    let mut state = 0x9e37_79b9_7f4a_7c15_u64;
    let inv_w = if scenario.width > 1 {
        1.0 / (scenario.width - 1) as f32
    } else {
        1.0
    };
    let inv_h = if scenario.height > 1 {
        1.0 / (scenario.height - 1) as f32
    } else {
        1.0
    };

    for y in 0..scenario.height {
        let row_off = y * scenario.src_pitch;
        for x in 0..scenario.width {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
            let noise0 = ((state >> 40) & 0xFF) as f32 / 255.0;
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
            let noise1 = ((state >> 40) & 0xFF) as f32 / 255.0;
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
            let noise2 = ((state >> 40) & 0xFF) as f32 / 255.0;

            let tx = x as f32 * inv_w;
            let ty = y as f32 * inv_h;
            let base = 0.04 + 0.96 * tx;
            let mix = 0.6 * tx + 0.4 * ty;

            let hdr_boost = if ((x ^ y) & 0x3) == 0 {
                1.0 + 0.4 * mix
            } else {
                2.2 + 6.2 * mix.powf(1.3)
            };

            let r = (base + 0.08 * noise0) * hdr_boost;
            let g = (0.03 + 0.85 * mix + 0.07 * noise1) * (0.85 * hdr_boost + 0.2);
            let b = (0.02 + 0.75 * (1.0 - tx) + 0.09 * noise2) * (0.75 * hdr_boost + 0.25);

            let px_off = row_off + x * 8;
            let r_bits = f16::from_f32(r.max(0.0)).to_bits();
            let g_bits = f16::from_f32(g.max(0.0)).to_bits();
            let b_bits = f16::from_f32(b.max(0.0)).to_bits();
            let a_bits = f16::from_f32(1.0).to_bits();
            out[px_off..px_off + 2].copy_from_slice(&r_bits.to_le_bytes());
            out[px_off + 2..px_off + 4].copy_from_slice(&g_bits.to_le_bytes());
            out[px_off + 4..px_off + 6].copy_from_slice(&b_bits.to_le_bytes());
            out[px_off + 6..px_off + 8].copy_from_slice(&a_bits.to_le_bytes());
        }
    }

    out
}

fn fill_source_buffer(scenario: Scenario) -> Vec<u8> {
    if scenario.format == SurfacePixelFormat::Rgba16Float {
        return fill_source_buffer_hdr_f16(scenario);
    }

    let row_bytes = scenario.width * source_bytes_per_pixel(scenario.format);
    let len = required_surface_bytes(scenario.src_pitch, row_bytes, scenario.height);
    let mut out = vec![0u8; len];

    let mut state = 0x9e37_79b9_7f4a_7c15_u64;
    for byte in &mut out {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        *byte = (state >> 32) as u8;
    }

    out
}

fn bgra_scenarios() -> [Scenario; 3] {
    let pitched_width = 1919usize;
    let pitched_height = 1079usize;
    let pitched_src_pitch = align_up(pitched_width * 4, 256);
    [
        Scenario {
            name: "contiguous_4k",
            format: SurfacePixelFormat::Bgra8,
            width: 3840,
            height: 2160,
            src_pitch: 3840 * 4,
            dst_pitch: 3840 * 4,
            options: SurfaceConversionOptions::default(),
        },
        Scenario {
            name: "pitched_1080p",
            format: SurfacePixelFormat::Bgra8,
            width: pitched_width,
            height: pitched_height,
            src_pitch: pitched_src_pitch,
            dst_pitch: pitched_width * 4,
            options: SurfaceConversionOptions::default(),
        },
        Scenario {
            name: "pitched_1080p_opaque",
            format: SurfacePixelFormat::Bgra8,
            width: pitched_width,
            height: pitched_height,
            src_pitch: pitched_src_pitch,
            dst_pitch: pitched_width * 4,
            options: SurfaceConversionOptions {
                force_opaque_alpha: true,
                ..SurfaceConversionOptions::default()
            },
        },
    ]
}

fn hdr_scenarios() -> [Scenario; 3] {
    let hdr_lut = HdrFrameContext {
        sdr_white_nits: 160.0,
        hdr_peak_nits: 1000.0,
        tonemap_use_lut: true,
        ..HdrFrameContext::default()
    };
    let hdr_precise = HdrFrameContext {
        tonemap_use_lut: false,
        ..hdr_lut
    };
    let pitched_width = 1919usize;
    let pitched_height = 1079usize;
    let pitched_src_pitch = align_up(pitched_width * 8, 256);

    [
        Scenario {
            name: "contiguous_4k_lut",
            format: SurfacePixelFormat::Rgba16Float,
            width: 3840,
            height: 2160,
            src_pitch: 3840 * 8,
            dst_pitch: 3840 * 4,
            options: SurfaceConversionOptions {
                hdr_to_sdr: Some(hdr_lut),
                ..SurfaceConversionOptions::default()
            },
        },
        Scenario {
            name: "contiguous_4k_precise",
            format: SurfacePixelFormat::Rgba16Float,
            width: 3840,
            height: 2160,
            src_pitch: 3840 * 8,
            dst_pitch: 3840 * 4,
            options: SurfaceConversionOptions {
                hdr_to_sdr: Some(hdr_precise),
                ..SurfaceConversionOptions::default()
            },
        },
        Scenario {
            name: "pitched_1080p_lut",
            format: SurfacePixelFormat::Rgba16Float,
            width: pitched_width,
            height: pitched_height,
            src_pitch: pitched_src_pitch,
            dst_pitch: pitched_width * 4,
            options: SurfaceConversionOptions {
                hdr_to_sdr: Some(hdr_lut),
                ..SurfaceConversionOptions::default()
            },
        },
    ]
}

fn bench_surface_conversion(c: &mut Criterion) {
    warmup();

    let mut bgra_group = c.benchmark_group("surface_convert_bgra8");
    for scenario in bgra_scenarios() {
        let src = fill_source_buffer(scenario);
        let dst_len =
            required_surface_bytes(scenario.dst_pitch, scenario.width * 4, scenario.height);
        let mut dst = vec![0u8; dst_len];
        let throughput =
            (scenario.width * scenario.height * source_bytes_per_pixel(scenario.format)) as u64;
        bgra_group.throughput(Throughput::Bytes(throughput));
        bgra_group.bench_with_input(
            BenchmarkId::from_parameter(scenario.name),
            &scenario,
            |b, scenario| {
                b.iter(|| {
                    convert_surface_to_rgba(
                        scenario.format,
                        black_box(src.as_slice()),
                        scenario.src_pitch,
                        black_box(dst.as_mut_slice()),
                        scenario.dst_pitch,
                        scenario.width,
                        scenario.height,
                        scenario.options,
                    );
                    black_box(dst.as_slice());
                });
            },
        );
    }
    bgra_group.finish();

    let mut hdr_group = c.benchmark_group("surface_convert_hdr");
    for scenario in hdr_scenarios() {
        let src = fill_source_buffer(scenario);
        let dst_len =
            required_surface_bytes(scenario.dst_pitch, scenario.width * 4, scenario.height);
        let mut dst = vec![0u8; dst_len];
        let throughput =
            (scenario.width * scenario.height * source_bytes_per_pixel(scenario.format)) as u64;
        hdr_group.throughput(Throughput::Bytes(throughput));
        hdr_group.bench_with_input(
            BenchmarkId::from_parameter(scenario.name),
            &scenario,
            |b, scenario| {
                b.iter(|| {
                    convert_surface_to_rgba(
                        scenario.format,
                        black_box(src.as_slice()),
                        scenario.src_pitch,
                        black_box(dst.as_mut_slice()),
                        scenario.dst_pitch,
                        scenario.width,
                        scenario.height,
                        scenario.options,
                    );
                    black_box(dst.as_slice());
                });
            },
        );
    }
    hdr_group.finish();
}

fn bench_screen_colors(c: &mut Criterion) {
    use snow_capture::{CapturePixelFormat, color_effect::ScreenColorTransform};
    warmup();
    let mut inverted = [0.; 25];
    for i in 0..5 {
        inverted[i * 6] = if i < 3 { -1. } else { 1. };
    }
    inverted[20..23].fill(1.);
    let mut mixed = inverted;
    mixed[0] = -0.8;
    mixed[5] = 0.1;
    let mut group = c.benchmark_group("screen_colors");
    group.bench_function("query_current_matrix", |b| {
        b.iter(|| black_box(snow_capture::color_effect::ColorCorrection::snapshot_current()));
    });
    for (width, height) in [(1920, 1080), (3840, 2160)] {
        let src = vec![64; width * height * 4];
        let mut dst = vec![0; src.len()];
        for (name, matrix) in [
            ("identity", None),
            (
                "inversion",
                ScreenColorTransform::from_magnifier_matrix(&inverted),
            ),
            (
                "matrix",
                ScreenColorTransform::from_magnifier_matrix(&mixed),
            ),
        ] {
            group.bench_function(format!("{width}x{height}/{name}"), |b| {
                b.iter(|| {
                    convert_surface_to_rgba(
                        SurfacePixelFormat::Bgra8,
                        black_box(&src),
                        width * 4,
                        black_box(&mut dst),
                        width * 4,
                        width,
                        height,
                        SurfaceConversionOptions {
                            screen_color_transform: matrix,
                            force_opaque_alpha: true,
                            output_pixel_format: CapturePixelFormat::Bgra8,
                            ..Default::default()
                        },
                    );
                })
            });
        }
    }
    group.finish();
}

fn criterion_config() -> Criterion {
    Criterion::default()
        .warm_up_time(Duration::from_secs(1))
        .measurement_time(Duration::from_secs(4))
        .sample_size(20)
}

criterion_group! {
    name = benches;
    config = criterion_config();
    targets = bench_surface_conversion, bench_screen_colors
}
criterion_main!(benches);
