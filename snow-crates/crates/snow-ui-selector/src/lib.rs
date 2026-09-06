use std::marker::PhantomData;
use std::sync::Once;

mod com;
mod geometry;
mod msaa;
mod spatial;
mod uia;
mod window;

use windows::Win32::Foundation::{HWND, POINT, RECT};
use windows::Win32::UI::HiDpi::{
    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
    DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, SetProcessDpiAwarenessContext,
};
use windows::core::Result;

static DPI_AWARENESS_INIT: Once = Once::new();

/// Enables process DPI awareness so cursor and accessibility bounds share the same coordinates.
/// Call this as early as possible during startup for the best compatibility on scaled displays.
pub fn enable_high_dpi_support() {
    DPI_AWARENESS_INIT.call_once(|| unsafe {
        let contexts = [
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE,
            DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
        ];

        for context in contexts {
            if SetProcessDpiAwarenessContext(context).is_ok() {
                break;
            }
        }
    });
}

/// Bounding region of a UI element found by hit-testing.
#[derive(Clone, Copy, Debug)]
pub struct ElementRect {
    rect: RECT,
}

impl ElementRect {
    pub(crate) fn new(rect: RECT) -> Self {
        Self { rect }
    }

    pub fn left(&self) -> i32 {
        self.rect.left
    }

    pub fn top(&self) -> i32 {
        self.rect.top
    }

    pub fn right(&self) -> i32 {
        self.rect.right
    }

    pub fn bottom(&self) -> i32 {
        self.rect.bottom
    }

    pub fn width(&self) -> i32 {
        self.rect.right - self.rect.left
    }

    pub fn height(&self) -> i32 {
        self.rect.bottom - self.rect.top
    }
}

/// Controls what `hit_test_point` returns.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum HitTestMode {
    /// Traverse the UI element tree and return the full region path from the
    /// smallest element up to the window.
    #[default]
    UiElement,
    /// Return only the window bounding rectangle — no element traversal.
    /// This is the fastest mode: it only needs the window geometry.
    Window,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum AccessibilityBackend {
    #[default]
    Uia,
    Msaa,
}

/// Enum-dispatched backend — eliminates `Box<dyn Backend>` overhead and keeps
/// the `AccessibilityBackend` variant in sync with the concrete implementation.
enum BackendImpl {
    Uia(uia::UiaBackend),
    Msaa(msaa::MsaaBackend),
}

impl BackendImpl {
    fn refresh(&mut self, excluded_hwnds: &[HWND]) -> Result<()> {
        match self {
            Self::Uia(b) => b.refresh(excluded_hwnds),
            Self::Msaa(b) => b.refresh(excluded_hwnds),
        }
    }

    fn release_cache(&mut self) {
        match self {
            Self::Uia(b) => b.release_cache(),
            Self::Msaa(b) => b.release_cache(),
        }
    }

    fn hit_test_point(
        &mut self,
        point: POINT,
        mode: HitTestMode,
    ) -> Result<Option<Vec<ElementRect>>> {
        match self {
            Self::Uia(b) => b.hit_test_point(point, mode),
            Self::Msaa(b) => b.hit_test_point(point, mode),
        }
    }

    fn variant(&self) -> AccessibilityBackend {
        match self {
            Self::Uia(_) => AccessibilityBackend::Uia,
            Self::Msaa(_) => AccessibilityBackend::Msaa,
        }
    }
}

/// The main entry point for hit-testing UI elements.
///
/// This type is intentionally `!Send` and `!Sync` because the underlying COM
/// interfaces (UIA / MSAA) are apartment-bound.  Create and use it on the same
/// thread.
pub struct ElementRegionService {
    _com: com::ComApartment,
    backend: BackendImpl,
    /// Prevent `Send` and `Sync` — COM pointers are apartment-bound.
    _not_send: PhantomData<*mut ()>,
}

impl ElementRegionService {
    pub fn new() -> Result<Self> {
        Self::with_backend(AccessibilityBackend::default())
    }

    pub fn with_backend(backend: AccessibilityBackend) -> Result<Self> {
        Self::with_backend_excluding_hwnds(backend, &[])
    }

    pub fn with_backend_excluding_hwnds(
        backend: AccessibilityBackend,
        excluded_hwnds: &[HWND],
    ) -> Result<Self> {
        enable_high_dpi_support();
        let com = com::ComApartment::new()?;
        let backend = match backend {
            AccessibilityBackend::Uia => {
                BackendImpl::Uia(uia::UiaBackend::new_excluding_hwnds(excluded_hwnds)?)
            }
            AccessibilityBackend::Msaa => {
                BackendImpl::Msaa(msaa::MsaaBackend::new_excluding_hwnds(excluded_hwnds)?)
            }
        };

        Ok(Self {
            _com: com,
            backend,
            _not_send: PhantomData,
        })
    }

    pub fn backend(&self) -> AccessibilityBackend {
        self.backend.variant()
    }

    pub fn refresh(&mut self) -> Result<()> {
        self.refresh_excluding_hwnds(&[])
    }

    pub fn refresh_excluding_hwnds(&mut self, excluded_hwnds: &[HWND]) -> Result<()> {
        self.backend.refresh(excluded_hwnds)
    }

    /// Releases the current desktop/window snapshot while keeping the COM
    /// apartment and backend automation objects alive for reuse.
    pub fn release_cache(&mut self) {
        self.backend.release_cache();
    }

    pub fn hit_test_point(
        &mut self,
        point: POINT,
        mode: HitTestMode,
    ) -> Result<Option<Vec<ElementRect>>> {
        self.backend.hit_test_point(point, mode)
    }
}
