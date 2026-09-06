use std::collections::VecDeque;
use std::sync::{Condvar, Mutex};
use std::time::Duration;

use crate::error::{RecvError, RecvTimeoutError, TryRecvError};
use crate::event::{DeliveryLane, StreamEvent};

struct QueueState<E> {
    closed: bool,
    data_events: VecDeque<E>,
    control_events: VecDeque<E>,
}

impl<E> QueueState<E> {
    fn is_empty(&self) -> bool {
        self.control_events.is_empty() && self.data_events.is_empty()
    }

    fn pop_next(&mut self) -> Option<E> {
        self.control_events
            .pop_front()
            .or_else(|| self.data_events.pop_front())
    }

    fn pop_next_with_data_len(&mut self) -> Option<(E, usize)> {
        self.pop_next().map(|event| (event, self.data_len()))
    }

    fn data_len(&self) -> usize {
        self.data_events.len()
    }

    fn total_len(&self) -> usize {
        self.control_events.len() + self.data_events.len()
    }
}

pub struct PushOutcome<E> {
    pub dropped: Option<E>,
    pub data_len: usize,
}

pub struct StreamQueue<E> {
    data_depth: usize,
    state: Mutex<QueueState<E>>,
    cv: Condvar,
}

impl<E: StreamEvent> StreamQueue<E> {
    pub fn new(depth: usize) -> Self {
        let depth = depth.max(1);
        Self {
            data_depth: depth,
            state: Mutex::new(QueueState {
                closed: false,
                data_events: VecDeque::with_capacity(depth),
                control_events: VecDeque::new(),
            }),
            cv: Condvar::new(),
        }
    }

    pub fn push(&self, event: E) -> PushOutcome<E> {
        let mut guard = self.state.lock().unwrap();
        if guard.closed {
            return PushOutcome {
                dropped: Some(event),
                data_len: guard.data_len(),
            };
        }

        match event.delivery_lane() {
            DeliveryLane::Control => {
                guard.control_events.push_back(event);
                let data_len = guard.data_len();
                self.cv.notify_one();
                PushOutcome {
                    dropped: None,
                    data_len,
                }
            }
            DeliveryLane::Data => {
                let dropped = if guard.data_events.len() >= self.data_depth {
                    guard.data_events.pop_front()
                } else {
                    None
                };
                guard.data_events.push_back(event);
                let data_len = guard.data_len();
                self.cv.notify_one();
                PushOutcome { dropped, data_len }
            }
        }
    }

    pub fn recv(&self) -> Result<(E, usize), RecvError> {
        let mut guard = self.state.lock().unwrap();
        loop {
            if let Some(outcome) = guard.pop_next_with_data_len() {
                return Ok(outcome);
            }
            if guard.closed {
                return Err(RecvError::Disconnected);
            }
            guard = self.cv.wait(guard).unwrap();
        }
    }

    pub fn try_recv(&self) -> Result<(E, usize), TryRecvError> {
        let mut guard = self.state.lock().unwrap();
        if let Some(outcome) = guard.pop_next_with_data_len() {
            return Ok(outcome);
        }
        if guard.closed {
            return Err(TryRecvError::Disconnected);
        }
        Err(TryRecvError::Empty)
    }

    pub fn recv_timeout(&self, timeout: Duration) -> Result<(E, usize), RecvTimeoutError> {
        let mut guard = self.state.lock().unwrap();
        if let Some(outcome) = guard.pop_next_with_data_len() {
            return Ok(outcome);
        }

        if guard.closed {
            return Err(RecvTimeoutError::Disconnected);
        }

        let (mut guard, wait_result) = self
            .cv
            .wait_timeout_while(guard, timeout, |state| !state.closed && state.is_empty())
            .unwrap();

        if let Some(outcome) = guard.pop_next_with_data_len() {
            return Ok(outcome);
        }

        if guard.closed {
            return Err(RecvTimeoutError::Disconnected);
        }

        if wait_result.timed_out() {
            Err(RecvTimeoutError::Timeout)
        } else {
            Err(RecvTimeoutError::Disconnected)
        }
    }

    pub fn close(&self) {
        let mut guard = self.state.lock().unwrap();
        guard.closed = true;
        self.cv.notify_all();
    }

    pub fn drain(&self) -> Vec<E> {
        let mut guard = self.state.lock().unwrap();
        let mut drained = Vec::with_capacity(guard.total_len());
        drained.extend(guard.control_events.drain(..));
        drained.extend(guard.data_events.drain(..));
        drained
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use crate::event::{DeliveryLane, StreamEvent};
    use crate::timestamp::StreamTimestamp;

    use super::StreamQueue;

    #[derive(Clone, Debug)]
    enum TestEvent {
        Data(()),
        Control(&'static str),
    }

    impl StreamEvent for TestEvent {
        fn delivery_lane(&self) -> DeliveryLane {
            match self {
                TestEvent::Data(_) => DeliveryLane::Data,
                TestEvent::Control(_) => DeliveryLane::Control,
            }
        }

        fn timestamp(&self) -> Option<&StreamTimestamp> {
            None
        }
    }

    #[test]
    fn control_events_are_prioritized() {
        let queue = StreamQueue::new(2);
        queue.push(TestEvent::Data(()));
        queue.push(TestEvent::Control("stop"));

        let first = queue.recv().unwrap().0;
        let second = queue.recv().unwrap().0;

        assert!(matches!(first, TestEvent::Control("stop")));
        assert!(matches!(second, TestEvent::Data(())));
    }

    #[test]
    fn saturated_data_lane_drops_oldest_event() {
        let queue = StreamQueue::new(2);
        assert!(queue.push(TestEvent::Data(())).dropped.is_none());
        assert!(queue.push(TestEvent::Data(())).dropped.is_none());

        let dropped = queue.push(TestEvent::Data(())).dropped;
        assert!(matches!(dropped, Some(TestEvent::Data(()))));
    }

    #[test]
    fn reported_len_tracks_data_lane_only() {
        let queue = StreamQueue::new(2);
        assert_eq!(queue.push(TestEvent::Data(())).data_len, 1);
        assert_eq!(queue.push(TestEvent::Control("pause")).data_len, 1);

        let (_, data_len) = queue.recv().unwrap();
        assert_eq!(data_len, 1);
        let (_, data_len) = queue.recv().unwrap();
        assert_eq!(data_len, 0);
    }

    #[test]
    fn drain_returns_control_then_data() {
        let queue = StreamQueue::new(2);
        queue.push(TestEvent::Data(()));
        queue.push(TestEvent::Control("end"));
        queue.push(TestEvent::Data(()));

        let drained = queue.drain();
        assert!(matches!(drained[0], TestEvent::Control("end")));
        assert!(matches!(drained[1], TestEvent::Data(())));
        assert!(matches!(drained[2], TestEvent::Data(())));
    }

    #[test]
    fn recv_timeout_reports_disconnect_after_close() {
        let queue = StreamQueue::<TestEvent>::new(1);
        queue.close();
        assert!(matches!(
            queue.recv_timeout(Duration::from_millis(1)),
            Err(crate::error::RecvTimeoutError::Disconnected)
        ));
    }
}
