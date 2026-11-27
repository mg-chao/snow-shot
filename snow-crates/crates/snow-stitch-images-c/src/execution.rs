//! Own the scrolling stitch workers for the lifetime of a session.

use std::sync::OnceLock;

use rayon::{ThreadPool, ThreadPoolBuildError, ThreadPoolBuilder};
use snow_stitch_images::StitchError;

#[cfg(target_os = "macos")]
const QOS_CLASS_DEFAULT: u32 = 0x15;

#[derive(Default)]
pub(super) struct Execution {
    pool: OnceLock<Result<ThreadPool, String>>,
}

#[cfg(target_os = "macos")]
fn apply_default_qos() -> i32 {
    unsafe extern "C" {
        fn pthread_set_qos_class_self_np(class: u32, relative_priority: i32) -> i32;
    }
    // SAFETY: Default is a supported Darwin class and zero is a valid relative
    // priority. This changes only the calling, dedicated stitching worker.
    unsafe { pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0) }
}

#[cfg(target_os = "macos")]
fn initialize_pool_qos(
    pool: &ThreadPool,
    initialize_worker: impl Fn(usize) -> i32 + Sync,
) -> Result<(), String> {
    // Complete initialization on every worker before the pool is
    // published. A native failure must not run stitch work with inherited QoS.
    let results = pool.broadcast(|context| initialize_worker(context.index()));
    for (worker, code) in results.into_iter().enumerate() {
        if code != 0 {
            return Err(format!(
                "could not set stitching worker {worker} to Default QoS: {code}"
            ));
        }
    }
    Ok(())
}

fn build_pool(
    build: impl FnOnce() -> Result<ThreadPool, ThreadPoolBuildError>,
) -> Result<ThreadPool, String> {
    let pool = build().map_err(|error| format!("could not create stitching workers: {error}"))?;
    #[cfg(target_os = "macos")]
    initialize_pool_qos(&pool, |_| apply_default_qos())?;
    Ok(pool)
}

impl Execution {
    fn pool(&self) -> Result<&ThreadPool, StitchError> {
        // Keep workers across resets of one session and let Rayon retire them
        // when its session is destroyed.
        self.pool
            .get_or_init(|| {
                build_pool(|| {
                    ThreadPoolBuilder::new()
                        .thread_name(|index| format!("snow-stitch-{index}"))
                        .build()
                })
            })
            .as_ref()
            .map_err(|message| StitchError::InvalidFrame {
                message: message.clone(),
            })
    }

    pub(super) fn install<F, R>(&self, operation: F) -> Result<R, StitchError>
    where
        F: FnOnce() -> R + Send,
        R: Send,
    {
        let pool = self.pool()?;
        #[cfg(feature = "perf-instrumentation")]
        {
            let (result, snapshot) = pool.install(|| snow_stitch_images::perf::capture(operation));
            snow_stitch_images::perf::merge(snapshot);
            Ok(result)
        }
        #[cfg(not(feature = "perf-instrumentation"))]
        {
            Ok(pool.install(operation))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::*;
    #[cfg(target_os = "macos")]
    use rayon::prelude::*;
    #[cfg(target_os = "macos")]
    use snow_core::qos::{ApplicationQos, apply_current_thread, initialize_application_qos};
    use std::thread;

    #[cfg(target_os = "macos")]
    fn native_qos() -> u32 {
        unsafe extern "C" {
            fn qos_class_self() -> u32;
        }
        // SAFETY: the function reads the calling thread's native QoS class.
        unsafe { qos_class_self() }
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn application_profiles_do_not_change_stitching_qos() {
        for profile in 0..5 {
            let status = std::process::Command::new(std::env::current_exe().unwrap())
                .args([
                    "--exact",
                    "execution::tests::application_profile_child",
                    "--nocapture",
                ])
                .env("SNOW_STITCH_QOS_TEST_PROFILE", profile.to_string())
                .status()
                .unwrap();
            assert!(status.success(), "application QoS profile {profile}");
        }
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn application_profile_child() {
        let Ok(profile) = std::env::var("SNOW_STITCH_QOS_TEST_PROFILE") else {
            return;
        };
        let qos = ApplicationQos::from_code(profile.parse().unwrap()).unwrap();
        assert!(initialize_application_qos(qos, None));
        apply_current_thread();
        assert_eq!(native_qos(), qos.native_class());

        let execution = Execution::default();
        let pool = execution.pool().unwrap();
        let workers = pool.broadcast(|_| (thread::current().id(), native_qos()));
        assert!(workers.iter().all(|(_, class)| *class == QOS_CLASS_DEFAULT));
        let nested = execution
            .install(|| {
                rayon::join(
                    || (native_qos(), pool.current_thread_index().is_some()),
                    || {
                        (0..128)
                            .into_par_iter()
                            .map(|_| (native_qos(), pool.current_thread_index().is_some()))
                            .collect::<Vec<_>>()
                    },
                )
            })
            .unwrap();
        assert_eq!(nested.0, (QOS_CLASS_DEFAULT, true));
        assert!(nested.1.iter().all(|value| *value == nested.0));
        assert_eq!(native_qos(), qos.native_class());

        let (sender, receiver) = std::sync::mpsc::channel();
        rayon::spawn(move || sender.send(native_qos()).unwrap());
        assert_eq!(receiver.recv().unwrap(), qos.native_class());

        check_ffi_inputs(qos.native_class());
        assert_eq!(
            pool.broadcast(|_| (thread::current().id(), native_qos())),
            workers
        );
        assert_eq!(native_qos(), qos.native_class());
    }

    #[cfg(target_os = "macos")]
    struct ObservedPixels {
        pixels: Vec<u8>,
        released: Arc<Mutex<Vec<(u32, bool)>>>,
    }

    #[cfg(target_os = "macos")]
    unsafe extern "C" fn release_observed_pixels(context: *mut c_void) {
        // SAFETY: each transferred context owns one Box and is released once.
        let owner = unsafe { Box::from_raw(context.cast::<ObservedPixels>()) };
        let on_stitch_worker = thread::current()
            .name()
            .is_some_and(|name| name.starts_with("snow-stitch-"));
        owner
            .released
            .lock()
            .unwrap()
            .push((native_qos(), on_stitch_worker));
    }

    #[test]
    fn wide_seed_frame_does_not_start_workers() {
        // Only a fresh test process can check the one-time global pool initialization.
        const CHILD: &str = "SNOW_STITCH_WIDE_SEED_CHILD";
        if std::env::var_os(CHILD).is_none() {
            let status = std::process::Command::new(std::env::current_exe().unwrap())
                .args([
                    "--exact",
                    "execution::tests::wide_seed_frame_does_not_start_workers",
                ])
                .env(CHILD, "1")
                .status()
                .unwrap();
            assert!(status.success());
            return;
        }

        let mut session = StitchSession::new(StitchConfig::default()).unwrap();
        let frame = Frame::new(1024, 256, PixelFormat::Rgba8, vec![0; 1024 * 256 * 4]);
        let outcome = session.push_owned_frame(frame).unwrap();
        assert!(matches!(outcome.event, FrameEvent::Initial));
        assert!(session.execution.pool.get().is_none());
        rayon::ThreadPoolBuilder::new()
            .num_threads(1)
            .build_global()
            .expect("the seed frame must leave the global pool uninitialized");
    }

    #[test]
    fn ffi_session_reuses_workers_until_destroy() {
        use std::{collections::HashSet, sync::mpsc, time::Duration};

        struct WorkerExit(mpsc::Sender<thread::ThreadId>, thread::ThreadId);

        impl Drop for WorkerExit {
            fn drop(&mut self) {
                let _ = self.0.send(self.1);
            }
        }

        thread_local! {
            static EXIT: RefCell<Option<WorkerExit>> = const { RefCell::new(None) };
        }

        fn push_frame(
            session: *mut SnowStitchSessionImpl,
            frame_pool: *mut SnowStitchFramePoolImpl,
        ) {
            let mut frame = unsafe { snow_stitch_frame_pool_acquire(frame_pool) };
            assert!(!frame.is_null());
            let buffer = unsafe { &mut *frame }.buffer.as_mut().unwrap();
            buffer.as_mut_rgba_bytes().fill(0x7f);
            let mut outcome: SnowStitchFrameOutcome = unsafe { std::mem::zeroed() };
            assert_eq!(
                unsafe { snow_stitch_session_push_owned(session, &mut frame, &mut outcome) },
                1
            );
            assert!(frame.is_null());
        }

        fn initialized_pool(session: &SnowStitchSessionImpl) -> &ThreadPool {
            session
                .session
                .execution
                .pool
                .get()
                .unwrap()
                .as_ref()
                .unwrap()
        }

        let frame_pool = snow_stitch_frame_pool_create(5, 5, 1);
        let config = ffi_config(StitchConfig::default());
        let session = unsafe { snow_stitch_session_create(&config) };
        assert!(!session.is_null() && !frame_pool.is_null());
        assert_eq!(unsafe { snow_stitch_session_reset(session) }, 1);
        push_frame(session, frame_pool);
        assert!(unsafe { &*session }.session.execution.pool.get().is_none());

        push_frame(session, frame_pool);
        let (sender, receiver) = mpsc::channel();
        let workers = initialized_pool(unsafe { &*session }).broadcast(|_| {
            let id = thread::current().id();
            EXIT.with_borrow_mut(|exit| *exit = Some(WorkerExit(sender.clone(), id)));
            id
        });
        drop(sender);
        assert!(!workers.is_empty());

        assert_eq!(unsafe { snow_stitch_session_reset(session) }, 1);
        push_frame(session, frame_pool);
        push_frame(session, frame_pool);
        let pool = initialized_pool(unsafe { &*session });
        assert_eq!(pool.broadcast(|_| thread::current().id()), workers);
        assert_eq!(receiver.try_recv(), Err(mpsc::TryRecvError::Empty));
        unsafe { snow_stitch_session_destroy(session) };

        let exited: HashSet<_> = (0..workers.len())
            .map(|_| receiver.recv_timeout(Duration::from_secs(5)).unwrap())
            .collect();
        assert_eq!(exited, workers.into_iter().collect());

        let next_session = unsafe { snow_stitch_session_create(&config) };
        assert!(!next_session.is_null());
        push_frame(next_session, frame_pool);
        push_frame(next_session, frame_pool);
        let pool = initialized_pool(unsafe { &*next_session });
        let next_workers = pool.broadcast(|_| thread::current().id());
        assert_eq!(next_workers.len(), exited.len());
        assert!(next_workers.iter().all(|id| !exited.contains(id)));
        unsafe {
            snow_stitch_session_destroy(next_session);
            snow_stitch_frame_pool_destroy(frame_pool);
        }
    }

    #[cfg(target_os = "macos")]
    fn push_external(
        session: *mut SnowStitchSessionImpl,
        released: &Arc<Mutex<Vec<(u32, bool)>>>,
        outcome: &mut SnowStitchFrameOutcome,
    ) {
        let owner = Box::new(ObservedPixels {
            pixels: vec![0x7f; 5 * 5 * 4],
            released: Arc::clone(released),
        });
        let bytes = owner.pixels.as_ptr();
        let context = Box::into_raw(owner).cast();
        assert_eq!(
            unsafe {
                snow_stitch_session_push_external_rgba(
                    session,
                    5,
                    5,
                    bytes,
                    100,
                    Some(release_observed_pixels),
                    context,
                    outcome,
                )
            },
            1
        );
    }

    #[cfg(target_os = "macos")]
    fn check_ffi_inputs(caller_qos: u32) {
        #[cfg(feature = "perf-instrumentation")]
        snow_stitch_images::perf::reset();
        let frame_pool = snow_stitch_frame_pool_create(5, 5, 1);
        let config = ffi_config(StitchConfig::default());
        let session = unsafe { snow_stitch_session_create(&config) };
        assert!(!frame_pool.is_null() && !session.is_null());
        let mut frame = unsafe { snow_stitch_frame_pool_acquire(frame_pool) };
        let buffer = unsafe { &mut *frame }.buffer.as_mut().unwrap();
        buffer.as_mut_rgba_bytes().fill(0x7f);
        let mut outcome: SnowStitchFrameOutcome = unsafe { std::mem::zeroed() };
        assert_eq!(
            unsafe { snow_stitch_session_push_owned(session, &mut frame, &mut outcome) },
            1
        );
        assert!(frame.is_null());
        assert!(matches!(outcome.event, SnowStitchFrameEvent::Initial));

        let released = Arc::new(Mutex::new(Vec::new()));
        push_external(session, &released, &mut outcome);
        assert!(matches!(outcome.event, SnowStitchFrameEvent::Duplicate));
        assert_eq!(*released.lock().unwrap(), [(QOS_CLASS_DEFAULT, true)]);
        assert_eq!(unsafe { snow_stitch_session_reset(session) }, 1);

        push_external(session, &released, &mut outcome);
        assert!(matches!(outcome.event, SnowStitchFrameEvent::Initial));
        push_external(session, &released, &mut outcome);
        assert!(matches!(outcome.event, SnowStitchFrameEvent::Duplicate));
        assert_eq!(
            *released.lock().unwrap(),
            [(QOS_CLASS_DEFAULT, true), (QOS_CLASS_DEFAULT, true)]
        );
        let snapshot = unsafe { snow_stitch_session_snapshot(session, 0, 5) };
        assert!(!snapshot.is_null());
        assert_eq!(unsafe { snow_stitch_session_reset(session) }, 1);
        assert_eq!(released.lock().unwrap().last(), Some(&(caller_qos, false)));

        push_external(session, &released, &mut outcome);
        unsafe {
            snow_stitch_session_destroy(session);
            snow_stitch_snapshot_destroy(snapshot);
            snow_stitch_frame_pool_destroy(frame_pool);
        }
        assert_eq!(released.lock().unwrap().last(), Some(&(caller_qos, false)));
        #[cfg(feature = "perf-instrumentation")]
        {
            use snow_stitch_images::perf::{Stage, snapshot};
            let counters = snapshot();
            assert_eq!(counters.calls[Stage::PushTotal as usize], 5);
            assert_eq!(counters.calls[Stage::Initialization as usize], 3);
            assert_eq!(counters.calls[Stage::FrameFreeze as usize], 5);
        }
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn native_initialization_failure_rejects_pool() {
        let initialized = AtomicUsize::new(0);
        let pool = ThreadPoolBuilder::new().num_threads(2).build().unwrap();
        let result = initialize_pool_qos(&pool, |index| {
            initialized.fetch_add(1, Ordering::SeqCst);
            if index == 1 { 22 } else { apply_default_qos() }
        });
        assert!(result.is_err());
        assert_eq!(initialized.load(Ordering::SeqCst), 2);
        assert!(
            result
                .err()
                .unwrap()
                .contains("worker 1 to Default QoS: 22")
        );
    }

    #[test]
    fn worker_creation_failure_is_reported() {
        let result = build_pool(|| {
            ThreadPoolBuilder::new()
                .spawn_handler(|_| Err(std::io::Error::other("injected spawn failure")))
                .build()
        });
        assert!(result.err().unwrap().contains("injected spawn failure"));
    }

    #[cfg(feature = "perf-instrumentation")]
    #[test]
    fn nested_install_preserves_caller_and_worker_performance_records() {
        use snow_stitch_images::perf::{Scope, Stage, reset, snapshot};

        let execution = Execution::default();
        reset();
        Scope::new(Stage::FrameFreeze).finish();
        execution
            .install(|| {
                Scope::new(Stage::Initialization).finish();
                execution
                    .install(|| Scope::new(Stage::Grayscale).finish())
                    .unwrap();
                Scope::new(Stage::CandidateScoring).finish();
            })
            .unwrap();
        let failure = execution
            .install(|| {
                Scope::new(Stage::Refinement).finish();
                Err::<(), _>("injected stitch failure")
            })
            .unwrap();
        assert_eq!(failure, Err("injected stitch failure"));
        let counters = snapshot();
        for stage in [
            Stage::FrameFreeze,
            Stage::Initialization,
            Stage::Grayscale,
            Stage::CandidateScoring,
            Stage::Refinement,
        ] {
            assert_eq!(counters.calls[stage as usize], 1);
        }
        assert_eq!(counters.calls.iter().sum::<u64>(), 5);
    }
}
