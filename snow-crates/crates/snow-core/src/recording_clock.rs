use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PauseInterval {
    pub start_ms: u64,
    pub end_ms: u64,
}

#[derive(Debug)]
struct RecordingClockState {
    started_at: Instant,
    pause_started_at: Option<Instant>,
    intervals: Vec<PauseInterval>,
    total_paused: Duration,
}

impl RecordingClockState {
    fn new(started_at: Instant) -> Self {
        Self {
            started_at,
            pause_started_at: None,
            intervals: Vec::new(),
            total_paused: Duration::ZERO,
        }
    }

    fn mark_pause(&mut self, at: Instant) {
        if self.pause_started_at.is_none() {
            self.pause_started_at = Some(at);
        }
    }

    fn mark_resume(&mut self, at: Instant) {
        if let Some(start) = self.pause_started_at.take() {
            let start_ms = start.saturating_duration_since(self.started_at).as_millis() as u64;
            let end_ms = at.saturating_duration_since(self.started_at).as_millis() as u64;
            self.total_paused += at.saturating_duration_since(start);
            self.intervals.push(PauseInterval { start_ms, end_ms });
        }
    }

    fn finalize(&mut self, at: Instant) {
        if self.pause_started_at.is_some() {
            self.mark_resume(at);
        }
    }

    fn active_elapsed_duration(&self, at: Instant) -> Duration {
        let elapsed = at.saturating_duration_since(self.started_at);
        let mut paused = self.total_paused;
        if let Some(paused_from) = self.pause_started_at {
            paused += at.saturating_duration_since(paused_from);
        }
        elapsed.saturating_sub(paused)
    }
}

#[derive(Clone, Debug)]
pub struct RecordingClock {
    inner: Arc<Mutex<RecordingClockState>>,
}

impl RecordingClock {
    pub fn new(started_at: Instant) -> Self {
        Self {
            inner: Arc::new(Mutex::new(RecordingClockState::new(started_at))),
        }
    }

    pub fn controller(&self) -> RecordingClockController {
        RecordingClockController {
            inner: Arc::clone(&self.inner),
        }
    }

    pub fn started_at(&self) -> Instant {
        self.inner.lock().unwrap().started_at
    }

    pub fn active_elapsed_duration(&self, at: Instant) -> Duration {
        self.inner.lock().unwrap().active_elapsed_duration(at)
    }

    pub fn active_elapsed_ms(&self, at: Instant) -> u64 {
        self.active_elapsed_duration(at).as_millis() as u64
    }

    pub fn active_elapsed_from_stream_offset(&self, offset: Duration) -> Duration {
        let started_at = self.started_at();
        let at = started_at.checked_add(offset).unwrap_or(started_at);
        self.active_elapsed_duration(at)
    }

    pub fn pause_intervals(&self) -> Vec<PauseInterval> {
        self.inner.lock().unwrap().intervals.clone()
    }
}

#[derive(Clone, Debug)]
pub struct RecordingClockController {
    inner: Arc<Mutex<RecordingClockState>>,
}

impl RecordingClockController {
    pub fn mark_pause(&self, at: Instant) {
        self.inner.lock().unwrap().mark_pause(at);
    }

    pub fn mark_resume(&self, at: Instant) {
        self.inner.lock().unwrap().mark_resume(at);
    }

    pub fn finalize(&self, at: Instant) {
        self.inner.lock().unwrap().finalize(at);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clock_tracks_pause_intervals_and_active_elapsed() {
        let started_at = Instant::now();
        let clock = RecordingClock::new(started_at);
        let controller = clock.controller();

        controller.mark_pause(started_at + Duration::from_millis(100));
        controller.mark_resume(started_at + Duration::from_millis(250));

        assert_eq!(
            clock.pause_intervals(),
            vec![PauseInterval {
                start_ms: 100,
                end_ms: 250,
            }]
        );
        assert_eq!(
            clock.active_elapsed_ms(started_at + Duration::from_millis(400)),
            250
        );
    }

    #[test]
    fn finalize_closes_open_pause_interval() {
        let started_at = Instant::now();
        let clock = RecordingClock::new(started_at);
        let controller = clock.controller();

        controller.mark_pause(started_at + Duration::from_millis(50));
        controller.finalize(started_at + Duration::from_millis(100));

        assert_eq!(
            clock.pause_intervals(),
            vec![PauseInterval {
                start_ms: 50,
                end_ms: 100,
            }]
        );
    }
}
