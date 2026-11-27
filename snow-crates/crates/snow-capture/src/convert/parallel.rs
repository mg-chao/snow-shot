use rayon::prelude::{IntoParallelIterator, ParallelIterator};
#[cfg(test)]
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, OnceLock, Weak};

const PARALLEL_CHUNK_ALIGNMENT_PIXELS: usize = 256;
pub(crate) type ConversionPool = Arc<Option<rayon::ThreadPool>>;
static PROCESS_POOL: OnceLock<ConversionPool> = OnceLock::new();
static ACTIVE_POOL: Mutex<Weak<Option<rayon::ThreadPool>>> = Mutex::new(Weak::new());

#[cfg(test)]
static WORKER_EXITS: AtomicUsize = AtomicUsize::new(0);

#[cfg(test)]
pub(crate) fn pool_initialized() -> bool {
    ACTIVE_POOL
        .lock()
        .unwrap()
        .upgrade()
        .is_some_and(|pool| pool.is_some())
}

#[cfg(test)]
pub(crate) fn pool_worker_exits() -> usize {
    WORKER_EXITS.load(Ordering::Acquire)
}

/// Hold the conversion runtime for the process when explicitly requested.
pub(crate) fn warmup_pool(max_workers: usize) {
    let pool = PROCESS_POOL.get_or_init(|| acquire_pool(max_workers));
    if let Some(pool) = pool.as_ref() {
        pool.install(|| {});
    }
}

/// Prewarm conversion workers and share them among overlapping continuous
/// captures. The last session drops the pool unless a process owner exists.
pub(crate) fn acquire_capture_pool(max_workers: usize) -> ConversionPool {
    let pool = acquire_pool(max_workers);
    if let Some(pool) = pool.as_ref() {
        pool.install(|| {});
    }
    pool
}

#[inline(always)]
pub(crate) fn should_parallelize(
    pixel_count: usize,
    min_pixels: usize,
    min_chunk_pixels: usize,
    max_workers: usize,
) -> bool {
    let workers = conversion_workers(max_workers);
    if workers <= 1 {
        return false;
    }
    let min_chunk_total = min_chunk_pixels.saturating_mul(workers);
    pixel_count >= min_pixels.max(min_chunk_total)
}

#[inline(always)]
pub(crate) fn parallel_chunk_pixels(
    pixel_count: usize,
    min_chunk_pixels: usize,
    max_workers: usize,
) -> Option<usize> {
    let alignment = PARALLEL_CHUNK_ALIGNMENT_PIXELS.max(1);
    let workers = conversion_workers(max_workers);
    let mut chunk_pixels = pixel_count / workers;

    if chunk_pixels < min_chunk_pixels {
        return None;
    }

    chunk_pixels -= chunk_pixels % alignment;
    if chunk_pixels == 0 || pixel_count.div_ceil(chunk_pixels) < 2 {
        return None;
    }

    Some(chunk_pixels)
}

#[inline(always)]
pub(crate) fn ranges_overlap(src: *const u8, src_len: usize, dst: *mut u8, dst_len: usize) -> bool {
    let src_start = src as usize;
    let dst_start = dst as usize;

    let Some(src_end) = src_start.checked_add(src_len) else {
        return true;
    };
    let Some(dst_end) = dst_start.checked_add(dst_len) else {
        return true;
    };

    src_start < dst_end && dst_start < src_end
}

#[inline]
pub(crate) fn conversion_workers(max_workers: usize) -> usize {
    static WORKERS: OnceLock<usize> = OnceLock::new();
    (*WORKERS.get_or_init(detect_conversion_worker_limit)).min(max_workers.max(1))
}

#[inline]
fn detect_conversion_worker_limit() -> usize {
    // Prefer physical cores so HDR/SDR CPU conversion doesn't over-subscribe
    // hyper-threads. Clamp to the currently available logical parallelism so
    // process affinity / cgroup limits are still respected.
    let logical = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(1)
        .max(1);
    let physical = num_cpus::get_physical();
    if physical == 0 {
        logical
    } else {
        physical.min(logical).max(1)
    }
}

#[inline]
#[cfg(windows)]
pub(crate) fn try_install_conversion_pool<F, R>(max_workers: usize, job: F) -> Option<R>
where
    F: FnOnce() -> R + Send,
    R: Send,
{
    acquire_pool(max_workers)
        .as_ref()
        .as_ref()
        .map(|pool| pool.install(job))
}

pub(crate) fn for_each_conversion_job<I, F>(max_workers: usize, items: I, job: F)
where
    I: IntoParallelIterator + IntoIterator<Item = <I as IntoParallelIterator>::Item> + Send,
    F: Fn(<I as IntoParallelIterator>::Item) + Send + Sync,
{
    match acquire_pool(max_workers).as_ref() {
        Some(pool) => pool.install(|| items.into_par_iter().for_each(job)),
        // A parallel iterator outside install would use Rayon's global workers.
        None => items.into_iter().for_each(job),
    }
}

fn acquire_pool(max_workers: usize) -> ConversionPool {
    let workers = conversion_workers(max_workers);
    if workers == 1 {
        return Arc::new(None);
    }
    let mut active = ACTIVE_POOL.lock().unwrap();
    if let Some(pool) = active.upgrade() {
        return pool;
    }
    let pool = Arc::new(new_pool(workers));
    *active = Arc::downgrade(&pool);
    pool
}

fn new_pool(workers: usize) -> Option<rayon::ThreadPool> {
    let builder = rayon::ThreadPoolBuilder::new()
        .start_handler(|_| snow_core::qos::apply_current_thread())
        .num_threads(workers);
    #[cfg(test)]
    let builder = builder.exit_handler(|_| {
        WORKER_EXITS.fetch_add(1, Ordering::Release);
    });
    builder.build().ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn serial_conversion_jobs_stay_on_the_calling_thread() {
        let _workers = acquire_capture_pool(usize::MAX);
        let caller = std::thread::current().id();
        let processed = AtomicUsize::new(0);
        for_each_conversion_job(1, 0..32, |_| {
            assert_eq!(std::thread::current().id(), caller);
            processed.fetch_add(1, Ordering::Relaxed);
        });
        assert_eq!(processed.load(Ordering::Relaxed), 32);
        #[cfg(windows)]
        assert!(try_install_conversion_pool(1, || 32).is_none());
    }
}
