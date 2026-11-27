use std::fs::{File, create_dir_all};
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread::JoinHandle;
use std::time::Duration;

use snow_core::recording_clock::RecordingClock;
use snow_media::time::{ClockDomain, MediaTime};
use snow_recording_model::media::{DiscontinuityReason, TimelineDiscontinuity};
use snow_recording_model::{
    AudioSampleFormat, AudioTrackManifest, AudioTrackRole, AudioTrackRole::*,
};

use crate::control::AudioControlHandle;
use crate::device::DeviceSelector;
use crate::error::{AudioError, AudioResult};
use crate::format::AudioFormat;
use crate::packet::{AudioEvent, AudioPacket, AudioSourceKind};
use crate::session::{AudioSession, AudioStreamConfig, SourceConfig};
use crate::timeline::{ActivePcmSpan, PcmTimeline, admit_active_pcm};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AudioTrackDevice {
    SystemDefault,
    MicrophoneDefault,
    InputDeviceId(String),
}

#[derive(Clone, Debug)]
pub struct AudioTrackConfig {
    pub track_id: String,
    pub role: AudioTrackRole,
    pub device: AudioTrackDevice,
    pub enabled: bool,
    pub required: bool,
}

impl AudioTrackConfig {
    pub fn system_default(track_id: impl Into<String>) -> Self {
        Self {
            track_id: track_id.into(),
            role: AudioTrackRole::SystemOutput,
            device: AudioTrackDevice::SystemDefault,
            enabled: true,
            required: false,
        }
    }

    pub fn microphone_default(track_id: impl Into<String>) -> Self {
        Self {
            track_id: track_id.into(),
            role: AudioTrackRole::MicrophoneInput,
            device: AudioTrackDevice::MicrophoneDefault,
            enabled: true,
            required: false,
        }
    }
}

#[derive(Clone, Debug)]
pub struct AudioRecordingConfig {
    pub cancellation: snow_core::cancellation::CancellationToken,
    pub output_dir: PathBuf,
    pub sample_rate_hz: u32,
    pub channels: u16,
    pub packet_duration: Duration,
    pub event_buffer_depth: usize,
    pub tracks: Vec<AudioTrackConfig>,
}

impl Default for AudioRecordingConfig {
    fn default() -> Self {
        Self {
            cancellation: Default::default(),
            output_dir: PathBuf::from("."),
            sample_rate_hz: 48_000,
            channels: 2,
            packet_duration: Duration::from_millis(20),
            event_buffer_depth: 128,
            tracks: vec![
                AudioTrackConfig::system_default("system"),
                AudioTrackConfig::microphone_default("microphone"),
            ],
        }
    }
}

impl AudioRecordingConfig {
    pub fn validate(&self) -> AudioResult<()> {
        if self.cancellation.is_canceled() {
            return Err(AudioError::Canceled);
        }
        AudioFormat::new(self.sample_rate_hz, self.channels).validate()?;
        if self.packet_duration.is_zero() {
            return Err(AudioError::InvalidConfig(
                "audio packet duration must be greater than zero".into(),
            ));
        }
        if self.event_buffer_depth == 0 {
            return Err(AudioError::InvalidConfig(
                "audio event_buffer_depth must be greater than zero".into(),
            ));
        }
        if self.tracks.is_empty() {
            return Err(AudioError::InvalidConfig(
                "at least one audio track must be configured".into(),
            ));
        }
        if self.output_dir.as_os_str().is_empty() {
            return Err(AudioError::InvalidConfig(
                "audio output_dir must not be empty".into(),
            ));
        }

        let mut seen_track_ids = std::collections::HashSet::new();
        let mut system_track = 0usize;
        let mut microphone_track = 0usize;
        for track in self.tracks.iter().filter(|track| track.enabled) {
            if !seen_track_ids.insert(track.track_id.clone()) {
                return Err(AudioError::InvalidConfig(format!(
                    "duplicate audio track_id {}",
                    track.track_id
                )));
            }
            match track.role {
                SystemOutput => system_track += 1,
                MicrophoneInput => microphone_track += 1,
                Auxiliary => {
                    return Err(AudioError::InvalidConfig(
                        "auxiliary audio tracks are not supported by the current backend".into(),
                    ));
                }
            }
        }

        if system_track > 1 {
            return Err(AudioError::InvalidConfig(
                "only one system output track is currently supported".into(),
            ));
        }
        if microphone_track > 1 {
            return Err(AudioError::InvalidConfig(
                "only one microphone input track is currently supported".into(),
            ));
        }

        Ok(())
    }
}

#[derive(Clone, Debug)]
pub struct RecordedAudioTrack {
    pub manifest: AudioTrackManifest,
    pub path: PathBuf,
    pub discontinuities: Vec<TimelineDiscontinuity>,
}

#[derive(Clone, Debug, Default)]
pub struct AudioRecordingArtifact {
    pub tracks: Vec<RecordedAudioTrack>,
}

pub struct AudioRecordingSession {
    stop_flag: Arc<AtomicBool>,
    pause_flag: Arc<AtomicBool>,
    join_handle: Option<JoinHandle<AudioResult<AudioRecordingArtifact>>>,
}

impl AudioRecordingSession {
    pub fn start(config: AudioRecordingConfig, clock: RecordingClock) -> AudioResult<Self> {
        Self::start_with_controls(config, clock, AudioControlHandle::new())
    }

    pub fn start_with_controls(
        config: AudioRecordingConfig,
        clock: RecordingClock,
        audio_control: AudioControlHandle,
    ) -> AudioResult<Self> {
        config.validate()?;
        create_dir_all(&config.output_dir).map_err(AudioError::platform)?;

        let session = AudioSession::new()?;
        let stream_config = build_stream_config(&config);
        let stream = session.start_streaming_with_controls(stream_config, audio_control)?;
        let stop_flag = Arc::new(AtomicBool::new(false));
        let pause_flag = Arc::new(AtomicBool::new(false));
        let worker_stop = Arc::clone(&stop_flag);
        let worker_pause = Arc::clone(&pause_flag);

        let join_handle = std::thread::Builder::new()
            .name("snow-audio-recording".to_string())
            .spawn(move || {
                snow_core::qos::apply_current_thread();
                run_audio_recording_worker(stream, config, clock, worker_stop, worker_pause)
            })
            .map_err(|err| {
                AudioError::platform(anyhow::anyhow!(
                    "failed to spawn audio recording worker: {err}"
                ))
            })?;

        Ok(Self {
            stop_flag,
            pause_flag,
            join_handle: Some(join_handle),
        })
    }

    /// Pause delivery from the audio stream while keeping the recording
    /// worker alive. The worker emits a control event that causes the writer
    /// to re-anchor the next packet on resume.
    pub fn pause(&self) {
        self.pause_flag.store(true, Ordering::Release);
    }

    pub fn resume(&self) {
        self.pause_flag.store(false, Ordering::Release);
    }

    pub fn finish(mut self) -> AudioResult<AudioRecordingArtifact> {
        self.stop_flag.store(true, Ordering::Release);
        let handle = self.join_handle.take().ok_or(AudioError::WorkerDead)?;
        handle.join().map_err(|_| {
            AudioError::platform(anyhow::anyhow!("audio recording worker thread panicked"))
        })?
    }
}

impl Drop for AudioRecordingSession {
    fn drop(&mut self) {
        self.stop_flag.store(true, Ordering::Release);
        if let Some(handle) = self.join_handle.take() {
            let _ = handle.join();
        }
    }
}

fn build_stream_config(config: &AudioRecordingConfig) -> AudioStreamConfig {
    let format = AudioFormat::new(config.sample_rate_hz, config.channels);
    let system_track = config
        .tracks
        .iter()
        .find(|track| track.enabled && matches!(track.role, SystemOutput));
    let microphone_track = config
        .tracks
        .iter()
        .find(|track| track.enabled && matches!(track.role, MicrophoneInput));

    AudioStreamConfig {
        cancellation: config.cancellation.clone(),
        event_buffer_depth: config.event_buffer_depth,
        system: SourceConfig {
            enabled: system_track.is_some(),
            required: system_track.is_some_and(|track| track.required),
            device: DeviceSelector::DefaultRender,
            output_format: format,
            packet_duration: config.packet_duration,
        },
        microphone: SourceConfig {
            enabled: microphone_track.is_some(),
            required: microphone_track.is_some_and(|track| track.required),
            device: microphone_track
                .map(resolve_input_device)
                .unwrap_or(DeviceSelector::DefaultCapture),
            output_format: format,
            packet_duration: config.packet_duration,
        },
        ..AudioStreamConfig::default()
    }
}

fn resolve_input_device(track: &AudioTrackConfig) -> DeviceSelector {
    match &track.device {
        AudioTrackDevice::MicrophoneDefault => DeviceSelector::DefaultCapture,
        AudioTrackDevice::InputDeviceId(id) => DeviceSelector::Id(id.clone()),
        AudioTrackDevice::SystemDefault => DeviceSelector::DefaultRender,
    }
}

fn run_audio_recording_worker(
    stream: crate::streaming::AudioStreamHandle,
    config: AudioRecordingConfig,
    clock: RecordingClock,
    stop_flag: Arc<AtomicBool>,
    pause_flag: Arc<AtomicBool>,
) -> AudioResult<AudioRecordingArtifact> {
    let format = AudioFormat::new(config.sample_rate_hz, config.channels);
    let mut writers = RecordingTrackWriters::new(&config, format)?;
    let mut stream = Some(stream);

    loop {
        if stop_flag.load(Ordering::Acquire) {
            if let Some(stream) = stream.take() {
                let drained = stream.stop_and_drain();
                process_audio_events(&mut writers, &clock, drained)?;
            }
            break;
        }

        if let Some(handle) = stream.as_ref() {
            if pause_flag.load(Ordering::Acquire) {
                handle.pause();
            } else {
                handle.resume();
            }
        }

        let Some(handle) = stream.as_ref() else {
            break;
        };

        match handle.recv_timeout(Duration::from_millis(25)) {
            Ok(event) => process_audio_event(&mut writers, &clock, event)?,
            Err(crate::error::RecvTimeoutError::Timeout) => {}
            Err(crate::error::RecvTimeoutError::Closed) => {
                if let Some(stream) = stream.take() {
                    let drained = stream.stop_and_drain();
                    process_audio_events(&mut writers, &clock, drained)?;
                }
                break;
            }
        }
    }

    writers.finish()
}

fn process_audio_events(
    writers: &mut RecordingTrackWriters,
    clock: &RecordingClock,
    events: Vec<AudioEvent>,
) -> AudioResult<()> {
    for event in events {
        process_audio_event(writers, clock, event)?;
    }
    Ok(())
}

fn process_audio_event(
    writers: &mut RecordingTrackWriters,
    clock: &RecordingClock,
    event: AudioEvent,
) -> AudioResult<()> {
    match event {
        AudioEvent::Packet(packet) if !packet.data.is_empty() => {
            writers.write_packet(&packet, clock)?;
        }
        AudioEvent::PacketDropped {
            source,
            dropped_frames,
        } => {
            writers.write_silence(source, dropped_frames)?;
        }
        AudioEvent::Error(err) => return Err(err),
        AudioEvent::StreamEnded => {}
        AudioEvent::Paused { .. } | AudioEvent::Resumed { .. } => {
            writers.reset_alignment(None);
        }
        AudioEvent::SourceRestarted { source, .. } => {
            writers.reset_alignment(Some(source));
        }
        AudioEvent::Packet(_) => {}
    }
    Ok(())
}

struct RecordingTrackWriters {
    system_writer: Option<RecordedTrackWriter>,
    microphone_writer: Option<RecordedTrackWriter>,
}

impl RecordingTrackWriters {
    fn new(config: &AudioRecordingConfig, format: AudioFormat) -> AudioResult<Self> {
        let system_writer = config
            .tracks
            .iter()
            .find(|track| track.enabled && matches!(track.role, SystemOutput))
            .map(|track| RecordedTrackWriter::new(track, &config.output_dir, format))
            .transpose()?;
        let microphone_writer = config
            .tracks
            .iter()
            .find(|track| track.enabled && matches!(track.role, MicrophoneInput))
            .map(|track| RecordedTrackWriter::new(track, &config.output_dir, format))
            .transpose()?;

        Ok(Self {
            system_writer,
            microphone_writer,
        })
    }

    fn write_packet(&mut self, packet: &AudioPacket, clock: &RecordingClock) -> AudioResult<()> {
        let writer = match packet.source {
            AudioSourceKind::System => self.system_writer.as_mut(),
            AudioSourceKind::Microphone => self.microphone_writer.as_mut(),
        };
        if let Some(writer) = writer {
            writer.write_packet(packet, clock)?;
        }
        Ok(())
    }

    fn write_silence(&mut self, source: AudioSourceKind, frames: u64) -> AudioResult<()> {
        let writer = match source {
            AudioSourceKind::System => self.system_writer.as_mut(),
            AudioSourceKind::Microphone => self.microphone_writer.as_mut(),
        };
        if let Some(writer) = writer {
            writer.record_discontinuity(Some(frames), DiscontinuityReason::AudioOverflow)?;
            writer.append_silence_frames(frames)?;
            writer.timeline.skip_frames(frames);
        }
        Ok(())
    }

    fn reset_alignment(&mut self, source: Option<AudioSourceKind>) {
        if source.is_none_or(|kind| matches!(kind, AudioSourceKind::System))
            && let Some(writer) = self.system_writer.as_mut()
        {
            writer.timeline.reset();
        }
        if source.is_none_or(|kind| matches!(kind, AudioSourceKind::Microphone))
            && let Some(writer) = self.microphone_writer.as_mut()
        {
            writer.timeline.reset();
        }
    }

    fn finish(self) -> AudioResult<AudioRecordingArtifact> {
        let mut tracks = Vec::new();
        if let Some(writer) = self.system_writer {
            tracks.push(writer.finish()?);
        }
        if let Some(writer) = self.microphone_writer {
            tracks.push(writer.finish()?);
        }
        Ok(AudioRecordingArtifact { tracks })
    }
}

/// Writes one recorded track as raw interleaved PCM.
///
/// The track file carries no container header: its layout is fully described
/// by the `AudioTrackManifest` (`sample_format`, `sample_rate_hz`, `channels`,
/// `duration_frames`) that is embedded in the recording bundle next to it.
/// Keeping the manifest as the single source of format metadata means the
/// bundle reader never has to parse or reconcile a second copy.
struct RecordedTrackWriter {
    manifest: AudioTrackManifest,
    path: PathBuf,
    writer: BufWriter<File>,
    written_frames: u64,
    format: AudioFormat,
    timeline: PcmTimeline,
    silence_chunk: Vec<u8>,
    discontinuities: Vec<TimelineDiscontinuity>,
    active_spans: Vec<ActivePcmSpan>,
    corrected_samples: Vec<i16>,
}

impl RecordedTrackWriter {
    fn new(config: &AudioTrackConfig, output_dir: &Path, format: AudioFormat) -> AudioResult<Self> {
        let asset_id = format!("audio/{}.pcm", config.track_id);
        let path = output_dir.join(format!("audio-{}.pcm", sanitize_track_id(&config.track_id)));
        let file = File::create(&path).map_err(AudioError::platform)?;
        let writer = BufWriter::with_capacity(128 * 1024, file);
        let silence_chunk = vec![0u8; 4096 * usize::from(format.channels) * 2];
        Ok(Self {
            manifest: AudioTrackManifest {
                track_id: config.track_id.clone(),
                role: config.role,
                asset_id,
                sample_rate_hz: format.sample_rate,
                channels: format.channels,
                sample_format: AudioSampleFormat::PcmS16Le,
                duration_frames: 0,
                recorded: false,
            },
            path,
            writer,
            written_frames: 0,
            format,
            timeline: PcmTimeline::default(),
            silence_chunk,
            discontinuities: Vec::new(),
            active_spans: Vec::with_capacity(2),
            corrected_samples: Vec::new(),
        })
    }

    fn write_packet(&mut self, packet: &AudioPacket, clock: &RecordingClock) -> AudioResult<()> {
        let packet_frames = i16_frame_count(packet.as_i16_slice(), self.format.channels)?;
        if packet_frames == 0 {
            return Ok(());
        }
        if packet.format != self.format || packet_frames != u64::from(packet.frames) {
            return Err(AudioError::InvalidConfig(
                "recorded PCM packet disagrees with its configured format".into(),
            ));
        }
        let Some(packet_start) = packet.start_capture_time() else {
            return Ok(());
        };
        admit_active_pcm(
            clock,
            packet_start,
            packet_frames,
            self.format.sample_rate,
            &mut self.active_spans,
        );
        if self.active_spans.is_empty() {
            self.timeline.reset();
        }
        let channels = usize::from(self.format.channels);
        for index in 0..self.active_spans.len() {
            let placement = self
                .timeline
                .place(packet, self.active_spans[index], &(0..u64::MAX));
            let target = placement.start_frame;
            let silence = target.saturating_sub(self.written_frames);
            if silence != 0
                || placement.clock_reset
                || (packet.metadata.discontinuity && index == 0)
            {
                let reason = if placement.clock_reset {
                    DiscontinuityReason::ClockReset
                } else {
                    DiscontinuityReason::SourceInterrupted
                };
                self.record_discontinuity((silence != 0).then_some(silence), reason)?;
            }
            self.append_silence_frames(silence)?;
            let mut scratch = std::mem::take(&mut self.corrected_samples);
            let samples = placement.samples(packet, &mut scratch);
            let skip = self
                .written_frames
                .saturating_sub(target)
                .min((samples.len() / channels) as u64);
            let result = self.append_i16_samples(&samples[skip as usize * channels..]);
            self.corrected_samples = scratch;
            result?;
        }
        Ok(())
    }

    fn append_silence_frames(&mut self, frames: u64) -> AudioResult<()> {
        if frames == 0 {
            return Ok(());
        }

        let channels = usize::from(self.format.channels);
        const CHUNK_FRAMES: u64 = 4096;
        let mut remaining = frames;
        while remaining > 0 {
            let take = remaining.min(CHUNK_FRAMES);
            let take_bytes = take as usize * channels * 2;
            self.writer
                .write_all(&self.silence_chunk[..take_bytes])
                .map_err(AudioError::platform)?;
            self.written_frames = self.written_frames.saturating_add(take);
            remaining -= take;
        }
        if frames > 0 {
            self.manifest.recorded = true;
            self.manifest.duration_frames = self.written_frames;
        }
        Ok(())
    }

    fn record_discontinuity(
        &mut self,
        frames: Option<u64>,
        reason: DiscontinuityReason,
    ) -> AudioResult<()> {
        let time = |value| -> AudioResult<MediaTime> {
            Ok(MediaTime {
                value: i64::try_from(value).map_err(|_| AudioError::BufferOverflow)?,
                timescale: self.format.sample_rate,
                domain: ClockDomain::Session,
                epoch: 0,
            })
        };
        self.discontinuities.push(TimelineDiscontinuity {
            timestamp: time(self.written_frames)?,
            duration: frames.map(time).transpose()?,
            reason,
        });
        Ok(())
    }

    fn append_i16_samples(&mut self, samples: &[i16]) -> AudioResult<()> {
        if samples.is_empty() {
            return Ok(());
        }
        let frames = i16_frame_count(samples, self.format.channels)?;
        write_i16_le(&mut self.writer, samples)?;
        self.written_frames = self.written_frames.saturating_add(frames);
        self.manifest.recorded = true;
        self.manifest.duration_frames = self.written_frames;
        Ok(())
    }

    fn finish(mut self) -> AudioResult<RecordedAudioTrack> {
        self.writer.flush().map_err(AudioError::platform)?;
        Ok(RecordedAudioTrack {
            manifest: self.manifest,
            path: self.path,
            discontinuities: self.discontinuities,
        })
    }
}

fn sanitize_track_id(track_id: &str) -> String {
    let sanitized: String = track_id
        .chars()
        .map(|ch| if ch.is_ascii_alphanumeric() { ch } else { '-' })
        .collect();
    if sanitized.is_empty() {
        "track".to_string()
    } else {
        sanitized
    }
}

fn i16_frame_count(samples: &[i16], channels: u16) -> AudioResult<u64> {
    let channels = usize::from(channels.max(1));
    if !samples.len().is_multiple_of(channels) {
        return Err(AudioError::InvalidConfig(
            "audio packet samples are not channel aligned".into(),
        ));
    }
    Ok((samples.len() / channels) as u64)
}

fn write_i16_le(writer: &mut impl Write, samples: &[i16]) -> AudioResult<()> {
    #[cfg(target_endian = "little")]
    {
        let bytes = unsafe {
            std::slice::from_raw_parts(
                samples.as_ptr() as *const u8,
                std::mem::size_of_val(samples),
            )
        };
        writer.write_all(bytes).map_err(AudioError::platform)?;
    }

    #[cfg(not(target_endian = "little"))]
    {
        for sample in samples {
            writer
                .write_all(&sample.to_le_bytes())
                .map_err(AudioError::platform)?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn timed_packet(end: std::time::Instant, qpc_100ns: i64, sequence: u64) -> AudioPacket {
        let format = AudioFormat::new(48_000, 2);
        let frames = 960;
        let mut metadata = crate::packet::AudioPacketMetadata {
            sequence,
            ..Default::default()
        };
        metadata.set_timing(Some(end), Some(qpc_100ns));
        AudioPacket {
            source: AudioSourceKind::Microphone,
            format,
            frames,
            data: vec![0; format.samples_for_frames(frames).unwrap()],
            metadata,
        }
    }

    #[test]
    fn default_audio_recording_config_is_track_based() {
        let config = AudioRecordingConfig::default();
        assert_eq!(config.tracks.len(), 2);
        assert!(
            config
                .tracks
                .iter()
                .any(|track| matches!(track.role, AudioTrackRole::SystemOutput))
        );
        assert!(
            config
                .tracks
                .iter()
                .any(|track| matches!(track.role, AudioTrackRole::MicrophoneInput))
        );
    }

    #[test]
    fn recording_audio_sources_are_optional() {
        let stream_config = build_stream_config(&AudioRecordingConfig::default());

        assert!(stream_config.system.enabled);
        assert!(!stream_config.system.required);
        assert!(stream_config.microphone.enabled);
        assert!(!stream_config.microphone.required);
    }

    #[test]
    fn required_track_policy_survives_stream_configuration() {
        let mut config = AudioRecordingConfig::default();
        config.tracks[0].required = true;
        config.tracks[1].required = true;
        let stream = build_stream_config(&config);
        assert!(stream.system.required);
        assert!(stream.microphone.required);
    }

    #[test]
    fn controlled_recording_writes_gained_pcm_and_exposes_live_levels() {
        use crate::backend::{AudioRecorderEngine, EngineEvent};
        struct GainedEngine {
            step: u64,
            origin: std::time::Instant,
            control: AudioControlHandle,
            ready: std::sync::mpsc::Sender<f32>,
        }
        impl AudioRecorderEngine for GainedEngine {
            fn poll(&mut self, _: Duration) -> AudioResult<EngineEvent> {
                let step = self.step;
                self.step += 1;
                if step == 1 {
                    assert_eq!(self.control.take_levels().microphone.peak, 501.0 / 32768.0);
                    self.control.set_gain_db(AudioSourceKind::Microphone, 6)?;
                }
                if step < 2 {
                    let mut packet = timed_packet(
                        self.origin + Duration::from_millis((step + 1) * 20),
                        1_000_000 + step as i64 * 200_000,
                        step,
                    );
                    packet.data.fill(1000);
                    return Ok(EngineEvent::Events(vec![AudioEvent::Packet(packet)]));
                }
                if step == 2 {
                    self.ready
                        .send(self.control.take_levels().microphone.peak)
                        .unwrap();
                }
                std::thread::sleep(Duration::from_millis(1));
                Ok(EngineEvent::Idle)
            }
        }
        let directory = std::env::temp_dir().join(format!(
            "snow-audio-controlled-recording-{}",
            std::process::id()
        ));
        std::fs::create_dir_all(&directory).unwrap();
        let config = AudioRecordingConfig {
            output_dir: directory.clone(),
            tracks: vec![AudioTrackConfig::microphone_default("microphone")],
            ..Default::default()
        };
        let control = AudioControlHandle::new();
        control
            .set_gain_db(AudioSourceKind::Microphone, -6)
            .unwrap();
        control.set_metering(AudioSourceKind::Microphone, true);
        let origin = std::time::Instant::now();
        let (ready, receiver) = std::sync::mpsc::channel();
        let stream = crate::streaming::AudioStreamHandle::start_with_controls(
            Box::new(GainedEngine {
                step: 0,
                origin,
                control: control.clone(),
                ready,
            }),
            build_stream_config(&config),
            control.clone(),
        )
        .unwrap();
        assert_eq!(
            receiver.recv_timeout(Duration::from_secs(2)).unwrap(),
            1995.0 / 32768.0
        );
        let artifact = run_audio_recording_worker(
            stream,
            config,
            RecordingClock::new(origin),
            Arc::new(AtomicBool::new(true)),
            Arc::new(AtomicBool::new(false)),
        )
        .unwrap();
        let pcm = std::fs::read(&artifact.tracks[0].path).unwrap();
        assert_eq!(pcm.len(), 1920 * 2 * 2);
        assert_eq!(i16::from_le_bytes(pcm[..2].try_into().unwrap()), 501);
        assert_eq!(
            i16::from_le_bytes(pcm[pcm.len() - 2..].try_into().unwrap()),
            1995
        );
        std::fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn pcm_overflow_metadata_matches_inserted_silence_in_sample_time() {
        let started = std::time::Instant::now();
        let output_dir =
            std::env::temp_dir().join(format!("snow-audio-overflow-{}", std::process::id()));
        std::fs::create_dir_all(&output_dir).unwrap();
        let config = AudioRecordingConfig {
            output_dir: output_dir.clone(),
            ..Default::default()
        };
        let mut writers = RecordingTrackWriters::new(&config, AudioFormat::new(48_000, 2)).unwrap();
        let clock = RecordingClock::new(started);
        process_audio_event(
            &mut writers,
            &clock,
            AudioEvent::Packet(timed_packet(
                started + Duration::from_millis(20),
                1_000_000,
                1,
            )),
        )
        .unwrap();
        process_audio_event(
            &mut writers,
            &clock,
            AudioEvent::PacketDropped {
                source: AudioSourceKind::Microphone,
                dropped_frames: 480,
            },
        )
        .unwrap();
        process_audio_event(
            &mut writers,
            &clock,
            AudioEvent::Packet(timed_packet(
                started + Duration::from_millis(50),
                1_300_000,
                2,
            )),
        )
        .unwrap();
        let recorded = writers.finish().unwrap();
        let track = recorded
            .tracks
            .iter()
            .find(|track| track.manifest.role == MicrophoneInput)
            .unwrap();
        assert_eq!(track.manifest.duration_frames, 2_400);
        let gap = track.discontinuities.last().unwrap();
        assert_eq!(gap.timestamp.value, 960);
        assert_eq!(gap.timestamp.timescale, 48_000);
        assert_eq!(gap.duration.unwrap().value, 480);
        assert!(matches!(gap.reason, DiscontinuityReason::AudioOverflow));
        let bytes = std::fs::read(&track.path).unwrap();
        assert_eq!(bytes.len(), 2_400 * 2 * 2);
        assert!(
            bytes[960 * 2 * 2..1_440 * 2 * 2]
                .iter()
                .all(|value| *value == 0)
        );
        assert_eq!(&bytes[1_440 * 2 * 2..], &bytes[..960 * 2 * 2]);
        std::fs::remove_dir_all(output_dir).unwrap();
    }

    #[test]
    fn buffered_pcm_excludes_startup_pause_samples_and_clips_the_accepted_stop() {
        let start = std::time::Instant::now();
        let output = std::env::temp_dir().join(format!(
            "snow-audio-active-spans-{}-{}",
            std::process::id(),
            start.elapsed().as_nanos()
        ));
        std::fs::create_dir_all(&output).unwrap();
        let format = AudioFormat::new(48_000, 2);
        let track = AudioTrackConfig::microphone_default("clock");
        let mut writer = RecordedTrackWriter::new(&track, &output, format).unwrap();
        let clock = RecordingClock::new(start);
        let controller = clock.controller();
        controller.mark_pause(start);
        controller.mark_resume(start + Duration::from_millis(30));
        controller.mark_pause(start + Duration::from_millis(50));
        controller.mark_resume(start + Duration::from_millis(70));
        controller.mark_pause(start + Duration::from_millis(85));
        for index in 1..=6 {
            let mut packet = timed_packet(
                start + Duration::from_millis(index * 20),
                1_000_000 + (index as i64 - 1) * 200_000,
                index,
            );
            packet.data = (0..960)
                .flat_map(|frame| [index as i16 * 1_000 + frame; 2])
                .collect();
            writer.write_packet(&packet, &clock).unwrap();
        }
        let recorded = writer.finish().unwrap();
        let bytes = std::fs::read(&recorded.path).unwrap();
        let samples: Vec<_> = bytes
            .chunks_exact(2)
            .map(|sample| i16::from_le_bytes([sample[0], sample[1]]))
            .collect();
        let expected: Vec<_> = [(2, 480..960), (3, 0..480), (4, 480..960), (5, 0..240)]
            .into_iter()
            .flat_map(|(packet, frames)| frames.flat_map(move |frame| [packet * 1_000 + frame; 2]))
            .collect();
        assert_eq!(samples, expected);
        assert_eq!(recorded.manifest.duration_frames, 1_680);
        assert!(recorded.manifest.recorded);
        std::fs::remove_dir_all(output).unwrap();
    }

    #[test]
    fn buffered_pcm_clips_a_packet_that_starts_before_recording() {
        let origin = std::time::Instant::now();
        let directory = std::env::temp_dir().join(format!(
            "snow-audio-startup-{}-{}",
            std::process::id(),
            origin.elapsed().as_nanos()
        ));
        std::fs::create_dir_all(&directory).unwrap();
        let clock = RecordingClock::new(origin);
        let mut packet = timed_packet(origin + Duration::from_millis(10), 1_000_000, 1);
        packet.data = (1..=1920).collect();
        let track = AudioTrackConfig::microphone_default("startup");
        let mut writer = RecordedTrackWriter::new(&track, &directory, packet.format).unwrap();
        writer.write_packet(&packet, &clock).unwrap();
        let recorded = writer.finish().unwrap();
        assert_eq!(recorded.manifest.duration_frames, 480);
        let bytes = std::fs::read(&recorded.path).unwrap();
        assert_eq!(i16::from_le_bytes(bytes[..2].try_into().unwrap()), 961);
        std::fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn buffered_pcm_corrects_drift_only_from_trusted_capture_times() {
        let origin = std::time::Instant::now();
        let directory = std::env::temp_dir().join(format!(
            "snow-audio-drift-{}-{}",
            std::process::id(),
            origin.elapsed().as_nanos()
        ));
        std::fs::create_dir_all(&directory).unwrap();
        let clock = RecordingClock::new(origin);

        for (end_ms, estimated, expected) in
            [(46, false, 1921), (34, false, 1919), (110, true, 1920)]
        {
            let track = AudioTrackConfig::microphone_default(format!("drift-{end_ms}"));
            let mut first = timed_packet(origin + Duration::from_millis(20), 1_000_000, 1);
            first.data = vec![1234; 1920];
            let mut second = timed_packet(origin + Duration::from_millis(end_ms), 1_200_000, 2);
            second.data = vec![2345; 1920];
            second.metadata.capture_time_estimated = estimated;
            let mut writer = RecordedTrackWriter::new(&track, &directory, first.format).unwrap();
            writer.write_packet(&first, &clock).unwrap();
            writer.write_packet(&second, &clock).unwrap();

            let recorded = writer.finish().unwrap();
            assert_eq!(recorded.manifest.duration_frames, expected);
            assert!(recorded.discontinuities.is_empty());
            let bytes = std::fs::read(&recorded.path).unwrap();
            let mut pcm = bytes
                .chunks_exact(2)
                .map(|sample| i16::from_le_bytes(sample.try_into().unwrap()));
            assert!(pcm.by_ref().take(1920).all(|sample| sample == 1234));
            assert!(pcm.all(|sample| sample == 2345));
        }
        std::fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn buffered_pcm_records_clock_resets_and_preserves_written_samples() {
        let origin = std::time::Instant::now();
        let directory = std::env::temp_dir().join(format!(
            "snow-audio-clock-reset-{}-{}",
            std::process::id(),
            origin.elapsed().as_nanos()
        ));
        std::fs::create_dir_all(&directory).unwrap();
        let clock = RecordingClock::new(origin);
        let track = AudioTrackConfig::microphone_default("clock-reset");
        let mut packet = timed_packet(origin + Duration::from_millis(20), 1_000_000, 1);
        let mut writer = RecordedTrackWriter::new(&track, &directory, packet.format).unwrap();

        for (index, end_ms) in [80, 40, 60, 80, 100, 180].into_iter().enumerate() {
            packet.metadata.sequence = index as u64 + 1;
            packet.metadata.capture_time_estimated = index == 0;
            packet
                .metadata
                .set_timing(Some(origin + Duration::from_millis(end_ms)), None);
            packet.data.fill(index as i16 + 1);
            writer.write_packet(&packet, &clock).unwrap();
        }
        let recorded = writer.finish().unwrap();
        assert_eq!(recorded.manifest.duration_frames, 8_640);
        let [startup, backward, forward] = recorded.discontinuities.as_slice() else {
            panic!("expected one startup gap and two clock resets");
        };
        assert!(matches!(
            startup.reason,
            DiscontinuityReason::SourceInterrupted
        ));
        assert!(matches!(backward.reason, DiscontinuityReason::ClockReset));
        assert!(backward.duration.is_none());
        assert!(matches!(forward.reason, DiscontinuityReason::ClockReset));
        assert_eq!(forward.duration.unwrap().value, 2_880);
        let bytes = std::fs::read(&recorded.path).unwrap();
        let actual: Vec<i16> = bytes
            .chunks_exact(2)
            .map(|sample| i16::from_le_bytes(sample.try_into().unwrap()))
            .collect();
        let expected: Vec<i16> = [0, 0, 0, 1, 5, 0, 0, 0, 6]
            .into_iter()
            .flat_map(|value| [value; 1920])
            .collect();
        assert_eq!(actual, expected);
        std::fs::remove_dir_all(directory).unwrap();
    }

    #[test]
    fn contiguous_device_packets_ignore_subframe_qpc_jitter() {
        let started_at = std::time::Instant::now();
        let output_dir = std::env::temp_dir().join(format!(
            "snow-audio-recorder-timing-{}",
            started_at.elapsed().as_nanos()
        ));
        std::fs::create_dir_all(&output_dir).expect("temporary output directory should be created");
        let track = AudioTrackConfig::microphone_default("timing");
        let format = AudioFormat::new(48_000, 2);
        let mut writer = RecordedTrackWriter::new(&track, &output_dir, format)
            .expect("temporary track should be created");
        let clock = RecordingClock::new(started_at);

        let make_packet = |end: std::time::Instant, qpc_100ns: i64, device_end: u64, value: i16| {
            let frames = 960;
            let mut metadata = crate::packet::AudioPacketMetadata {
                device_position_frames: Some(device_end),
                ..Default::default()
            };
            metadata.set_timing(Some(end), Some(qpc_100ns));
            AudioPacket {
                source: AudioSourceKind::Microphone,
                format,
                frames,
                data: vec![value; format.samples_for_frames(frames).unwrap()],
                metadata,
            }
        };

        writer
            .write_packet(
                &make_packet(started_at + Duration::from_millis(20), 1_000_000, 960, 1),
                &clock,
            )
            .expect("first packet should be written");
        writer
            .write_packet(
                &make_packet(
                    started_at + Duration::from_micros(40_030),
                    1_200_300,
                    1_920,
                    2,
                ),
                &clock,
            )
            .expect("second packet should be written");

        let recorded = writer.finish().expect("track should finish");
        let bytes = std::fs::read(&recorded.path).expect("recorded samples should be readable");
        let samples: Vec<i16> = bytes
            .chunks_exact(2)
            .map(|chunk| i16::from_le_bytes([chunk[0], chunk[1]]))
            .collect();
        assert_eq!(samples.len(), 1_920 * 2);
        assert_eq!(&samples[0..2], &[1, 1]);
        assert_eq!(&samples[1_920..1_922], &[2, 2]);

        std::fs::remove_dir_all(output_dir).expect("temporary output directory should be removed");
    }
}
