use snow_audio_recorder::AudioEvent;
use snow_capture::CaptureEvent;
pub use snow_core::event::{DeliveryLane, StreamEvent};
use snow_core::timestamp::StreamTimestamp;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct SourceId(pub u8);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SourceKind {
    Video,
    Audio,
}

impl SourceKind {
    pub const fn source_id(self) -> SourceId {
        match self {
            Self::Video => SourceId(0),
            Self::Audio => SourceId(1),
        }
    }
}

#[derive(Clone, Debug)]
pub struct TaggedEvent<T> {
    pub source: SourceId,
    pub event: T,
}

impl<T: StreamEvent> StreamEvent for TaggedEvent<T> {
    fn delivery_lane(&self) -> DeliveryLane {
        self.event.delivery_lane()
    }

    fn is_paused(&self) -> bool {
        self.event.is_paused()
    }

    fn is_resumed(&self) -> bool {
        self.event.is_resumed()
    }

    fn is_stream_ended(&self) -> bool {
        self.event.is_stream_ended()
    }

    fn is_error(&self) -> bool {
        self.event.is_error()
    }

    fn timestamp(&self) -> Option<&StreamTimestamp> {
        self.event.timestamp()
    }
}

pub enum PipelineEvent {
    Video(TaggedEvent<CaptureEvent>),
    Audio(TaggedEvent<AudioEvent>),
}

impl StreamEvent for PipelineEvent {
    fn delivery_lane(&self) -> DeliveryLane {
        match self {
            Self::Video(event) => event.delivery_lane(),
            Self::Audio(event) => event.delivery_lane(),
        }
    }

    fn is_paused(&self) -> bool {
        match self {
            Self::Video(event) => event.is_paused(),
            Self::Audio(event) => event.is_paused(),
        }
    }

    fn is_resumed(&self) -> bool {
        match self {
            Self::Video(event) => event.is_resumed(),
            Self::Audio(event) => event.is_resumed(),
        }
    }

    fn is_stream_ended(&self) -> bool {
        match self {
            Self::Video(event) => event.is_stream_ended(),
            Self::Audio(event) => event.is_stream_ended(),
        }
    }

    fn is_error(&self) -> bool {
        match self {
            Self::Video(event) => event.is_error(),
            Self::Audio(event) => event.is_error(),
        }
    }

    fn timestamp(&self) -> Option<&StreamTimestamp> {
        match self {
            Self::Video(event) => event.timestamp(),
            Self::Audio(event) => event.timestamp(),
        }
    }
}
