//! Streaming traits: `StreamHandle<E>` and `StreamStats`.

use std::time::Duration;

use crate::error::{RecvError, RecvTimeoutError, TryRecvError};

/// Trait for streaming handles returned by leaf crate `start_streaming` methods.
/// `E` is the event type (e.g., `CaptureEvent`, `AudioEvent`).
pub trait StreamHandle<E>: Send {
    /// Error type returned by blocking recv.
    /// Must be convertible to the core `RecvError` for generic adapter use.
    type RecvError: Into<RecvError> + Send;

    /// Error type returned by non-blocking try_recv.
    type TryRecvError: Into<TryRecvError> + Send;

    /// Error type returned by recv_timeout.
    type RecvTimeoutError: Into<RecvTimeoutError> + Send;

    /// Block until the next event is available.
    fn recv(&self) -> Result<E, Self::RecvError>;

    /// Non-blocking receive. Returns immediately if no event is available.
    fn try_recv(&self) -> Result<E, Self::TryRecvError>;

    /// Block until an event is available or the timeout elapses.
    fn recv_timeout(&self, timeout: Duration) -> Result<E, Self::RecvTimeoutError>;

    /// Signal the stream to stop. Non-blocking.
    fn stop(&self);

    /// Signal the stream to pause. Non-blocking.
    fn pause(&self);

    /// Signal the stream to resume. Non-blocking.
    fn resume(&self);

    /// Returns `true` if the stream is currently paused.
    fn is_paused(&self) -> bool;

    /// Returns `true` if the stream thread is still running.
    fn is_running(&self) -> bool;
}

/// Trait for querying stream statistics.
pub trait StreamStats {
    /// Take a point-in-time snapshot of stream statistics.
    fn snapshot(&self) -> StreamStatsSnapshot;
}

/// Common statistics snapshot shared across all stream types.
#[derive(Clone, Debug, Default)]
pub struct StreamStatsSnapshot {
    pub total_events: u64,
    pub dropped_events: u64,
    pub buffer_fill_ratio: f64,
}
