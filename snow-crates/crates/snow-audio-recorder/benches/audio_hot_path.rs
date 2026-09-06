use std::time::Duration;

use criterion::{
    BatchSize, BenchmarkId, Criterion, Throughput, black_box, criterion_group, criterion_main,
};
use snow_audio_recorder::AudioSourceKind;
use snow_audio_recorder::benchmark::{
    AccumulatorBenchHarness, BenchSampleFormat, ConverterBenchHarness, make_f32_bytes,
    make_i16_bytes, make_sine_f32_samples, make_sine_i16_samples,
};

fn bench_converter(c: &mut Criterion) {
    let mut group = c.benchmark_group("converter");

    let stereo_48k_20ms_frames = 960u32;
    let stereo_i16_samples = make_sine_i16_samples(stereo_48k_20ms_frames, 2, 440.0, 48_000);
    let stereo_i16_bytes = make_i16_bytes(&stereo_i16_samples);
    group.throughput(Throughput::Bytes(stereo_i16_bytes.len() as u64));
    group.bench_with_input(
        BenchmarkId::new("i16_passthrough_48k_stereo_20ms", stereo_48k_20ms_frames),
        &stereo_i16_bytes,
        |b, input| {
            b.iter_batched(
                || {
                    ConverterBenchHarness::new(48_000, 2, BenchSampleFormat::I16, 48_000, 2)
                        .expect("converter harness")
                },
                |mut harness| {
                    let out = harness
                        .convert(black_box(input.as_slice()), stereo_48k_20ms_frames)
                        .expect("conversion should succeed");
                    black_box(out);
                },
                BatchSize::SmallInput,
            );
        },
    );

    let stereo_f32_samples = make_sine_f32_samples(stereo_48k_20ms_frames, 2, 440.0, 48_000);
    let stereo_f32_bytes = make_f32_bytes(&stereo_f32_samples);
    group.throughput(Throughput::Bytes(stereo_f32_bytes.len() as u64));
    group.bench_with_input(
        BenchmarkId::new("f32_quantize_48k_stereo_20ms", stereo_48k_20ms_frames),
        &stereo_f32_bytes,
        |b, input| {
            b.iter_batched(
                || {
                    ConverterBenchHarness::new(48_000, 2, BenchSampleFormat::F32, 48_000, 2)
                        .expect("converter harness")
                },
                |mut harness| {
                    let out = harness
                        .convert(black_box(input.as_slice()), 882)
                        .expect("conversion should succeed");
                    black_box(out);
                },
                BatchSize::SmallInput,
            );
        },
    );

    let stereo_44k_frames = 882u32;
    let stereo_44k_samples = make_sine_i16_samples(stereo_44k_frames, 2, 440.0, 44_100);
    let stereo_44k_bytes = make_i16_bytes(&stereo_44k_samples);
    group.throughput(Throughput::Bytes(stereo_44k_bytes.len() as u64));
    group.bench_with_input(
        BenchmarkId::new("i16_resample_44k1_to_48k_stereo_20ms", stereo_44k_frames),
        &stereo_44k_bytes,
        |b, input| {
            b.iter_batched(
                || {
                    ConverterBenchHarness::new(44_100, 2, BenchSampleFormat::I16, 48_000, 2)
                        .expect("converter harness")
                },
                |mut harness| {
                    let out = harness
                        .convert(black_box(input.as_slice()), stereo_48k_20ms_frames)
                        .expect("conversion should succeed");
                    black_box(out);
                },
                BatchSize::SmallInput,
            );
        },
    );

    group.finish();
}

fn bench_accumulator(c: &mut Criterion) {
    let mut group = c.benchmark_group("accumulator");

    let packet_duration = Duration::from_millis(20);
    let frames = 960u32;
    let samples = make_sine_i16_samples(frames, 2, 440.0, 48_000);

    group.throughput(Throughput::Elements(frames as u64));
    group.bench_function("exact_packet_passthrough_48k_stereo_20ms", |b| {
        b.iter_batched(
            || {
                (
                    AccumulatorBenchHarness::new(
                        AudioSourceKind::System,
                        48_000,
                        2,
                        packet_duration,
                    )
                    .expect("accumulator harness"),
                    samples.clone(),
                )
            },
            |(mut harness, chunk)| {
                let packets = harness
                    .push_chunk(black_box(chunk), frames)
                    .expect("accumulation should succeed");
                black_box(packets);
            },
            BatchSize::SmallInput,
        );
    });

    let half_frames = frames / 2;
    let half_samples = make_sine_i16_samples(half_frames, 2, 440.0, 48_000);
    group.bench_function("split_packet_two_half_chunks_48k_stereo_20ms", |b| {
        b.iter_batched(
            || {
                (
                    AccumulatorBenchHarness::new(
                        AudioSourceKind::System,
                        48_000,
                        2,
                        packet_duration,
                    )
                    .expect("accumulator harness"),
                    half_samples.clone(),
                )
            },
            |(mut harness, chunk)| {
                let first = harness
                    .push_chunk(black_box(chunk.clone()), half_frames)
                    .expect("first half should succeed");
                let second = harness
                    .push_chunk(black_box(chunk), half_frames)
                    .expect("second half should succeed");
                black_box((first, second));
            },
            BatchSize::SmallInput,
        );
    });

    group.finish();
}

criterion_group!(benches, bench_converter, bench_accumulator);
criterion_main!(benches);
