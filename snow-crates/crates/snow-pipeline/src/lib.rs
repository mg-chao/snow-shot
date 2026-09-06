pub mod event;
pub mod multiplexer;
pub mod recording;

pub use event::{PipelineEvent, SourceId, SourceKind, TaggedEvent};
pub use multiplexer::{
    MultiplexerConfig, MuxCommand as PipelineControl, MuxStatus as PipelineStatus, SourceConfig,
    StreamMultiplexer as PipelineRuntime, StreamMultiplexerBuilder,
};
pub use recording::build_recording_pipeline;
