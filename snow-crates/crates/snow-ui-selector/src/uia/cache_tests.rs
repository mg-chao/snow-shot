use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::rc::Rc;

use windows::Win32::Foundation::E_FAIL;
use windows::core::Error;

use super::*;

#[derive(Clone, Default)]
struct FakeClock(Rc<Cell<Duration>>);

impl Clock for FakeClock {
    fn now(&self) -> Duration {
        self.0.get()
    }
}

impl FakeClock {
    fn advance(&self, amount: Duration) {
        self.0.set(self.now() + amount);
    }
}

struct Element {
    id: usize,
    live: Rc<Cell<usize>>,
}

impl Element {
    fn new(id: usize, live: Rc<Cell<usize>>) -> Self {
        live.set(live.get() + 1);
        Self { id, live }
    }
}

impl Drop for Element {
    fn drop(&mut self) {
        self.live.set(self.live.get() - 1);
    }
}

#[derive(Clone)]
struct Spec {
    id: usize,
    bounds: RECT,
    offscreen: bool,
    error: bool,
}

impl Spec {
    fn new(id: usize, bounds: RECT) -> Self {
        Self {
            id,
            bounds,
            offscreen: false,
            error: false,
        }
    }
}

struct FakeBatch {
    specs: Vec<Spec>,
    live: Rc<Cell<usize>>,
    clock: FakeClock,
    decode_cost: Duration,
    reads: Rc<RefCell<Vec<usize>>>,
}

impl Batch for FakeBatch {
    type Element = Element;
    fn len(&self) -> usize {
        self.specs.len()
    }
    fn get(&self, index: usize) -> Result<Candidate<Element>> {
        self.clock.advance(self.decode_cost);
        let spec = &self.specs[index];
        self.reads.borrow_mut().push(spec.id);
        if spec.error {
            return Err(Error::from_hresult(E_FAIL));
        }
        Ok(Candidate {
            element: Element::new(spec.id, self.live.clone()),
            bounds: spec.bounds,
            offscreen: spec.offscreen,
        })
    }
}

#[derive(Default)]
struct FakeProvider {
    children: HashMap<usize, Vec<Spec>>,
    calls: Vec<usize>,
    remaining: Vec<Duration>,
    clock: FakeClock,
    call_cost: Duration,
    decode_cost: Duration,
    failure: Option<usize>,
    live: Rc<Cell<usize>>,
    reads: Rc<RefCell<Vec<usize>>>,
}

impl FakeProvider {
    fn load(&mut self, id: usize, remaining: Duration) -> Result<FakeBatch> {
        self.calls.push(id);
        self.remaining.push(remaining);
        self.clock.advance(self.call_cost);
        if self.failure == Some(id) {
            return Err(Error::from_hresult(E_FAIL));
        }
        Ok(FakeBatch {
            specs: self.children.get(&id).cloned().unwrap_or_default(),
            live: self.live.clone(),
            clock: self.clock.clone(),
            decode_cost: self.decode_cost,
            reads: self.reads.clone(),
        })
    }
}

impl Provider for FakeProvider {
    type Element = Element;
    type Batch = FakeBatch;
    fn root(&mut self, _: HWND, remaining: Duration) -> Result<FakeBatch> {
        self.load(0, remaining)
    }
    fn children(&mut self, element: &Element, remaining: Duration) -> Result<FakeBatch> {
        self.load(element.id, remaining)
    }
}

fn rect(left: i32, top: i32, right: i32, bottom: i32) -> RECT {
    RECT {
        left,
        top,
        right,
        bottom,
    }
}

fn tree(bounds: RECT) -> WindowTree<FakeProvider> {
    WindowTree::new(HWND::default(), bounds)
}

fn hit(
    tree: &mut WindowTree<FakeProvider>,
    provider: &mut FakeProvider,
    x: i32,
    y: i32,
) -> Vec<RECT> {
    let clock = provider.clock.clone();
    tree.hit(provider, POINT { x, y }, &clock)
}

#[test]
fn window_path_is_deepest_first_with_window_last() {
    let bounds = rect(-100, -100, 100, 100);
    let container = rect(-50, -50, 50, 50);
    let leaf = rect(-10, -10, 10, 10);
    let mut provider = FakeProvider::default();
    provider.children.insert(0, vec![Spec::new(1, container)]);
    provider.children.insert(1, vec![Spec::new(2, leaf)]);
    assert_eq!(
        hit(&mut tree(bounds), &mut provider, 0, 0),
        [leaf, container, bounds]
    );
}

#[test]
fn empty_provider_returns_window_and_is_not_requeried() {
    let bounds = rect(0, 0, 100, 100);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider::default();
    for _ in 0..3 {
        assert_eq!(hit(&mut tree, &mut provider, 5, 5), [bounds]);
    }
    assert_eq!(provider.calls, [0]);
}

#[test]
fn provider_cycles_are_bounded_without_discarding_equal_bounds_nodes() {
    let bounds = rect(0, 0, 100, 100);
    let inner = rect(0, 0, 10, 10);
    let mut provider = FakeProvider::default();
    provider.children.insert(0, vec![Spec::new(1, inner)]);
    provider.children.insert(1, vec![Spec::new(1, inner)]);
    assert_eq!(hit(&mut tree(bounds), &mut provider, 5, 5), [inner, bounds]);
    assert_eq!(provider.calls.len(), MAX_STEPS);
}

#[test]
fn repeat_queries_are_local_and_siblings_expand_only_when_visited() {
    let bounds = rect(0, 0, 100, 100);
    let left = rect(0, 0, 50, 100);
    let right = rect(50, 0, 100, 100);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider::default();
    provider
        .children
        .insert(0, vec![Spec::new(1, left), Spec::new(2, right)]);
    for _ in 0..10 {
        assert_eq!(hit(&mut tree, &mut provider, 5, 5), [left, bounds]);
    }
    assert_eq!(provider.calls, [0, 1]);
    assert_eq!(
        provider.live.get(),
        1,
        "only the unexpanded sibling keeps a reference"
    );
    assert_eq!(hit(&mut tree, &mut provider, 75, 5), [right, bounds]);
    assert_eq!(provider.calls, [0, 1, 2]);
    assert_eq!(provider.live.get(), 0);
    for _ in 0..10 {
        hit(&mut tree, &mut provider, 75, 5);
    }
    assert_eq!(provider.calls, [0, 1, 2]);
}

#[test]
fn last_containing_sibling_wins_in_linear_and_indexed_batches() {
    for count in [2, 16, 17, 100] {
        let bounds = rect(0, 0, 100, 100);
        let mut provider = FakeProvider::default();
        provider.children.insert(
            0,
            (1..=count)
                .map(|id| Spec::new(id, rect(0, 0, 50, 50)))
                .collect(),
        );
        provider
            .children
            .insert(count, vec![Spec::new(count + 1, rect(1, 1, 10, 10))]);
        assert_eq!(
            hit(&mut tree(bounds), &mut provider, 5, 5),
            [rect(1, 1, 10, 10), rect(0, 0, 50, 50), bounds]
        );
        assert_eq!(provider.calls, [0, count, count + 1]);
    }
}

#[test]
fn invalid_offscreen_and_unreadable_candidates_are_skipped() {
    let bounds = rect(0, 0, 100, 100);
    let visible = Spec::new(1, rect(0, 0, 20, 20));
    let mut offscreen = Spec::new(2, rect(0, 0, 20, 20));
    offscreen.offscreen = true;
    let mut unreadable = Spec::new(3, rect(0, 0, 20, 20));
    unreadable.error = true;
    let mut provider = FakeProvider::default();
    provider.children.insert(
        0,
        vec![
            visible,
            offscreen,
            unreadable,
            Spec::new(4, rect(10, 0, 10, 20)),
            Spec::new(5, rect(200, 200, 300, 300)),
        ],
    );
    assert_eq!(
        hit(&mut tree(bounds), &mut provider, 5, 5),
        [rect(0, 0, 20, 20), bounds]
    );
    assert_eq!(provider.calls, [0, 1]);
    assert_eq!(provider.live.get(), 0);
}

#[test]
fn bounds_are_clipped_and_point_edges_are_half_open() {
    for count in [1, 20] {
        let bounds = rect(-100, -100, 100, 100);
        let mut tree = tree(bounds);
        let mut provider = FakeProvider::default();
        provider.children.insert(
            0,
            (1..=count)
                .map(|id| Spec::new(id, rect(-200, -200, 10, 10)))
                .collect(),
        );
        assert_eq!(
            hit(&mut tree, &mut provider, -100, -100),
            [rect(-100, -100, 10, 10), bounds]
        );
        assert_eq!(hit(&mut tree, &mut provider, 10, 5), [bounds]);
        assert_eq!(hit(&mut tree, &mut provider, 5, 10), [bounds]);
        assert!(hit(&mut tree, &mut provider, 100, 5).is_empty());
    }
}

#[test]
fn provider_failure_keeps_best_path_and_is_cached_until_refresh() {
    let bounds = rect(0, 0, 100, 100);
    let child = rect(0, 0, 20, 20);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider {
        failure: Some(1),
        ..Default::default()
    };
    provider.children.insert(0, vec![Spec::new(1, child)]);
    for _ in 0..3 {
        assert_eq!(hit(&mut tree, &mut provider, 5, 5), [child, bounds]);
    }
    assert_eq!(provider.calls, [0, 1]);
    assert_eq!(provider.live.get(), 0);
    tree = WindowTree::new(HWND::default(), bounds);
    provider.failure = None;
    hit(&mut tree, &mut provider, 5, 5);
    assert_eq!(provider.calls, [0, 1, 0, 1]);
}

#[test]
fn failed_root_returns_window_without_retries() {
    let bounds = rect(0, 0, 100, 100);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider {
        failure: Some(0),
        ..Default::default()
    };
    for _ in 0..3 {
        assert_eq!(hit(&mut tree, &mut provider, 5, 5), [bounds]);
    }
    assert_eq!(provider.calls, [0]);
}

#[test]
fn budget_exhaustion_returns_best_path_and_later_query_continues() {
    let bounds = rect(0, 0, 100, 100);
    let child = rect(0, 0, 80, 80);
    let leaf = rect(0, 0, 10, 10);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider {
        call_cost: Duration::from_millis(100),
        ..Default::default()
    };
    provider.children.insert(0, vec![Spec::new(1, child)]);
    provider.children.insert(1, vec![Spec::new(2, leaf)]);
    assert_eq!(hit(&mut tree, &mut provider, 5, 5), [child, bounds]);
    assert_eq!(
        provider.remaining,
        [QUERY_BUDGET, Duration::from_millis(68)]
    );
    assert_eq!(hit(&mut tree, &mut provider, 5, 5), [leaf, child, bounds]);
    assert_eq!(provider.calls, [0, 1, 2]);
}

#[test]
fn completed_batch_after_budget_still_returns_deepest_cached_rectangle() {
    let bounds = rect(0, 0, 100, 100);
    let first = rect(0, 0, 20, 20);
    let last = rect(0, 0, 30, 30);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider {
        decode_cost: Duration::from_millis(100),
        ..Default::default()
    };
    provider
        .children
        .insert(0, vec![Spec::new(1, first), Spec::new(2, last)]);
    assert_eq!(hit(&mut tree, &mut provider, 5, 5), [last, bounds]);
    assert_eq!(
        provider.calls,
        [0],
        "no acquisition after the budget expires"
    );
    assert_eq!(*provider.reads.borrow(), [1, 2]);
    assert_eq!(hit(&mut tree, &mut provider, 5, 5), [last, bounds]);
    assert_eq!(provider.calls, [0, 2]);
}

#[test]
fn partial_batches_resume_without_refetching_or_publishing_incomplete_overlap_order() {
    let bounds = rect(0, 0, 100, 100);
    let first = rect(0, 0, 20, 20);
    let last = rect(0, 0, 30, 30);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider {
        decode_cost: Duration::from_millis(100),
        ..Default::default()
    };
    provider.children.insert(
        0,
        vec![Spec::new(1, first), Spec::new(2, first), Spec::new(3, last)],
    );
    assert_eq!(hit(&mut tree, &mut provider, 5, 5), [bounds]);
    assert_eq!(provider.calls, [0]);
    assert_eq!(*provider.reads.borrow(), [1, 2]);
    assert_eq!(hit(&mut tree, &mut provider, 5, 5), [last, bounds]);
    assert_eq!(provider.calls, [0, 3]);
    assert_eq!(*provider.reads.borrow(), [1, 2, 3]);
}

#[test]
fn cached_geometry_is_frozen_when_a_child_is_later_expanded() {
    let bounds = rect(0, 0, 100, 100);
    let left = rect(0, 0, 50, 100);
    let right = rect(50, 0, 100, 100);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider::default();
    provider
        .children
        .insert(0, vec![Spec::new(1, left), Spec::new(2, right)]);
    hit(&mut tree, &mut provider, 5, 5);
    provider.children.insert(0, vec![Spec::new(1, bounds)]);
    assert_eq!(hit(&mut tree, &mut provider, 75, 5), [right, bounds]);
}

#[test]
fn window_rectangle_is_always_last_even_for_equal_bounds_ancestors() {
    let bounds = rect(0, 0, 100, 100);
    let inner = rect(0, 0, 20, 20);
    let mut provider = FakeProvider::default();
    provider.children.insert(0, vec![Spec::new(1, bounds)]);
    provider.children.insert(1, vec![Spec::new(2, inner)]);
    provider.children.insert(2, vec![Spec::new(3, bounds)]);
    assert_eq!(hit(&mut tree(bounds), &mut provider, 5, 5), [inner, bounds]);
}

#[test]
fn releasing_tree_drops_unexpanded_and_partial_batch_references() {
    let bounds = rect(0, 0, 100, 100);
    let mut tree = tree(bounds);
    let mut provider = FakeProvider {
        decode_cost: Duration::from_millis(100),
        ..Default::default()
    };
    provider
        .children
        .insert(0, (1..=4).map(|id| Spec::new(id, bounds)).collect());
    hit(&mut tree, &mut provider, 5, 5);
    assert_eq!(provider.live.get(), 2);
    drop(tree);
    assert_eq!(provider.live.get(), 0);
}

#[test]
fn outside_query_never_contacts_provider() {
    let mut provider = FakeProvider::default();
    assert!(hit(&mut tree(rect(0, 0, 10, 10)), &mut provider, 20, 20).is_empty());
    assert!(provider.calls.is_empty());
}
