use std::ops::Range;
use std::time::{Duration, Instant};

use snow_core::timestamp::{StreamTimestamp, TickFormat, TimestampAnchor};

use crate::packet::AudioPacket;

/// A range of source PCM frames admitted to the active recording timeline.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ActivePcmSpan {
    pub source_start_frame: u64,
    pub frames: u64,
    pub timeline_start_frame: u64,
}

/// Clips PCM at precise recording-clock boundaries, reusing the caller's scratch space.
/// Sample instants belong to half-open active intervals; paused samples are never admitted.
pub fn admit_active_pcm(
    clock: &snow_core::recording_clock::RecordingClock,
    started_at: Instant,
    frames: u64,
    sample_rate: u32,
    spans: &mut Vec<ActivePcmSpan>,
) {
    spans.clear();
    if frames == 0 || sample_rate == 0 {
        return;
    }
    let duration = |frames: u64| {
        Duration::from_nanos(
            (u128::from(frames) * 1_000_000_000 / u128::from(sample_rate)).min(u128::from(u64::MAX))
                as u64,
        )
    };
    let Some(end) = started_at.checked_add(duration(frames)) else {
        return;
    };
    clock.visit_active_spans(started_at, end, |span| {
        let frame_at = |at: Instant| {
            (at.saturating_duration_since(started_at).as_nanos() * u128::from(sample_rate))
                .div_ceil(1_000_000_000)
                .min(u128::from(frames)) as u64
        };
        let first = frame_at(span.start);
        let last = frame_at(span.end);
        if first < last {
            let within_span =
                duration(first).saturating_sub(span.start.saturating_duration_since(started_at));
            spans.push(ActivePcmSpan {
                source_start_frame: first,
                frames: last - first,
                timeline_start_frame: duration_to_frames_round(
                    span.timeline_start.saturating_add(within_span),
                    sample_rate,
                ),
            });
        }
    });
}

const CLOCK_TOLERANCE_MS: u64 = 5;
const CLOCK_RESET_MS: u64 = 50;

/// Tracks the expected position of the next contiguous packet for one source.
/// The sink still owns the samples it has accepted and its writable time range.
#[derive(Default)]
pub struct PcmTimeline {
    next_frame: Option<u64>,
}

impl PcmTimeline {
    pub fn reset(&mut self) {
        self.next_frame = None;
    }

    pub(crate) fn skip_frames(&mut self, frames: u64) {
        self.next_frame = self.next_frame.map(|next| next.saturating_add(frames));
    }

    /// Place an admitted span, using capture time as a clock observation only
    /// when the backend could establish it. Clipped spans break continuity.
    pub fn place(
        &mut self,
        packet: &AudioPacket,
        span: ActivePcmSpan,
        writable: &Range<u64>,
    ) -> PcmPlacement {
        let whole_packet = span.source_start_frame == 0 && span.frames == u64::from(packet.frames);
        let expected = self
            .next_frame
            .filter(|_| whole_packet && !packet.metadata.discontinuity);
        let observed = span.timeline_start_frame;
        let trusted = !packet.metadata.capture_time_estimated;
        let frames_per_second = u64::from(packet.format.sample_rate);
        let clock_reset = expected.is_some_and(|next| {
            trusted && next.abs_diff(observed) > frames_per_second * CLOCK_RESET_MS / 1_000
        });
        let start_frame = expected
            .filter(|next| {
                !clock_reset && (writable.contains(next) || !writable.contains(&observed))
            })
            .unwrap_or(observed);
        let correction = if expected == Some(start_frame)
            && trusted
            && span.frames >= 3
            && start_frame.abs_diff(observed) > frames_per_second * CLOCK_TOLERANCE_MS / 1_000
        {
            if observed > start_frame {
                ClockCorrection::Expand
            } else {
                ClockCorrection::Contract
            }
        } else {
            ClockCorrection::None
        };
        let end_frame = start_frame.saturating_add(correction.output_frames(span.frames));
        self.next_frame = whole_packet.then_some(end_frame);
        PcmPlacement {
            start_frame,
            clock_reset,
            span,
            correction,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ClockCorrection {
    None,
    Expand,
    Contract,
}

impl ClockCorrection {
    fn output_frames(self, input_frames: u64) -> u64 {
        match self {
            Self::None => input_frames,
            Self::Expand => input_frames + 1,
            Self::Contract => input_frames - 1,
        }
    }
}

pub struct PcmPlacement {
    pub start_frame: u64,
    pub(crate) clock_reset: bool,
    span: ActivePcmSpan,
    correction: ClockCorrection,
}

impl PcmPlacement {
    pub fn samples<'a>(&self, packet: &'a AudioPacket, scratch: &'a mut Vec<i16>) -> &'a [i16] {
        let channels = usize::from(packet.format.channels);
        let start = self.span.source_start_frame as usize * channels;
        let end = start + self.span.frames as usize * channels;
        let samples = &packet.data[start..end];
        if self.correction == ClockCorrection::None {
            return samples;
        }
        interpolate_pcm(samples, channels, self.correction, scratch)
    }
}

/// Resample one whole PCM span at the rate selected by the recording clock.
fn interpolate_pcm<'a>(
    samples: &'a [i16],
    channels: usize,
    correction: ClockCorrection,
    scratch: &'a mut Vec<i16>,
) -> &'a [i16] {
    let last_input = samples.len() / channels - 1;
    let expand = correction == ClockCorrection::Expand;
    let last_output = correction.output_frames(last_input as u64) as usize;
    let denominator = last_output as i64;
    scratch.clear();
    scratch.reserve((last_output + 1) * channels);
    scratch.extend_from_slice(&samples[..channels]);

    // A one-frame expansion interpolates from the preceding frame to this one
    // with decreasing weight. Contraction uses this frame and the next one
    // with increasing weight. The endpoints are copied exactly.
    let first_offset = usize::from(expand);
    let (mut fraction, fraction_step) = if expand {
        (denominator - 1, -1)
    } else {
        (1, 1)
    };
    for output_frame in 1..last_output {
        let first = (output_frame - first_offset) * channels;
        let second = first + channels;
        for channel in 0..channels {
            let a = i64::from(samples[first + channel]);
            let b = i64::from(samples[second + channel]);
            scratch.push(((a * (denominator - fraction) + b * fraction) / denominator) as i16);
        }
        fraction += fraction_step;
    }
    scratch.extend_from_slice(&samples[last_input * channels..]);
    scratch
}

/// Stream-relative packet time range.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AudioPacketTimestamp {
    pub start: Duration,
    pub end: Duration,
}

impl AudioPacketTimestamp {
    pub fn duration(&self) -> Duration {
        self.end.saturating_sub(self.start)
    }
}

/// Alignment decision for writing one packet into a stream timeline.
///
/// Consumers should:
/// 1) write `silence_prefix_frames` frames of silence,
/// 2) skip `skip_packet_frames` from packet head,
/// 3) write the remaining `write_packet_frames` payload.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct AudioPacketAlignment {
    pub silence_prefix_frames: u64,
    pub skip_packet_frames: u64,
    pub write_packet_frames: u64,
}

impl AudioPacketAlignment {
    pub fn is_empty(&self) -> bool {
        self.write_packet_frames == 0
    }
}

/// Build a [`TimestampAnchor`] from the first audio packet.
///
/// Derives the stream origin from `packet.metadata.stream_timestamp`,
/// adjusting from packet-end to packet-start by subtracting the packet
/// duration.
pub fn audio_anchor_from_first_packet(packet: &AudioPacket) -> TimestampAnchor {
    let end_ts = packet
        .metadata
        .stream_timestamp
        .as_ref()
        .expect("stream_timestamp must be set on every AudioPacket");

    let duration = packet.duration();
    let origin_instant = end_ts
        .instant
        .checked_sub(duration)
        .unwrap_or(end_ts.instant);
    let origin_ticks = end_ts
        .raw_os_ticks
        .map(|t| t.saturating_sub(packet.duration_100ns()));

    TimestampAnchor::new(StreamTimestamp {
        instant: origin_instant,
        raw_os_ticks: origin_ticks,
        tick_format: end_ts.tick_format,
    })
}

/// Build a [`TimestampAnchor`] from a known stream origin instant.
///
/// This is the easiest way to align with `snow-capture`: pass the
/// first video frame's capture instant as the shared origin.
pub fn audio_anchor_from_origin_instant(origin_instant: Instant) -> TimestampAnchor {
    TimestampAnchor::new(StreamTimestamp {
        instant: origin_instant,
        raw_os_ticks: None,
        tick_format: TickFormat::Hns100,
    })
}

/// Build a [`TimestampAnchor`] from a known origin instant and optional QPC value (100ns units).
pub fn audio_anchor_from_origin(
    origin_instant: Instant,
    origin_qpc_100ns: Option<i64>,
) -> TimestampAnchor {
    TimestampAnchor::new(StreamTimestamp {
        instant: origin_instant,
        raw_os_ticks: origin_qpc_100ns,
        tick_format: TickFormat::Hns100,
    })
}

/// Extension trait providing audio-specific convenience methods on [`TimestampAnchor`].
pub trait AudioTimestampAnchorExt {
    /// Convert a packet into stream-relative `[start, end]` time.
    ///
    /// Uses QPC(100ns) when both anchor and packet carry QPC metadata,
    /// otherwise falls back to `Instant` deltas.
    fn audio_stream_relative(&self, packet: &AudioPacket) -> AudioPacketTimestamp;
}

impl AudioTimestampAnchorExt for TimestampAnchor {
    fn audio_stream_relative(&self, packet: &AudioPacket) -> AudioPacketTimestamp {
        let packet_duration = packet.duration();

        let end_ts = packet
            .metadata
            .stream_timestamp
            .as_ref()
            .expect("stream_timestamp must be set on every AudioPacket");

        let end = self.stream_relative(end_ts);

        AudioPacketTimestamp {
            start: end.saturating_sub(packet_duration),
            end,
        }
    }
}

/// Convert a duration into sample frames with symmetric rounding.
pub fn duration_to_frames_round(duration: Duration, sample_rate_hz: u32) -> u64 {
    let nanos = duration.as_nanos();
    let scaled = nanos.saturating_mul(u128::from(sample_rate_hz.max(1)));
    let rounded = scaled.saturating_add(500_000_000) / 1_000_000_000;
    rounded.min(u64::MAX as u128) as u64
}

/// Compute how to place one packet onto a stream timeline.
///
/// `written_frames` is the number of frames already emitted to the output
/// track before this packet is processed.
pub fn align_packet_frames(
    written_frames: u64,
    sample_rate_hz: u32,
    packet_ts: AudioPacketTimestamp,
    packet_frames: u64,
) -> AudioPacketAlignment {
    if packet_frames == 0 {
        return AudioPacketAlignment::default();
    }

    let desired_start = duration_to_frames_round(packet_ts.start, sample_rate_hz);
    let silence_prefix_frames = desired_start.saturating_sub(written_frames);
    let overlap_frames = written_frames.saturating_sub(desired_start);

    if overlap_frames >= packet_frames {
        return AudioPacketAlignment {
            silence_prefix_frames,
            skip_packet_frames: packet_frames,
            write_packet_frames: 0,
        };
    }

    AudioPacketAlignment {
        silence_prefix_frames,
        skip_packet_frames: overlap_frames,
        write_packet_frames: packet_frames.saturating_sub(overlap_frames),
    }
}

/// Pad or truncate interleaved i16 samples to exactly target duration.
pub fn align_i16_interleaved_to_duration(
    mut samples: Vec<i16>,
    sample_rate_hz: u32,
    channels: u16,
    target_duration: Duration,
) -> Vec<i16> {
    let target_frames = duration_to_frames_round(target_duration, sample_rate_hz) as usize;
    let target_samples = target_frames.saturating_mul(usize::from(channels.max(1)));
    if samples.len() < target_samples {
        samples.resize(target_samples, 0);
    } else {
        samples.truncate(target_samples);
    }
    samples
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::format::AudioFormat;
    use crate::packet::{AudioPacketMetadata, AudioSourceKind};

    #[test]
    fn clock_correction_interpolates_each_channel_and_preserves_endpoints() {
        let samples = [-1000, 900, 0, 100, 1000, -700, 2000, -1500];
        let mut packet = packet(4);
        packet.data.copy_from_slice(&samples);
        let mut timeline = PcmTimeline {
            next_frame: Some(1_000),
        };
        let mut span = ActivePcmSpan {
            source_start_frame: 0,
            frames: 4,
            timeline_start_frame: 1_241,
        };
        let mut scratch = Vec::new();
        let placement = timeline.place(&packet, span, &(0..u64::MAX));
        let expanded = placement.samples(&packet, &mut scratch);
        assert_eq!(
            expanded,
            &[-1000, 900, -250, 300, 500, -300, 1250, -900, 2000, -1500]
        );
        let scratch_ptr = scratch.as_ptr();
        span.timeline_start_frame = 764;
        let placement = timeline.place(&packet, span, &(0..u64::MAX));
        let compressed = placement.samples(&packet, &mut scratch);
        assert_eq!(compressed, &[-1000, 900, 500, -300, 2000, -1500]);
        assert_eq!(compressed.as_ptr(), scratch_ptr);

        span.timeline_start_frame = 1_248;
        let placement = timeline.place(&packet, span, &(0..u64::MAX));
        let unchanged = placement.samples(&packet, &mut scratch);
        assert_eq!(unchanged.as_ptr(), packet.data.as_ptr());
    }

    #[test]
    fn clock_correction_matches_general_interpolation_sample_for_sample() {
        let mut scratch = Vec::new();
        for frames in [3, 4, 7, 480, 960] {
            for channels in [1, 2, 6] {
                let samples: Vec<i16> = (0..frames * channels)
                    .map(|sample| (sample as u16).wrapping_mul(13_577) as i16)
                    .collect();
                for (correction, output_frames) in [
                    (ClockCorrection::Expand, frames + 1),
                    (ClockCorrection::Contract, frames - 1),
                ] {
                    let output = interpolate_pcm(&samples, channels, correction, &mut scratch);
                    assert_eq!(output.len(), output_frames * channels);
                    let denominator = output_frames - 1;
                    for (frame, actual) in output.chunks_exact(channels).enumerate() {
                        let position = frame * (frames - 1);
                        let left = position / denominator;
                        let right = (left + 1).min(frames - 1);
                        let fraction = position % denominator;
                        for (channel, &sample) in actual.iter().enumerate() {
                            let a = i64::from(samples[left * channels + channel]);
                            let b = i64::from(samples[right * channels + channel]);
                            let expected = (a * (denominator - fraction) as i64
                                + b * fraction as i64)
                                / denominator as i64;
                            assert_eq!(sample, expected as i16);
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn pcm_timeline_bounds_drift_over_two_virtual_hours() {
        const PACKETS: u64 = 2 * 60 * 60 * 100;
        let packet = packet(480);
        for packet_ns in [9_999_680, 10_000_320] {
            let mut timeline = PcmTimeline::default();
            let mut end_frame = 0;
            for index in 0..PACKETS {
                let observed = duration_to_frames_round(
                    Duration::from_nanos(index * packet_ns),
                    packet.format.sample_rate,
                );
                let span = ActivePcmSpan {
                    source_start_frame: 0,
                    frames: 480,
                    timeline_start_frame: observed,
                };
                let writable = observed.saturating_sub(4_800)..observed + 5_760;
                let placement = timeline.place(&packet, span, &writable);
                assert_eq!(placement.start_frame, end_frame);
                assert!(end_frame.abs_diff(observed) <= 241);
                end_frame += placement.correction.output_frames(span.frames);
            }
        }
    }

    #[test]
    fn pcm_timeline_reanchors_when_only_the_observation_is_writable() {
        let mut packet = packet(480);
        let writable = 191_995_200..192_006_240;
        let span = ActivePcmSpan {
            source_start_frame: 0,
            frames: 480,
            timeline_start_frame: 192_000_000,
        };
        for estimated in [false, true] {
            packet.metadata.capture_time_estimated = estimated;
            for expected in [191_990_400, 192_009_600] {
                let mut timeline = PcmTimeline {
                    next_frame: Some(expected),
                };
                let placement = timeline.place(&packet, span, &writable);
                assert_eq!(placement.start_frame, span.timeline_start_frame);
                assert_eq!(placement.correction, ClockCorrection::None);
                assert_eq!(timeline.next_frame, Some(192_000_480));
            }
        }
    }

    #[test]
    fn pcm_timeline_clipped_span_clears_continuity() {
        let packet = packet(480);
        let span = ActivePcmSpan {
            source_start_frame: 240,
            frames: 240,
            timeline_start_frame: 2_000,
        };
        let mut timeline = PcmTimeline {
            next_frame: Some(1_000),
        };
        let placement = timeline.place(&packet, span, &(0..u64::MAX));
        assert_eq!(placement.start_frame, span.timeline_start_frame);
        assert_eq!(placement.correction, ClockCorrection::None);
        assert_eq!(timeline.next_frame, None);
    }

    #[test]
    fn active_pcm_maps_multiple_boundaries_to_exact_source_and_output_frames() {
        let start = Instant::now();
        let clock = snow_core::recording_clock::RecordingClock::new(start);
        let controller = clock.controller();
        controller.mark_pause(start);
        controller.mark_resume(start + Duration::from_millis(5));
        controller.mark_pause(start + Duration::from_millis(15));
        controller.mark_resume(start + Duration::from_millis(25));
        controller.mark_pause(start + Duration::from_millis(35));
        let mut spans = Vec::with_capacity(2);
        admit_active_pcm(&clock, start, 1_920, 48_000, &mut spans);
        assert_eq!(
            spans,
            [
                ActivePcmSpan {
                    source_start_frame: 240,
                    frames: 480,
                    timeline_start_frame: 0,
                },
                ActivePcmSpan {
                    source_start_frame: 1_200,
                    frames: 480,
                    timeline_start_frame: 480,
                },
            ]
        );
        let capacity = spans.capacity();
        admit_active_pcm(
            &clock,
            start + Duration::from_millis(40),
            960,
            48_000,
            &mut spans,
        );
        assert!(spans.is_empty());
        assert_eq!(spans.capacity(), capacity);
    }

    #[test]
    fn active_pcm_uses_half_open_subsample_pause_boundaries() {
        let start = Instant::now();
        let clock = snow_core::recording_clock::RecordingClock::new(start);
        let controller = clock.controller();
        controller.mark_pause(start);
        controller.mark_resume(start + Duration::from_micros(2_501));
        controller.mark_pause(start + Duration::from_micros(7_501));
        let mut spans = Vec::new();
        admit_active_pcm(&clock, start, 480, 48_000, &mut spans);
        assert_eq!(
            spans,
            [ActivePcmSpan {
                source_start_frame: 121,
                frames: 240,
                timeline_start_frame: 1,
            }]
        );
    }

    fn packet(frames: u32) -> AudioPacket {
        let format = AudioFormat::new(48_000, 2);
        let samples = format
            .samples_for_frames(frames)
            .expect("test format should be valid");
        AudioPacket {
            source: AudioSourceKind::System,
            format,
            frames,
            data: vec![0; samples],
            metadata: AudioPacketMetadata::default(),
        }
    }

    #[test]
    fn stream_relative_prefers_qpc_when_available() {
        let anchor = audio_anchor_from_origin(Instant::now(), Some(1_000_000));

        let mut pkt = packet(480); // 10ms at 48kHz.
        pkt.metadata.set_timing(None, Some(1_500_000));

        let ts = anchor.audio_stream_relative(&pkt);
        assert_eq!(ts.end, Duration::from_millis(50));
        assert_eq!(ts.start, Duration::from_millis(40));
        assert_eq!(ts.duration(), Duration::from_millis(10));
    }

    #[test]
    fn stream_relative_falls_back_to_instant_when_qpc_absent() {
        let origin = Instant::now();
        let anchor = audio_anchor_from_origin_instant(origin);

        let mut pkt = packet(480); // 10ms.
        pkt.metadata
            .set_timing(origin.checked_add(Duration::from_millis(30)), None);

        let ts = anchor.audio_stream_relative(&pkt);
        assert_eq!(ts.end, Duration::from_millis(30));
        assert_eq!(ts.start, Duration::from_millis(20));
    }

    #[test]
    fn align_packet_frames_inserts_silence_when_packet_starts_late() {
        let plan = align_packet_frames(
            0,
            48_000,
            AudioPacketTimestamp {
                start: Duration::from_millis(20),
                end: Duration::from_millis(30),
            },
            480,
        );

        assert_eq!(plan.silence_prefix_frames, 960);
        assert_eq!(plan.skip_packet_frames, 0);
        assert_eq!(plan.write_packet_frames, 480);
    }

    #[test]
    fn align_packet_frames_trims_fully_overlapped_packet() {
        let plan = align_packet_frames(
            1_920,
            48_000,
            AudioPacketTimestamp {
                start: Duration::from_millis(20),
                end: Duration::from_millis(30),
            },
            480,
        );

        assert_eq!(plan.silence_prefix_frames, 0);
        assert_eq!(plan.skip_packet_frames, 480);
        assert_eq!(plan.write_packet_frames, 0);
        assert!(plan.is_empty());
    }

    #[test]
    fn align_i16_interleaved_to_duration_pads_or_truncates() {
        let padded =
            align_i16_interleaved_to_duration(vec![1; 20], 10, 2, Duration::from_millis(1_500));
        assert_eq!(padded.len(), 30);

        let truncated =
            align_i16_interleaved_to_duration(vec![1; 40], 10, 2, Duration::from_millis(1_500));
        assert_eq!(truncated.len(), 30);
    }
}
