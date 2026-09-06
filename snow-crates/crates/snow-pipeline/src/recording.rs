use std::time::Duration;

use smallvec::{SmallVec, smallvec};
use snow_audio_recorder::AudioStreamHandle;
use snow_capture::{CaptureEvent, CaptureStream};

use crate::event::{PipelineEvent, SourceId, SourceKind, TaggedEvent};
use crate::multiplexer::{MultiplexerConfig, SourceConfig, StreamMultiplexerBuilder};

const SELECT_TIMEOUT: Duration = Duration::from_millis(25);
const SOURCE_SEND_TIMEOUT: Duration = Duration::from_millis(10);
const OUTPUT_CAPACITY: usize = 32;
const VIDEO_CHANNEL_CAPACITY: usize = 8;
const AUDIO_CHANNEL_CAPACITY: usize = 16;

pub fn build_recording_pipeline(
    video_handle: CaptureStream,
    audio_handle: Option<AudioStreamHandle>,
) -> (crate::PipelineRuntime<PipelineEvent>, Vec<SourceId>) {
    let video_source = SourceKind::Video.source_id();
    let audio_source = SourceKind::Audio.source_id();

    let mut builder = StreamMultiplexerBuilder::new(MultiplexerConfig {
        select_timeout: SELECT_TIMEOUT,
        priority_source: audio_handle.as_ref().map(|_| audio_source),
        priority_drain_batch: 8,
        output_capacity: OUTPUT_CAPACITY,
        output_send_timeout: SOURCE_SEND_TIMEOUT,
    });

    let mut active_sources = Vec::with_capacity(2);

    builder.register(
        SourceConfig {
            source_id: video_source,
            channel_capacity: VIDEO_CHANNEL_CAPACITY,
            send_timeout: SOURCE_SEND_TIMEOUT,
        },
        video_handle,
        video_mapper,
    );
    active_sources.push(video_source);

    if let Some(handle) = audio_handle {
        builder.register(
            SourceConfig {
                source_id: audio_source,
                channel_capacity: AUDIO_CHANNEL_CAPACITY,
                send_timeout: SOURCE_SEND_TIMEOUT,
            },
            handle,
            audio_mapper,
        );
        active_sources.push(audio_source);
    }

    (builder.build(), active_sources)
}

fn video_mapper(te: TaggedEvent<CaptureEvent>) -> SmallVec<[PipelineEvent; 2]> {
    smallvec![PipelineEvent::Video(te)]
}

fn audio_mapper(te: TaggedEvent<snow_audio_recorder::AudioEvent>) -> SmallVec<[PipelineEvent; 2]> {
    smallvec![PipelineEvent::Audio(te)]
}
