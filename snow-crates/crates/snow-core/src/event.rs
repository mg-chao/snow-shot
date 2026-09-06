//! Event abstractions shared across source crates.

use crate::timestamp::StreamTimestamp;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum DeliveryLane {
    #[default]
    Data,
    Control,
}

/// Trait for stream events that support lifecycle introspection and timestamp access.
pub trait StreamEvent: Send + 'static {
    fn is_paused(&self) -> bool {
        false
    }

    fn is_resumed(&self) -> bool {
        false
    }

    fn is_stream_ended(&self) -> bool {
        false
    }

    fn is_error(&self) -> bool {
        false
    }

    fn delivery_lane(&self) -> DeliveryLane {
        DeliveryLane::Data
    }

    fn timestamp(&self) -> Option<&StreamTimestamp> {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::timestamp::{StreamTimestamp, TickFormat};
    use proptest::prelude::*;
    use std::time::Instant;

    #[derive(Clone, Debug)]
    struct MockEvent {
        paused: bool,
        resumed: bool,
        stream_ended: bool,
        error: bool,
        lane: DeliveryLane,
        timestamp: Option<StreamTimestamp>,
    }

    impl StreamEvent for MockEvent {
        fn is_paused(&self) -> bool {
            self.paused
        }

        fn is_resumed(&self) -> bool {
            self.resumed
        }

        fn is_stream_ended(&self) -> bool {
            self.stream_ended
        }

        fn is_error(&self) -> bool {
            self.error
        }

        fn delivery_lane(&self) -> DeliveryLane {
            self.lane
        }

        fn timestamp(&self) -> Option<&StreamTimestamp> {
            self.timestamp.as_ref()
        }
    }

    fn arb_stream_timestamp() -> impl Strategy<Value = StreamTimestamp> {
        (prop::bool::ANY, any::<i64>(), prop::bool::ANY).prop_map(|(use_raw, ticks, is_hns100)| {
            StreamTimestamp {
                instant: Instant::now(),
                raw_os_ticks: if use_raw { Some(ticks) } else { None },
                tick_format: if is_hns100 {
                    TickFormat::Hns100
                } else {
                    TickFormat::RawQpc
                },
            }
        })
    }

    proptest! {
        #[test]
        fn lifecycle_methods_delegate(
            paused in prop::bool::ANY,
            resumed in prop::bool::ANY,
            ended in prop::bool::ANY,
            error in prop::bool::ANY,
            lane in prop_oneof![Just(DeliveryLane::Data), Just(DeliveryLane::Control)],
            timestamp in prop::option::of(arb_stream_timestamp()),
        ) {
            let event = MockEvent {
                paused,
                resumed,
                stream_ended: ended,
                error,
                lane,
                timestamp,
            };

            prop_assert_eq!(event.is_paused(), paused);
            prop_assert_eq!(event.is_resumed(), resumed);
            prop_assert_eq!(event.is_stream_ended(), ended);
            prop_assert_eq!(event.is_error(), error);
            prop_assert_eq!(event.delivery_lane(), lane);
            prop_assert_eq!(event.timestamp().is_some(), event.timestamp.is_some());
        }
    }
}
