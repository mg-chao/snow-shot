use windows::Win32::Foundation::{HWND, POINT, RECT};
use windows::Win32::System::Com::{CLSCTX_INPROC_SERVER, CoCreateInstance};
use windows::Win32::UI::Accessibility::{
    CUIAutomation, IUIAutomation, IUIAutomationCacheRequest, IUIAutomationCondition,
    IUIAutomationElement, IUIAutomationTreeWalker, TreeScope_Children, TreeScope_Element,
    UIA_BoundingRectanglePropertyId, UIA_IsOffscreenPropertyId, UIA_NativeWindowHandlePropertyId,
};
use windows::Win32::UI::WindowsAndMessaging::{GA_ROOT, GetAncestor};
use windows::core::Result;

use crate::ElementRect;
use crate::geometry::*;
use crate::spatial::*;
use crate::window;

const UIA_MAX_ANCESTOR_STEPS: usize = 80;
const UIA_MAX_HIT_PATH_RECTS: usize = 100;

struct UiaWindow {
    hwnd: HWND,
    bounds: RECT,
}

pub(crate) struct UiaBackend {
    automation: IUIAutomation,
    walker: IUIAutomationTreeWalker,
    raw_condition: IUIAutomationCondition,
    cache_request: IUIAutomationCacheRequest,
    windows: Vec<UiaWindow>,
    window_index: WindowSpatialIndex,
}

impl UiaBackend {
    pub(crate) fn new_excluding_hwnds(excluded_hwnds: &[HWND]) -> Result<Self> {
        let automation: IUIAutomation =
            unsafe { CoCreateInstance(&CUIAutomation, None, CLSCTX_INPROC_SERVER)? };
        let walker = unsafe { automation.RawViewWalker()? };
        let raw_condition = unsafe { automation.RawViewCondition()? };
        let cache_request = create_element_cache_request(&automation)?;
        let (windows, window_index) = build_uia_window_cache(excluded_hwnds)?;

        Ok(Self {
            automation,
            walker,
            raw_condition,
            cache_request,
            windows,
            window_index,
        })
    }

    pub(crate) fn refresh(&mut self, excluded_hwnds: &[HWND]) -> Result<()> {
        let (windows, window_index) = build_uia_window_cache(excluded_hwnds)?;
        self.windows = windows;
        self.window_index = window_index;
        Ok(())
    }

    pub(crate) fn release_cache(&mut self) {
        self.windows = Vec::new();
        self.window_index.release_cache();
    }

    pub(crate) fn hit_test_point(
        &mut self,
        point: POINT,
        mode: crate::HitTestMode,
    ) -> Result<Option<Vec<ElementRect>>> {
        let Some(window_idx) = self.window_at_point(point) else {
            return Ok(None);
        };

        let window = &self.windows[window_idx];
        match mode {
            crate::HitTestMode::Window => Ok(Some(vec![ElementRect::new(window.bounds)])),
            crate::HitTestMode::UiElement => {
                let mut rects = match self.hit_path_from_global_point(point, window)? {
                    Some(rects) => rects,
                    None => self
                        .hit_path_from_window_root(point, window)?
                        .unwrap_or_default(),
                };

                if rects.is_empty() {
                    rects.push(window.bounds);
                }
                Ok(Some(rects.into_iter().map(ElementRect::new).collect()))
            }
        }
    }

    fn window_at_point(&self, point: POINT) -> Option<usize> {
        self.window_index
            .window_at_point([point.x, point.y])
            .map(|window| window.cache_index)
    }

    fn hit_path_from_global_point(
        &self,
        point: POINT,
        window: &UiaWindow,
    ) -> Result<Option<Vec<RECT>>> {
        let Some(root) = self.root_from_window(window.hwnd) else {
            return Ok(None);
        };
        let element = unsafe {
            self.automation
                .ElementFromPointBuildCache(point, &self.cache_request)
        }?;
        if !self.element_is_inside_window(&element, &root, window)? {
            return Ok(None);
        }

        self.hit_path_from_element(element, point, window, &root)
            .map(Some)
    }

    fn hit_path_from_window_root(
        &self,
        point: POINT,
        window: &UiaWindow,
    ) -> Result<Option<Vec<RECT>>> {
        let root = unsafe {
            self.automation
                .ElementFromHandleBuildCache(window.hwnd, &self.cache_request)
        }
        .ok();
        let Some(root) = root else {
            return Ok(None);
        };

        let mut best_path = Vec::new();
        let mut current = root;
        for _ in 0..UIA_MAX_ANCESTOR_STEPS {
            let children = unsafe {
                current.FindAllBuildCache(
                    TreeScope_Children,
                    &self.raw_condition,
                    &self.cache_request,
                )
            }?;
            let child_count = unsafe { children.Length()? };

            let mut next_child = None;
            for index in (0..child_count).rev() {
                let child = unsafe { children.GetElement(index)? };
                let Some(rect) = cached_visible_rect(&child, point, window.bounds)? else {
                    continue;
                };
                next_child = Some((child, rect));
                break;
            }

            let Some((child, rect)) = next_child else {
                break;
            };
            best_path.push(rect);
            current = child;

            if best_path.len() >= UIA_MAX_HIT_PATH_RECTS {
                break;
            }
        }

        best_path.reverse();
        push_rect_if_useful(&mut best_path, window.bounds, point);
        Ok(Some(best_path))
    }

    fn hit_path_from_element(
        &self,
        element: IUIAutomationElement,
        point: POINT,
        window: &UiaWindow,
        root: &IUIAutomationElement,
    ) -> Result<Vec<RECT>> {
        let mut rects = Vec::with_capacity(8);
        let mut current = element;

        for _ in 0..UIA_MAX_ANCESTOR_STEPS {
            push_uia_candidate_rect(&mut rects, &current, point, window.bounds)?;
            if self.elements_are_same(&current, root) {
                break;
            }

            let parent = unsafe {
                self.walker
                    .GetParentElementBuildCache(&current, &self.cache_request)
            }
            .ok();
            let Some(parent) = parent else {
                break;
            };
            if self.elements_are_same(&current, &parent) {
                break;
            }
            current = parent;

            if rects.len() >= UIA_MAX_HIT_PATH_RECTS {
                break;
            }
        }

        push_rect_if_useful(&mut rects, window.bounds, point);
        Ok(rects)
    }

    fn element_is_inside_window(
        &self,
        element: &IUIAutomationElement,
        root: &IUIAutomationElement,
        window: &UiaWindow,
    ) -> Result<bool> {
        if matches_cached_hwnd(element, window.hwnd) {
            return Ok(true);
        }
        if self.elements_are_same(element, root) {
            return Ok(true);
        }

        let mut current = element.clone();
        for _ in 0..UIA_MAX_ANCESTOR_STEPS {
            if matches_cached_hwnd(&current, window.hwnd) {
                return Ok(true);
            }
            if self.elements_are_same(&current, root) {
                return Ok(true);
            }

            let parent = unsafe {
                self.walker
                    .GetParentElementBuildCache(&current, &self.cache_request)
            }
            .ok();
            let Some(parent) = parent else {
                break;
            };
            if self.elements_are_same(&current, &parent) {
                break;
            }
            current = parent;
        }

        Ok(false)
    }

    fn root_from_window(&self, hwnd: HWND) -> Option<IUIAutomationElement> {
        unsafe {
            self.automation
                .ElementFromHandleBuildCache(hwnd, &self.cache_request)
        }
        .ok()
    }

    fn elements_are_same(&self, left: &IUIAutomationElement, right: &IUIAutomationElement) -> bool {
        unsafe { self.automation.CompareElements(left, right) }
            .map(|same| same.as_bool())
            .unwrap_or(false)
    }
}

fn create_element_cache_request(automation: &IUIAutomation) -> Result<IUIAutomationCacheRequest> {
    let cache_request = unsafe { automation.CreateCacheRequest()? };
    unsafe {
        cache_request.SetTreeScope(TreeScope_Element)?;
        cache_request.AddProperty(UIA_BoundingRectanglePropertyId)?;
        cache_request.AddProperty(UIA_IsOffscreenPropertyId)?;
        cache_request.AddProperty(UIA_NativeWindowHandlePropertyId)?;
    }
    Ok(cache_request)
}

fn build_uia_window_cache(excluded_hwnds: &[HWND]) -> Result<(Vec<UiaWindow>, WindowSpatialIndex)> {
    let hwnds = window::enumerate_top_windows()?;
    let monitors = MonitorCache::new();
    let mut excluded_raw = excluded_hwnds
        .iter()
        .map(|hwnd| hwnd.0 as isize)
        .collect::<Vec<_>>();
    excluded_raw.sort_unstable();
    excluded_raw.dedup();

    let mut windows = Vec::with_capacity(hwnds.len());
    let mut entries = Vec::with_capacity(hwnds.len());

    for (z_order, hwnd) in hwnds.into_iter().enumerate() {
        if excluded_raw.binary_search(&(hwnd.0 as isize)).is_ok() {
            continue;
        }
        if window::is_window_cloaked(hwnd) {
            continue;
        }
        let Some(rect) = window::visible_window_rect(hwnd) else {
            continue;
        };
        let Some(bounds) = monitors.clip_rect_to_visible_area(rect) else {
            continue;
        };

        let cache_index = windows.len();
        windows.push(UiaWindow { hwnd, bounds });
        entries.push(IndexedWindow {
            envelope: rect_to_aabb(bounds),
            cache_index,
            z_order,
        });
    }

    Ok((windows, WindowSpatialIndex::build(entries)))
}

fn matches_cached_hwnd(element: &IUIAutomationElement, hwnd: HWND) -> bool {
    let Ok(element_hwnd) = (unsafe { element.CachedNativeWindowHandle() }) else {
        return false;
    };
    if element_hwnd.is_invalid() {
        return false;
    }
    if element_hwnd == hwnd {
        return true;
    }

    let root = unsafe { GetAncestor(element_hwnd, GA_ROOT) };
    root == hwnd
}

fn push_uia_candidate_rect(
    rects: &mut Vec<RECT>,
    element: &IUIAutomationElement,
    point: POINT,
    window_bounds: RECT,
) -> Result<()> {
    let Some(rect) = cached_visible_rect(element, point, window_bounds)? else {
        return Ok(());
    };
    push_rect_if_useful(rects, rect, point);
    Ok(())
}

fn cached_visible_rect(
    element: &IUIAutomationElement,
    point: POINT,
    window_bounds: RECT,
) -> Result<Option<RECT>> {
    if unsafe { element.CachedIsOffscreen()?.as_bool() } {
        return Ok(None);
    }

    let rect = unsafe { element.CachedBoundingRectangle()? };
    let Some(rect) = intersect_rect(rect, window_bounds) else {
        return Ok(None);
    };
    Ok((!is_empty(rect) && contains_point(rect, point)).then_some(rect))
}

fn push_rect_if_useful(rects: &mut Vec<RECT>, rect: RECT, point: POINT) {
    if rects.len() >= UIA_MAX_HIT_PATH_RECTS || is_empty(rect) || !contains_point(rect, point) {
        return;
    }
    if rects.iter().any(|existing| same_rect(*existing, rect)) {
        return;
    }
    rects.push(rect);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rect(left: i32, top: i32, right: i32, bottom: i32) -> RECT {
        RECT {
            left,
            top,
            right,
            bottom,
        }
    }

    #[test]
    fn push_rect_if_useful_rejects_empty_outside_and_duplicate_rects() {
        let point = POINT { x: 5, y: 5 };
        let mut rects = Vec::new();

        push_rect_if_useful(&mut rects, rect(0, 0, 0, 10), point);
        push_rect_if_useful(&mut rects, rect(20, 20, 30, 30), point);
        push_rect_if_useful(&mut rects, rect(0, 0, 10, 10), point);
        push_rect_if_useful(&mut rects, rect(0, 0, 10, 10), point);

        assert_eq!(rects.len(), 1);
        assert!(same_rect(rects[0], rect(0, 0, 10, 10)));
    }
}
