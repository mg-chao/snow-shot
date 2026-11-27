use std::collections::VecDeque;
use std::mem::{size_of, size_of_val};
use std::ptr;
use std::slice;
use std::time::{Duration, Instant};

use windows::Win32::Foundation::S_OK;
use windows::Win32::Media::Audio::{
    AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY, AUDCLNT_BUFFERFLAGS_SILENT,
    AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR, AUDCLNT_E_UNSUPPORTED_FORMAT, AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
    AUDCLNT_STREAMFLAGS_LOOPBACK, AUDCLNT_STREAMFLAGS_NOPERSIST,
    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, IAudioCaptureClient, IAudioClient,
    IMMDeviceEnumerator, WAVE_FORMAT_PCM, WAVEFORMATEX, WAVEFORMATEXTENSIBLE,
    WAVEFORMATEXTENSIBLE_0,
};
use windows::Win32::Media::KernelStreaming::{KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE};
use windows::Win32::System::Com::{CLSCTX_ALL, CoTaskMemFree};
use windows::Win32::System::Performance::{QueryPerformanceCounter, QueryPerformanceFrequency};

use crate::device::{DeviceFlow, DeviceSelector};
use crate::error::{AudioError, AudioResult};
use crate::format::AudioFormat;
use crate::packet::{
    AudioPacket, AudioPacketMetadata, AudioSourceKind, frames_to_100ns, frames_to_duration,
};
use crate::session::SourceConfig;

use super::com::EventHandle;
use super::convert::{AudioConverter, NativeAudioFormat, NativeSampleFormat};
use super::device_enum;
use super::hresult::map_hresult;

const IEEE_FLOAT_FORMAT_TAG: u16 = 3;
const KSDATAFORMAT_SUBTYPE_IEEE_FLOAT: windows::core::GUID =
    windows::core::GUID::from_u128(0x00000003_0000_0010_8000_00aa00389b71);

struct SourceRuntime {
    device_id: String,
    audio_client: IAudioClient,
    capture_client: IAudioCaptureClient,
    native_format: NativeAudioFormat,
}

#[derive(Clone, Debug, Default)]
pub(crate) struct PendingMetadata {
    capture_time: Option<Instant>,
    qpc_position_100ns: Option<i64>,
    device_position_frames: Option<u64>,
    /// Native source-frame span represented by the buffered output chunk.
    /// These fields are used only while slicing a resampled chunk; they are
    /// intentionally not exposed on `AudioPacketMetadata`.
    native_frames: Option<u32>,
    native_sample_rate: Option<u32>,
    discontinuity: bool,
    is_silent: bool,
}

struct BufferedChunk {
    data: Vec<i16>,
    frames: u32,
    consumed_frames: u32,
    metadata: PendingMetadata,
}

impl BufferedChunk {
    fn remaining_frames(&self) -> u32 {
        self.frames.saturating_sub(self.consumed_frames)
    }

    fn is_depleted(&self) -> bool {
        self.consumed_frames >= self.frames
    }

    fn append_prefix_to(
        &mut self,
        frames: u32,
        channels: u16,
        sample_rate: u32,
        packet_data: &mut Vec<i16>,
        packet_meta: &mut Option<PendingMetadata>,
    ) -> AudioResult<()> {
        if frames == 0 {
            return Ok(());
        }

        let start = (self.consumed_frames as usize)
            .checked_mul(usize::from(channels))
            .ok_or(AudioError::BufferOverflow)?;
        let sample_count = (frames as usize)
            .checked_mul(usize::from(channels))
            .ok_or(AudioError::BufferOverflow)?;
        let end = start
            .checked_add(sample_count)
            .ok_or(AudioError::BufferOverflow)?;
        if end > self.data.len() {
            return Err(AudioError::BufferOverflow);
        }

        packet_data.extend_from_slice(&self.data[start..end]);
        let slice_meta = self.slice_end_metadata(frames, sample_rate)?;
        merge_pending_meta(packet_meta, slice_meta);
        self.consumed_frames = self
            .consumed_frames
            .checked_add(frames)
            .ok_or(AudioError::BufferOverflow)?;
        Ok(())
    }

    fn slice_end_metadata(
        &self,
        consumed_slice_frames: u32,
        sample_rate: u32,
    ) -> AudioResult<PendingMetadata> {
        let slice_end_frame = self
            .consumed_frames
            .checked_add(consumed_slice_frames)
            .ok_or(AudioError::BufferOverflow)?;
        if slice_end_frame > self.frames {
            return Err(AudioError::BufferOverflow);
        }

        let output_tail_frames = self.frames.saturating_sub(slice_end_frame);
        let (tail_duration, tail_100ns, tail_device_frames) = match (
            self.metadata.native_frames,
            self.metadata.native_sample_rate,
        ) {
            (Some(native_frames), Some(native_sample_rate)) if self.frames > 0 => {
                let native_tail_frames =
                    scale_output_frames_to_native(output_tail_frames, self.frames, native_frames);
                (
                    frames_to_duration(native_tail_frames, native_sample_rate),
                    frames_to_100ns(native_tail_frames, native_sample_rate),
                    u64::from(native_tail_frames),
                )
            }
            _ => (
                frames_to_duration(output_tail_frames, sample_rate),
                frames_to_100ns(output_tail_frames, sample_rate),
                u64::from(output_tail_frames),
            ),
        };

        Ok(PendingMetadata {
            capture_time: self
                .metadata
                .capture_time
                .map(|end| end.checked_sub(tail_duration).unwrap_or(end)),
            qpc_position_100ns: self
                .metadata
                .qpc_position_100ns
                .map(|end| end.saturating_sub(tail_100ns)),
            device_position_frames: self
                .metadata
                .device_position_frames
                .map(|end| end.saturating_sub(tail_device_frames)),
            // WASAPI discontinuity marks the first sample after a gap, so it
            // only applies to the first slice emitted from this chunk.
            discontinuity: self.metadata.discontinuity && self.consumed_frames == 0,
            is_silent: self.metadata.is_silent,
            native_frames: None,
            native_sample_rate: None,
        })
    }
}

fn scale_output_frames_to_native(
    output_frames: u32,
    output_total_frames: u32,
    native_total_frames: u32,
) -> u32 {
    if output_total_frames == 0 {
        return 0;
    }
    let numerator = u128::from(output_frames).saturating_mul(u128::from(native_total_frames));
    let rounded = numerator.saturating_add(u128::from(output_total_frames / 2))
        / u128::from(output_total_frames);
    rounded.min(u128::from(u32::MAX)) as u32
}

pub(crate) struct PacketAccumulator {
    source: AudioSourceKind,
    format: AudioFormat,
    target_frames: u32,
    chunks: VecDeque<BufferedChunk>,
    buffered_frames: u32,
}

impl PacketAccumulator {
    pub(crate) fn new(
        source: AudioSourceKind,
        format: AudioFormat,
        packet_duration: std::time::Duration,
    ) -> AudioResult<Self> {
        let mut target_frames = (format.sample_rate as u128)
            .checked_mul(packet_duration.as_nanos())
            .and_then(|value| value.checked_div(1_000_000_000))
            .ok_or(AudioError::BufferOverflow)? as u32;
        if target_frames == 0 {
            target_frames = 1;
        }

        Ok(Self {
            source,
            format,
            target_frames,
            chunks: VecDeque::new(),
            buffered_frames: 0,
        })
    }

    pub(crate) fn push_chunk(
        &mut self,
        data: Vec<i16>,
        frames: u32,
        meta: PendingMetadata,
        sequence: &mut u64,
    ) -> AudioResult<Vec<AudioPacket>> {
        if frames == 0 || data.is_empty() {
            return Ok(Vec::new());
        }

        let expected_samples = self.format.samples_for_frames(frames)?;
        if data.len() != expected_samples {
            return Err(AudioError::BufferOverflow);
        }

        let mut output = Vec::new();
        if meta.discontinuity && self.buffered_frames > 0 {
            // A discontinuity describes the first frame of this chunk. Flush
            // any older partial packet before enqueueing it so the writer can
            // align the post-gap packet as a whole.
            output.push(self.build_packet(self.buffered_frames, sequence)?);
        }

        if self.buffered_frames == 0 && self.chunks.is_empty() && frames == self.target_frames {
            output.push(self.build_packet_from_samples(data, frames, meta, sequence));
            return Ok(output);
        }

        self.chunks.push_back(BufferedChunk {
            data,
            frames,
            consumed_frames: 0,
            metadata: meta,
        });
        self.buffered_frames = self
            .buffered_frames
            .checked_add(frames)
            .ok_or(AudioError::BufferOverflow)?;

        while self.buffered_frames >= self.target_frames {
            output.push(self.build_packet(self.target_frames, sequence)?);
        }

        Ok(output)
    }

    fn build_packet_from_samples(
        &self,
        data: Vec<i16>,
        frames: u32,
        pending: PendingMetadata,
        sequence: &mut u64,
    ) -> AudioPacket {
        *sequence = sequence.wrapping_add(1);

        AudioPacket {
            source: self.source,
            format: self.format,
            frames,
            data,
            metadata: {
                let mut meta = AudioPacketMetadata {
                    device_position_frames: pending.device_position_frames,
                    discontinuity: pending.discontinuity,
                    is_silent: pending.is_silent,
                    sequence: *sequence,
                    capture_time_estimated: pending.qpc_position_100ns.is_none(),
                    ..Default::default()
                };
                meta.set_timing(pending.capture_time, pending.qpc_position_100ns);
                meta
            },
        }
    }

    fn build_packet(&mut self, packet_frames: u32, sequence: &mut u64) -> AudioResult<AudioPacket> {
        let packet_sample_count = self.format.samples_for_frames(packet_frames)?;

        let mut data = Vec::with_capacity(packet_sample_count);
        let mut packet_meta: Option<PendingMetadata> = None;
        let mut remaining_frames = packet_frames;

        while remaining_frames > 0 {
            let drop_chunk = {
                let chunk = self.chunks.front_mut().ok_or(AudioError::BufferOverflow)?;
                let available = chunk.remaining_frames();
                if available == 0 {
                    true
                } else {
                    let take = available.min(remaining_frames);
                    chunk.append_prefix_to(
                        take,
                        self.format.channels,
                        self.format.sample_rate,
                        &mut data,
                        &mut packet_meta,
                    )?;
                    remaining_frames = remaining_frames.saturating_sub(take);
                    chunk.is_depleted()
                }
            };
            if drop_chunk {
                let _ = self.chunks.pop_front();
            }
        }

        if data.len() != packet_sample_count {
            return Err(AudioError::BufferOverflow);
        }

        self.buffered_frames = self
            .buffered_frames
            .checked_sub(packet_frames)
            .ok_or(AudioError::BufferOverflow)?;
        *sequence = sequence.wrapping_add(1);

        let pending = packet_meta.unwrap_or_else(|| PendingMetadata {
            is_silent: true,
            ..Default::default()
        });

        Ok(self.build_packet_from_samples(data, packet_frames, pending, sequence))
    }
}

fn merge_pending_meta(slot: &mut Option<PendingMetadata>, incoming: PendingMetadata) {
    match slot {
        Some(existing) => {
            // All positions describe the last slice, including when its QPC is invalid.
            existing.capture_time = incoming.capture_time;
            existing.qpc_position_100ns = incoming.qpc_position_100ns;
            existing.device_position_frames = incoming.device_position_frames;
            existing.discontinuity |= incoming.discontinuity;
            existing.is_silent &= incoming.is_silent;
        }
        None => {
            *slot = Some(incoming);
        }
    }
}

pub(crate) struct WasapiSource {
    config: SourceConfig,
    event: EventHandle,
    runtime: SourceRuntime,
    converter: AudioConverter,
    accumulator: PacketAccumulator,
    clock: WasapiClock,
    sequence: u64,
    silence_scratch: Vec<u8>,
}

#[derive(Default)]
struct WasapiClock {
    // Rust's Windows Instant and WASAPI both use QPC. Calibrate their epochs once.
    anchor: Option<(Instant, i64)>,
    last_qpc_start: Option<i64>,
}

impl WasapiClock {
    fn new() -> Self {
        let mut frequency = 0;
        if unsafe { QueryPerformanceFrequency(&mut frequency) }.is_err() || frequency <= 0 {
            return Self::default();
        }

        // Prefer the tightest bracket in case the thread is preempted during calibration.
        let anchor = (0..3)
            .filter_map(|_| {
                let before = Instant::now();
                let mut ticks = 0;
                unsafe { QueryPerformanceCounter(&mut ticks) }.ok()?;
                let after = Instant::now();
                let span = after.duration_since(before);
                let qpc =
                    i64::try_from(i128::from(ticks) * 10_000_000 / i128::from(frequency)).ok()?;
                Some((span, (before + span / 2, qpc)))
            })
            .min_by_key(|&(span, _)| span)
            .map(|(_, anchor)| anchor);
        Self {
            anchor,
            ..Self::default()
        }
    }

    fn capture_instant(&self, position: i64) -> Option<Instant> {
        let (instant, anchor) = self.anchor?;
        let offset = Duration::from_nanos(anchor.abs_diff(position).checked_mul(100)?);
        if position >= anchor {
            instant.checked_add(offset)
        } else {
            instant.checked_sub(offset)
        }
    }

    fn packet_end(
        &mut self,
        read_at: Instant,
        qpc_start: u64,
        duration: Duration,
    ) -> (Instant, Option<i64>) {
        if let Ok(position) = i64::try_from(qpc_start)
            && position > 0
            && self.last_qpc_start.is_none_or(|last| position > last)
            && let Some(start) = self.capture_instant(position)
            && start <= read_at
            && let Some(end) = start.checked_add(duration)
            && let Some(qpc_end) = position.checked_add((duration.as_nanos() / 100) as i64)
        {
            self.last_qpc_start = Some(position);
            return (end, Some(qpc_end));
        }

        (read_at, None)
    }
}

/// Normalize the first-frame WASAPI positions to the end of the native chunk
/// so a resampled or split output packet keeps its position on the capture clock.
fn pending_metadata_for_wasapi_chunk(
    clock: &mut WasapiClock,
    read_at: Instant,
    qpc_position_100ns: u64,
    device_position_frames: u64,
    native_frames: u32,
    native_sample_rate: u32,
    flags: u32,
) -> PendingMetadata {
    let timestamp_valid = flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR.0 as u32 == 0;
    let discontinuity = flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY.0 as u32 != 0;
    let duration = frames_to_duration(native_frames, native_sample_rate);
    let (end, qpc_position_100ns) = clock.packet_end(
        read_at,
        if timestamp_valid {
            qpc_position_100ns
        } else {
            0
        },
        duration,
    );

    PendingMetadata {
        capture_time: Some(end),
        qpc_position_100ns,
        device_position_frames: timestamp_valid
            .then(|| device_position_frames.saturating_add(u64::from(native_frames))),
        native_frames: Some(native_frames),
        native_sample_rate: Some(native_sample_rate),
        discontinuity,
        is_silent: flags & AUDCLNT_BUFFERFLAGS_SILENT.0 as u32 != 0,
    }
}

fn estimate_batch_times(packets: &mut [AudioPacket]) {
    // A single wake can drain several native packets. Walk continuous runs
    // backward from the next packet's time or the final packet's read time.
    // An earlier packet was already available at its own read time.
    let mut next_start = None;
    for packet in packets.iter_mut().rev() {
        if packet.metadata.capture_time_estimated
            && let Some(end) = next_start
            && packet
                .end_capture_time()
                .is_some_and(|read_end| end < read_end)
        {
            packet.metadata.set_timing(Some(end), None);
        }
        next_start = if packet.metadata.discontinuity {
            None
        } else {
            packet.start_capture_time()
        };
    }
}

impl WasapiSource {
    pub fn new(
        kind: AudioSourceKind,
        config: SourceConfig,
        enumerator: IMMDeviceEnumerator,
    ) -> AudioResult<Self> {
        let flow = match kind {
            AudioSourceKind::System => DeviceFlow::Render,
            AudioSourceKind::Microphone => DeviceFlow::Capture,
        };

        let event = EventHandle::new_auto_reset(false)?;
        let selector = config.device.clone();
        let (runtime, converter) =
            init_runtime(kind, flow, &selector, &config, &enumerator, event.raw())?;

        let accumulator =
            PacketAccumulator::new(kind, config.output_format, config.packet_duration)?;

        Ok(Self {
            config,
            event,
            runtime,
            converter,
            accumulator,
            clock: WasapiClock::new(),
            sequence: 0,
            silence_scratch: Vec::new(),
        })
    }

    pub fn current_device_id(&self) -> &str {
        &self.runtime.device_id
    }

    pub fn event_handle(&self) -> windows::Win32::Foundation::HANDLE {
        self.event.raw()
    }

    pub fn drain_packets(&mut self) -> AudioResult<Vec<AudioPacket>> {
        let mut packets = Vec::new();
        loop {
            let next = unsafe { self.runtime.capture_client.GetNextPacketSize() }
                .map_err(|err| map_hresult(err.code(), "IAudioCaptureClient::GetNextPacketSize"))?;

            if next == 0 {
                break;
            }

            let mut data_ptr = ptr::null_mut::<u8>();
            let mut frames = 0u32;
            let mut flags = 0u32;
            let mut device_position = 0u64;
            let mut qpc_position = 0u64;

            unsafe {
                self.runtime.capture_client.GetBuffer(
                    &mut data_ptr,
                    &mut frames,
                    &mut flags,
                    Some(&mut device_position),
                    Some(&mut qpc_position),
                )
            }
            .map_err(|err| map_hresult(err.code(), "IAudioCaptureClient::GetBuffer"))?;

            let read_at = Instant::now();
            let process_result = (|| {
                let native_bytes_per_frame = self.runtime.native_format.bytes_per_frame()?;
                let byte_count = native_bytes_per_frame
                    .checked_mul(frames as usize)
                    .ok_or(AudioError::BufferOverflow)?;

                let is_silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT.0 as u32) != 0;

                let converted = if is_silent {
                    self.silence_scratch.resize(byte_count, 0);
                    self.converter.convert_chunk(&self.silence_scratch, frames)
                } else {
                    if data_ptr.is_null() {
                        return Err(AudioError::platform(anyhow::anyhow!(
                            "WASAPI returned null audio buffer pointer"
                        )));
                    }
                    let input = unsafe { slice::from_raw_parts(data_ptr, byte_count) };
                    self.converter.convert_chunk(input, frames)
                }?;

                if converted.is_empty() {
                    return Ok(Vec::new());
                }

                let out_channels = usize::from(self.config.output_format.channels.max(1));
                if converted.len() % out_channels != 0 {
                    return Err(AudioError::BufferOverflow);
                }
                let out_frames = (converted.len() / out_channels) as u32;

                self.accumulator.push_chunk(
                    converted,
                    out_frames,
                    pending_metadata_for_wasapi_chunk(
                        &mut self.clock,
                        read_at,
                        qpc_position,
                        device_position,
                        frames,
                        self.runtime.native_format.sample_rate,
                        flags,
                    ),
                    &mut self.sequence,
                )
            })();

            unsafe {
                self.runtime
                    .capture_client
                    .ReleaseBuffer(frames)
                    .map_err(|err| map_hresult(err.code(), "IAudioCaptureClient::ReleaseBuffer"))?;
            }

            packets.extend(process_result?);
        }

        estimate_batch_times(&mut packets);
        Ok(packets)
    }
}

impl Drop for WasapiSource {
    fn drop(&mut self) {
        unsafe {
            let _ = self.runtime.audio_client.Stop();
        }
    }
}

fn init_runtime(
    kind: AudioSourceKind,
    flow: DeviceFlow,
    selector: &DeviceSelector,
    config: &SourceConfig,
    enumerator: &IMMDeviceEnumerator,
    event_handle: windows::Win32::Foundation::HANDLE,
) -> AudioResult<(SourceRuntime, AudioConverter)> {
    let device = device_enum::resolve_device(enumerator, selector, flow)?;
    let device_id = device_enum::device_id(&device)?;
    let audio_client: IAudioClient = unsafe { device.Activate(CLSCTX_ALL, None) }
        .map_err(|err| map_hresult(err.code(), "IMMDevice::Activate(IAudioClient)"))?;

    let (selected_format, native_format) = if kind == AudioSourceKind::System {
        select_loopback_stream_format(&audio_client)?
    } else {
        select_stream_format(&audio_client, config.output_format)?
    };

    let duration_hns = duration_to_hns(config.packet_duration)?;
    let mut stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;

    if kind == AudioSourceKind::System {
        stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    } else {
        stream_flags |=
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    }

    let format_ptr = selected_format.as_ptr() as *const WAVEFORMATEX;

    unsafe {
        audio_client.Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            stream_flags,
            duration_hns,
            0,
            format_ptr,
            None,
        )
    }
    .map_err(|err| map_hresult(err.code(), "IAudioClient::Initialize"))?;

    unsafe { audio_client.SetEventHandle(event_handle) }
        .map_err(|err| map_hresult(err.code(), "IAudioClient::SetEventHandle"))?;

    let capture_client: IAudioCaptureClient = unsafe { audio_client.GetService() }
        .map_err(|err| map_hresult(err.code(), "IAudioClient::GetService(IAudioCaptureClient)"))?;

    unsafe { audio_client.Start() }
        .map_err(|err| map_hresult(err.code(), "IAudioClient::Start"))?;

    let converter = AudioConverter::new(native_format, config.output_format)?;

    Ok((
        SourceRuntime {
            device_id,
            audio_client,
            capture_client,
            native_format,
        },
        converter,
    ))
}

fn duration_to_hns(duration: std::time::Duration) -> AudioResult<i64> {
    let nanos = duration.as_nanos();
    let hns = nanos.checked_div(100).ok_or(AudioError::BufferOverflow)?;

    if hns > i64::MAX as u128 {
        return Err(AudioError::BufferOverflow);
    }

    Ok(hns as i64)
}

fn select_stream_format(
    audio_client: &IAudioClient,
    output: AudioFormat,
) -> AudioResult<(Vec<u8>, NativeAudioFormat)> {
    let requested = build_requested_wave_format(output)?;
    let requested_ptr = requested.as_ptr() as *const WAVEFORMATEX;

    let mut closest_ptr: *mut WAVEFORMATEX = ptr::null_mut();
    let support_hr = unsafe {
        audio_client.IsFormatSupported(
            AUDCLNT_SHAREMODE_SHARED,
            requested_ptr,
            Some(&mut closest_ptr),
        )
    };

    let selected = if support_hr == S_OK {
        requested
    } else if support_hr.is_ok() && !closest_ptr.is_null() {
        copy_wave_format(closest_ptr)?
    } else if support_hr == AUDCLNT_E_UNSUPPORTED_FORMAT {
        get_mix_format(audio_client)?
    } else {
        if !closest_ptr.is_null() {
            unsafe {
                CoTaskMemFree(Some(closest_ptr as *const _));
            }
        }
        return Err(map_hresult(support_hr, "IAudioClient::IsFormatSupported"));
    };

    if !closest_ptr.is_null() {
        unsafe {
            CoTaskMemFree(Some(closest_ptr as *const _));
        }
    }

    let native = parse_native_format(selected.as_ptr() as *const WAVEFORMATEX)?;
    Ok((selected, native))
}

fn select_loopback_stream_format(
    audio_client: &IAudioClient,
) -> AudioResult<(Vec<u8>, NativeAudioFormat)> {
    let mix = get_mix_format(audio_client)?;
    let native = parse_native_format(mix.as_ptr() as *const WAVEFORMATEX)?;
    Ok((mix, native))
}

fn get_mix_format(audio_client: &IAudioClient) -> AudioResult<Vec<u8>> {
    let mix_ptr = unsafe { audio_client.GetMixFormat() }
        .map_err(|err| map_hresult(err.code(), "IAudioClient::GetMixFormat"))?;
    let mix = copy_wave_format(mix_ptr);
    unsafe {
        CoTaskMemFree(Some(mix_ptr as *const _));
    }
    mix
}

fn copy_wave_format(ptr: *const WAVEFORMATEX) -> AudioResult<Vec<u8>> {
    if ptr.is_null() {
        return Err(AudioError::UnsupportedFormat(
            "WASAPI returned null format pointer".into(),
        ));
    }

    let base = unsafe { *ptr };
    let total = size_of::<WAVEFORMATEX>()
        .checked_add(base.cbSize as usize)
        .ok_or(AudioError::BufferOverflow)?;
    let bytes = unsafe { slice::from_raw_parts(ptr as *const u8, total) };
    Ok(bytes.to_vec())
}

fn build_requested_wave_format(output: AudioFormat) -> AudioResult<Vec<u8>> {
    let bits_per_sample: u16 = 16;

    let block_align = u32::from(output.channels)
        .checked_mul(u32::from(bits_per_sample))
        .and_then(|v| v.checked_div(8))
        .ok_or(AudioError::BufferOverflow)? as u16;

    let avg_bytes_per_sec = output
        .sample_rate
        .checked_mul(u32::from(block_align))
        .ok_or(AudioError::BufferOverflow)?;

    let extensible = WAVEFORMATEXTENSIBLE {
        Format: WAVEFORMATEX {
            wFormatTag: WAVE_FORMAT_EXTENSIBLE as u16,
            nChannels: output.channels,
            nSamplesPerSec: output.sample_rate,
            nAvgBytesPerSec: avg_bytes_per_sec,
            nBlockAlign: block_align,
            wBitsPerSample: bits_per_sample,
            cbSize: (size_of::<WAVEFORMATEXTENSIBLE>() - size_of::<WAVEFORMATEX>()) as u16,
        },
        Samples: WAVEFORMATEXTENSIBLE_0 {
            wValidBitsPerSample: bits_per_sample,
        },
        dwChannelMask: channel_mask(output.channels),
        SubFormat: KSDATAFORMAT_SUBTYPE_PCM,
    };

    let bytes = unsafe {
        slice::from_raw_parts(
            &extensible as *const WAVEFORMATEXTENSIBLE as *const u8,
            size_of_val(&extensible),
        )
    };

    Ok(bytes.to_vec())
}

fn channel_mask(channels: u16) -> u32 {
    match channels {
        1 => 0x4,  // SPEAKER_FRONT_CENTER
        2 => 0x3,  // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
        6 => 0x3f, // 5.1 standard mask
        _ => 0,
    }
}

fn parse_native_format(ptr: *const WAVEFORMATEX) -> AudioResult<NativeAudioFormat> {
    if ptr.is_null() {
        return Err(AudioError::UnsupportedFormat(
            "WASAPI reported null format".into(),
        ));
    }

    let wf = unsafe { ptr.read_unaligned() };
    let sample_rate = unsafe { std::ptr::addr_of!(wf.nSamplesPerSec).read_unaligned() };
    let channels = unsafe { std::ptr::addr_of!(wf.nChannels).read_unaligned() };
    let format_tag = unsafe { std::ptr::addr_of!(wf.wFormatTag).read_unaligned() } as u32;
    let bits_per_sample = unsafe { std::ptr::addr_of!(wf.wBitsPerSample).read_unaligned() };
    let cb_size = unsafe { std::ptr::addr_of!(wf.cbSize).read_unaligned() };

    let sample_format = match format_tag {
        tag if tag == WAVE_FORMAT_PCM => match bits_per_sample {
            16 => NativeSampleFormat::I16,
            32 => NativeSampleFormat::I32,
            bits => {
                return Err(AudioError::UnsupportedFormat(format!(
                    "unsupported PCM bit depth: {bits}"
                )));
            }
        },
        tag if tag == u32::from(IEEE_FLOAT_FORMAT_TAG) => {
            if bits_per_sample != 32 {
                return Err(AudioError::UnsupportedFormat(format!(
                    "unsupported IEEE float bit depth: {}",
                    bits_per_sample
                )));
            }
            NativeSampleFormat::F32
        }
        tag if tag == WAVE_FORMAT_EXTENSIBLE => {
            if (cb_size as usize) < (size_of::<WAVEFORMATEXTENSIBLE>() - size_of::<WAVEFORMATEX>())
            {
                return Err(AudioError::UnsupportedFormat(
                    "malformed WAVE_FORMAT_EXTENSIBLE format".into(),
                ));
            }

            let extensible = unsafe { (ptr as *const WAVEFORMATEXTENSIBLE).read_unaligned() };
            let subformat = unsafe { std::ptr::addr_of!(extensible.SubFormat).read_unaligned() };

            if subformat == KSDATAFORMAT_SUBTYPE_PCM {
                match bits_per_sample {
                    16 => NativeSampleFormat::I16,
                    32 => NativeSampleFormat::I32,
                    bits => {
                        return Err(AudioError::UnsupportedFormat(format!(
                            "unsupported extensible PCM bit depth: {bits}"
                        )));
                    }
                }
            } else if subformat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT {
                if bits_per_sample != 32 {
                    return Err(AudioError::UnsupportedFormat(format!(
                        "unsupported extensible float bit depth: {}",
                        bits_per_sample
                    )));
                }
                NativeSampleFormat::F32
            } else {
                return Err(AudioError::UnsupportedFormat(format!(
                    "unsupported extensible subformat: {:?}",
                    subformat
                )));
            }
        }
        other => {
            return Err(AudioError::UnsupportedFormat(format!(
                "unsupported wave format tag: {other}"
            )));
        }
    };

    Ok(NativeAudioFormat {
        sample_rate,
        channels,
        sample_format,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn batched_wasapi_reads_keep_each_packets_capture_time() {
        let origin = Instant::now();
        let qpc_at_origin = 1_000_000;
        let anchor_time = origin + Duration::from_millis(30);
        let read_at = origin + Duration::from_millis(80);
        let format = AudioFormat::new(48_000, 2);
        for source in [AudioSourceKind::System, AudioSourceKind::Microphone] {
            let mut clock = WasapiClock {
                anchor: Some((anchor_time, qpc_at_origin + 300_000)),
                ..WasapiClock::default()
            };
            let mut accumulator =
                PacketAccumulator::new(source, format, Duration::from_millis(10)).unwrap();
            let mut sequence = 0;
            for index in 0..8 {
                let metadata = pending_metadata_for_wasapi_chunk(
                    &mut clock,
                    read_at,
                    qpc_at_origin as u64 + index * 100_000,
                    index * 480,
                    480,
                    format.sample_rate,
                    0,
                );
                let packets = accumulator
                    .push_chunk(vec![123; 960], 480, metadata, &mut sequence)
                    .unwrap();
                let [packet] = packets.as_slice() else {
                    panic!("each chunk should produce one packet");
                };
                assert_eq!(
                    packet.start_capture_time(),
                    Some(origin + Duration::from_millis(index * 10))
                );
                assert!(!packet.metadata.capture_time_estimated);
            }
        }
    }

    #[test]
    fn estimated_batch_times_follow_pcm_and_respect_discontinuities() {
        let origin = Instant::now();
        let read_at = origin + Duration::from_millis(100);
        let format = AudioFormat::new(1_000, 1);
        let timestamp_error = AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR.0 as u32;
        for valid_tail in [false, true] {
            let mut clock = WasapiClock {
                anchor: Some((origin, 100_000)),
                ..WasapiClock::default()
            };
            let mut accumulator =
                PacketAccumulator::new(AudioSourceKind::System, format, Duration::from_millis(4))
                    .unwrap();
            let mut packets = Vec::new();
            let mut sequence = 0;
            for index in 0..4 {
                let (qpc, flags) = match index {
                    2 if valid_tail => (460_000, 0),
                    3 => (
                        0,
                        timestamp_error | AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY.0 as u32,
                    ),
                    _ => (0, timestamp_error),
                };
                let metadata = pending_metadata_for_wasapi_chunk(
                    &mut clock,
                    read_at,
                    qpc,
                    index * 4,
                    4,
                    format.sample_rate,
                    flags,
                );
                packets.extend(
                    accumulator
                        .push_chunk(vec![index as i16 + 1; 4], 4, metadata, &mut sequence)
                        .unwrap(),
                );
            }
            estimate_batch_times(&mut packets);
            let expected_starts = if valid_tail {
                [28, 32, 36, 96]
            } else {
                [88, 92, 96, 96]
            };
            for (index, (packet, ms)) in packets.iter().zip(expected_starts).enumerate() {
                assert_eq!(
                    packet.start_capture_time(),
                    Some(origin + Duration::from_millis(ms))
                );
                assert_eq!(packet.data, [index as i16 + 1; 4]);
                assert_eq!(
                    packet.metadata.capture_time_estimated,
                    !(index == 2 && valid_tail)
                );
            }
            assert_eq!(
                packets[2].end_qpc_position_100ns(),
                valid_tail.then_some(500_000)
            );
        }
    }

    #[test]
    fn estimated_batch_times_keep_the_read_time_as_an_upper_bound() {
        let origin = Instant::now();
        let mut accumulator = PacketAccumulator::new(
            AudioSourceKind::System,
            AudioFormat::new(1_000, 1),
            Duration::from_millis(4),
        )
        .unwrap();
        let mut packets = Vec::new();
        let mut sequence = 0;
        for end_ms in [4, 100] {
            packets.extend(
                accumulator
                    .push_chunk(
                        vec![1; 4],
                        4,
                        PendingMetadata {
                            capture_time: Some(origin + Duration::from_millis(end_ms)),
                            ..Default::default()
                        },
                        &mut sequence,
                    )
                    .unwrap(),
            );
        }
        estimate_batch_times(&mut packets);
        assert_eq!(packets[0].start_capture_time(), Some(origin));
        assert_eq!(
            packets[1].start_capture_time(),
            Some(origin + Duration::from_millis(96))
        );
    }

    #[test]
    fn invalid_wasapi_timestamps_keep_pcm_and_do_not_reuse_a_previous_chunk_stamp() {
        let origin = Instant::now();
        let read_at = origin + Duration::from_millis(20);
        let mut clock = WasapiClock {
            anchor: Some((origin, 100_000)),
            ..WasapiClock::default()
        };
        let format = AudioFormat::new(1_000, 1);
        let mut accumulator =
            PacketAccumulator::new(AudioSourceKind::System, format, Duration::from_millis(4))
                .unwrap();
        let mut sequence = 0;
        let valid = pending_metadata_for_wasapi_chunk(&mut clock, read_at, 100_000, 0, 2, 1_000, 0);
        assert!(
            accumulator
                .push_chunk(vec![1, 2], 2, valid, &mut sequence)
                .unwrap()
                .is_empty()
        );

        let invalid = pending_metadata_for_wasapi_chunk(
            &mut clock,
            read_at,
            120_000,
            2,
            2,
            1_000,
            AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR.0 as u32,
        );
        let packets = accumulator
            .push_chunk(vec![3, 4], 2, invalid, &mut sequence)
            .unwrap();
        let packet = &packets[0];
        assert_eq!(packet.data, [1, 2, 3, 4]);
        assert!(packet.metadata.capture_time_estimated);
        assert_eq!(packet.end_capture_time(), Some(read_at));
        assert_eq!(packet.end_qpc_position_100ns(), None);
        assert_eq!(packet.metadata.device_position_frames, None);

        for rejected in [0, 99_999, 500_000, u64::MAX] {
            let (end, qpc) = clock.packet_end(read_at, rejected, Duration::from_millis(2));
            assert_eq!((end, qpc), (read_at, None));
        }
        let (end, qpc) = clock.packet_end(read_at, 140_000, Duration::from_millis(2));
        assert_eq!(
            (end, qpc),
            (origin + Duration::from_millis(6), Some(160_000))
        );
    }

    #[test]
    fn packet_timestamp_describes_last_frame_when_wasapi_chunks_are_split() {
        let format = AudioFormat::new(1_000, 1);
        let mut accumulator = PacketAccumulator::new(
            AudioSourceKind::System,
            format,
            std::time::Duration::from_millis(4),
        )
        .expect("test format should be valid");
        let mut sequence = 0;
        let origin = Instant::now();
        let read_at = origin + Duration::from_millis(100);
        let mut clock = WasapiClock {
            anchor: Some((origin, 100_000)),
            ..WasapiClock::default()
        };

        // WASAPI reports the position of the first frame in each chunk. The
        // source normalizes those positions to chunk ends before buffering.
        let first = accumulator
            .push_chunk(
                vec![1, 2, 3],
                3,
                pending_metadata_for_wasapi_chunk(&mut clock, read_at, 100_000, 10, 3, 1_000, 0),
                &mut sequence,
            )
            .expect("first chunk should be accepted");
        assert!(first.is_empty());

        let packets = accumulator
            .push_chunk(
                vec![4, 5, 6],
                3,
                pending_metadata_for_wasapi_chunk(&mut clock, read_at, 130_000, 13, 3, 1_000, 0),
                &mut sequence,
            )
            .expect("second chunk should complete a packet");
        assert_eq!(packets.len(), 1);
        assert_eq!(packets[0].end_qpc_position_100ns(), Some(140_000));
        assert_eq!(
            packets[0].end_capture_time(),
            Some(origin + Duration::from_millis(4))
        );
        assert_eq!(packets[0].metadata.device_position_frames, Some(14));
    }

    #[test]
    fn discontinuity_is_never_hidden_inside_a_target_packet() {
        let format = AudioFormat::new(1_000, 1);
        let mut accumulator = PacketAccumulator::new(
            AudioSourceKind::System,
            format,
            std::time::Duration::from_millis(4),
        )
        .expect("test format should be valid");
        let mut sequence = 0;

        let first = accumulator
            .push_chunk(
                vec![1, 1],
                2,
                PendingMetadata {
                    is_silent: false,
                    ..Default::default()
                },
                &mut sequence,
            )
            .expect("first half should be buffered");
        assert!(first.is_empty());

        let packets = accumulator
            .push_chunk(
                vec![2, 2],
                2,
                PendingMetadata {
                    discontinuity: true,
                    is_silent: false,
                    ..Default::default()
                },
                &mut sequence,
            )
            .expect("discontinuity should flush the preceding partial packet");

        assert_eq!(packets.len(), 1);
        assert_eq!(packets[0].frames, 2);
        assert_eq!(packets[0].data, vec![1, 1]);
        assert!(!packets[0].metadata.discontinuity);
    }

    #[test]
    fn resampled_chunk_slices_device_position_in_native_frames() {
        let format = AudioFormat::new(1_500, 1);
        let mut accumulator = PacketAccumulator::new(
            AudioSourceKind::System,
            format,
            std::time::Duration::from_millis(3 + 1),
        )
        .expect("test format should be valid");
        let mut sequence = 0;
        let origin = Instant::now();
        let read_at = origin + Duration::from_millis(100);
        let mut clock = WasapiClock {
            anchor: Some((origin, 100_000)),
            ..WasapiClock::default()
        };

        // Ten native frames at 1 kHz become fifteen output frames at 1.5 kHz.
        // The first six output frames end 4 native frames into the chunk, so
        // the native device position must advance by four, not six output
        // frames.
        let packets = accumulator
            .push_chunk(
                vec![0; 15],
                15,
                pending_metadata_for_wasapi_chunk(&mut clock, read_at, 100_000, 20, 10, 1_000, 0),
                &mut sequence,
            )
            .expect("resampled chunk should produce a packet");

        assert_eq!(packets.len(), 2);
        assert_eq!(packets[0].frames, 6);
        assert_eq!(packets[0].metadata.device_position_frames, Some(24));
        assert_eq!(packets[0].end_qpc_position_100ns(), Some(140_000));
        assert_eq!(
            packets[0].end_capture_time(),
            Some(origin + Duration::from_millis(4))
        );
    }
}
