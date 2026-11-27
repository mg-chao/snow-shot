//! Inclusive wall-clock scopes on the calling thread. Parallel work is timed
//! around its join, so worker CPU times are not mistaken for elapsed latency.

#[repr(usize)]
#[derive(Clone, Copy)]
pub enum Stage {
    FrameFreeze,
    DuplicateCheck,
    ReferencePreparation,
    Grayscale,
    SimilarityMaps,
    FeatureExtraction,
    DescriptorMatching,
    CandidateScoring,
    Refinement,
    CanvasComposition,
    ReferenceSynthesis,
    PreviewScaling,
    PushTotal,
    Initialization,
}

pub const STAGE_COUNT: usize = Stage::Initialization as usize + 1;

#[cfg(feature = "perf-instrumentation")]
mod enabled {
    use super::{STAGE_COUNT, Stage};
    use std::{cell::RefCell, marker::PhantomData, rc::Rc, time::Instant};

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct Snapshot {
        pub elapsed_ns: [u64; STAGE_COUNT],
        pub calls: [u64; STAGE_COUNT],
    }

    thread_local! {
        static RECORD: RefCell<Snapshot> = RefCell::new(Snapshot::default());
    }

    pub fn reset() {
        RECORD.with_borrow_mut(|record| *record = Snapshot::default());
    }

    pub fn snapshot() -> Snapshot {
        RECORD.with_borrow(|record| *record)
    }

    struct RestoreRecord(Snapshot);

    impl Drop for RestoreRecord {
        fn drop(&mut self) {
            RECORD.with_borrow_mut(|record| *record = self.0);
        }
    }

    /// Measures one operation independently of the current thread's existing
    /// counters. Nested captures and unwinding restore the enclosing record.
    pub fn capture<F, R>(operation: F) -> (R, Snapshot)
    where
        F: FnOnce() -> R,
    {
        let restore = RestoreRecord(RECORD.with_borrow_mut(std::mem::take));
        let result = operation();
        let measured = snapshot();
        drop(restore);
        (result, measured)
    }

    /// Adds measurements transferred from another execution context to the
    /// current thread's counters without overflowing them.
    pub fn merge(measured: Snapshot) {
        RECORD.with_borrow_mut(|record| {
            for index in 0..STAGE_COUNT {
                record.elapsed_ns[index] =
                    record.elapsed_ns[index].saturating_add(measured.elapsed_ns[index]);
                record.calls[index] = record.calls[index].saturating_add(measured.calls[index]);
            }
        });
    }

    pub struct Scope {
        stage: Stage,
        started: Instant,
        _thread_bound: PhantomData<Rc<()>>,
    }

    impl Scope {
        pub fn new(stage: Stage) -> Self {
            Self {
                stage,
                started: Instant::now(),
                _thread_bound: PhantomData,
            }
        }
        pub fn finish(self) {}
    }

    impl Drop for Scope {
        fn drop(&mut self) {
            let elapsed = self.started.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;
            RECORD.with_borrow_mut(|record| {
                let index = self.stage as usize;
                record.elapsed_ns[index] = record.elapsed_ns[index].saturating_add(elapsed);
                record.calls[index] = record.calls[index].saturating_add(1);
            });
        }
    }
}

#[cfg(feature = "perf-instrumentation")]
pub use enabled::{Scope, Snapshot, capture, merge, reset, snapshot};

#[cfg(not(feature = "perf-instrumentation"))]
pub struct Scope;

#[cfg(not(feature = "perf-instrumentation"))]
impl Scope {
    #[inline(always)]
    pub fn new(_: Stage) -> Self {
        Self
    }
    #[inline(always)]
    pub fn finish(self) {}
}

/// Join independent stages and return both workers' inclusive counters to the caller.
pub(crate) fn join<A: Send, B: Send>(
    left: impl FnOnce() -> A + Send,
    right: impl FnOnce() -> B + Send,
) -> (A, B) {
    #[cfg(feature = "perf-instrumentation")]
    {
        let ((a, first), (b, second)) = rayon::join(|| capture(left), || capture(right));
        merge(first);
        merge(second);
        (a, b)
    }
    #[cfg(not(feature = "perf-instrumentation"))]
    {
        rayon::join(left, right)
    }
}

#[cfg(all(test, feature = "perf-instrumentation"))]
mod tests {
    use super::*;

    #[test]
    fn scopes_record_early_returns_and_isolate_threads() {
        reset();
        let early_return = || {
            let _scope = Scope::new(Stage::DuplicateCheck);
            Err::<(), _>("duplicate")
        };
        assert!(early_return().is_err());
        assert_eq!(snapshot().calls[Stage::DuplicateCheck as usize], 1);
        std::thread::spawn(|| {
            assert_eq!(snapshot(), Snapshot::default());
            let _scope = Scope::new(Stage::FrameFreeze);
        })
        .join()
        .unwrap();
        assert_eq!(snapshot().calls[Stage::FrameFreeze as usize], 0);
        reset();
        assert_eq!(snapshot(), Snapshot::default());
    }

    #[test]
    fn repeated_captures_restore_the_caller_and_merge_independently() {
        reset();
        let existing = Snapshot {
            elapsed_ns: [17; STAGE_COUNT],
            calls: [3; STAGE_COUNT],
        };
        merge(existing);
        let index = Stage::PushTotal as usize;
        for expected_calls in 4..=5 {
            let (result, measured) = capture(|| {
                assert_eq!(snapshot(), Snapshot::default());
                let scope = Scope::new(Stage::PushTotal);
                scope.finish();
                42
            });
            assert_eq!(result, 42);
            assert_eq!(measured.calls[index], 1);
            assert_eq!(snapshot().calls[index], expected_calls - 1);
            assert_eq!(snapshot().calls[Stage::FrameFreeze as usize], 3);
            merge(measured);
            assert_eq!(snapshot().calls[index], expected_calls);
        }
    }

    #[test]
    fn joined_stages_contribute_once_to_the_calling_thread() {
        reset();
        Scope::new(Stage::PushTotal).finish();
        assert_eq!(
            join(
                || {
                    Scope::new(Stage::Grayscale).finish();
                    3
                },
                || {
                    Scope::new(Stage::FeatureExtraction).finish();
                    7
                }
            ),
            (3, 7)
        );
        let record = snapshot();
        for stage in [Stage::PushTotal, Stage::Grayscale, Stage::FeatureExtraction] {
            assert_eq!(record.calls[stage as usize], 1);
        }
    }

    #[test]
    fn nested_captures_keep_enclosing_measurements_separate() {
        reset();
        let existing = Snapshot {
            elapsed_ns: [19; STAGE_COUNT],
            calls: [5; STAGE_COUNT],
        };
        merge(existing);
        let (inner, outer) = capture(|| {
            let _outer_scope = Scope::new(Stage::PushTotal);
            Scope::new(Stage::FrameFreeze).finish();
            let (_, inner) = capture(|| {
                assert_eq!(snapshot(), Snapshot::default());
                let _inner_scope = Scope::new(Stage::DuplicateCheck);
            });
            assert_eq!(snapshot().calls[Stage::FrameFreeze as usize], 1);
            assert_eq!(snapshot().calls[Stage::DuplicateCheck as usize], 0);
            inner
        });
        assert_eq!(snapshot(), existing);
        assert_eq!(inner.calls[Stage::DuplicateCheck as usize], 1);
        assert_eq!(inner.calls[Stage::PushTotal as usize], 0);
        assert_eq!(outer.calls[Stage::PushTotal as usize], 1);
        assert_eq!(outer.calls[Stage::FrameFreeze as usize], 1);
        assert_eq!(outer.calls[Stage::DuplicateCheck as usize], 0);
    }

    #[test]
    fn captures_measure_error_returns_and_restore_after_panics() {
        reset();
        let existing = Snapshot {
            elapsed_ns: [23; STAGE_COUNT],
            calls: [7; STAGE_COUNT],
        };
        merge(existing);
        let (result, measured) = capture(|| {
            let _scope = Scope::new(Stage::DuplicateCheck);
            Err::<(), _>("duplicate")
        });
        assert_eq!(result, Err("duplicate"));
        assert_eq!(measured.calls[Stage::DuplicateCheck as usize], 1);
        assert_eq!(snapshot(), existing);

        let (_, enclosing) = capture(|| {
            Scope::new(Stage::FrameFreeze).finish();
            let panic = std::panic::catch_unwind(|| {
                capture(|| {
                    let _scope = Scope::new(Stage::ReferencePreparation);
                    panic!("failed operation");
                });
            });
            assert!(panic.is_err());
            assert_eq!(snapshot().calls[Stage::FrameFreeze as usize], 1);
            assert_eq!(snapshot().calls[Stage::ReferencePreparation as usize], 0);
            Scope::new(Stage::PreviewScaling).finish();
        });
        assert_eq!(enclosing.calls[Stage::FrameFreeze as usize], 1);
        assert_eq!(enclosing.calls[Stage::PreviewScaling as usize], 1);
        assert_eq!(enclosing.calls[Stage::ReferencePreparation as usize], 0);
        assert_eq!(snapshot(), existing);

        let panic = std::panic::catch_unwind(|| {
            capture(|| {
                let _scope = Scope::new(Stage::PushTotal);
                panic!("failed top-level operation");
            });
        });
        assert!(panic.is_err());
        assert_eq!(snapshot(), existing);
    }

    #[test]
    fn merging_saturates_every_stage_counter() {
        reset();
        merge(Snapshot {
            elapsed_ns: [u64::MAX - 1; STAGE_COUNT],
            calls: [u64::MAX - 1; STAGE_COUNT],
        });
        merge(Snapshot {
            elapsed_ns: [2; STAGE_COUNT],
            calls: [3; STAGE_COUNT],
        });
        assert_eq!(
            snapshot(),
            Snapshot {
                elapsed_ns: [u64::MAX; STAGE_COUNT],
                calls: [u64::MAX; STAGE_COUNT],
            }
        );
        Scope::new(Stage::PushTotal).finish();
        assert_eq!(snapshot().elapsed_ns[Stage::PushTotal as usize], u64::MAX);
        assert_eq!(snapshot().calls[Stage::PushTotal as usize], u64::MAX);
    }
}
