use std::time::{Duration, Instant};

use rstar::{AABB, RTree, RTreeObject};
use windows::Win32::Foundation::{HWND, POINT, RECT};
use windows::core::Result;

use crate::geometry::{contains_point, intersect_rect, rect_to_aabb, same_rect};

pub(super) const QUERY_BUDGET: Duration = Duration::from_millis(168);
const MAX_STEPS: usize = 80;
const MAX_RECTS: usize = 100;
const LINEAR_CHILD_LIMIT: usize = 16;

pub(super) trait Clock {
    fn now(&self) -> Duration;
}

pub(super) struct QueryClock(Instant);

impl QueryClock {
    pub(super) fn new() -> Self {
        Self(Instant::now())
    }
}

impl Clock for QueryClock {
    fn now(&self) -> Duration {
        self.0.elapsed()
    }
}

pub(super) struct Candidate<E> {
    pub(super) element: E,
    pub(super) bounds: RECT,
    pub(super) offscreen: bool,
}

pub(super) trait Batch {
    type Element;
    fn len(&self) -> usize;
    fn get(&self, index: usize) -> Result<Candidate<Self::Element>>;
}

pub(super) trait Provider {
    type Element;
    type Batch: Batch<Element = Self::Element>;
    fn root(&mut self, hwnd: HWND, remaining: Duration) -> Result<Self::Batch>;
    fn children(&mut self, element: &Self::Element, remaining: Duration) -> Result<Self::Batch>;
}

struct Node<E, B> {
    bounds: RECT,
    parent: Option<usize>,
    children: Children<E, B>,
}

enum Children<E, B> {
    Root(HWND),
    Unloaded(E),
    Pending {
        batch: B,
        next: usize,
        entries: Vec<ChildEntry>,
    },
    Loaded(ChildIndex),
    Failed,
}

#[derive(Clone, Copy)]
struct ChildEntry {
    bounds: RECT,
    node: usize,
}

impl RTreeObject for ChildEntry {
    type Envelope = AABB<[i32; 2]>;
    fn envelope(&self) -> Self::Envelope {
        rect_to_aabb(self.bounds)
    }
}

enum ChildIndex {
    Linear(Vec<ChildEntry>),
    Tree(RTree<ChildEntry>),
}

impl ChildIndex {
    fn new(entries: Vec<ChildEntry>) -> Self {
        if entries.len() <= LINEAR_CHILD_LIMIT {
            Self::Linear(entries)
        } else {
            Self::Tree(RTree::bulk_load(entries))
        }
    }

    fn hit(&self, point: POINT) -> Option<usize> {
        match self {
            Self::Linear(entries) => entries
                .iter()
                .rev()
                .find(|entry| contains_point(entry.bounds, point))
                .map(|entry| entry.node),
            Self::Tree(tree) => tree
                .locate_in_envelope_intersecting(&AABB::from_point([point.x, point.y]))
                .filter(|entry| contains_point(entry.bounds, point))
                .map(|entry| entry.node)
                .max(),
        }
    }
}

pub(super) struct WindowTree<P: Provider> {
    nodes: Vec<Node<P::Element, P::Batch>>,
}

impl<P: Provider> WindowTree<P> {
    pub(super) fn new(hwnd: HWND, bounds: RECT) -> Self {
        Self {
            nodes: vec![Node {
                bounds,
                parent: None,
                children: Children::Root(hwnd),
            }],
        }
    }

    pub(super) fn hit(&mut self, provider: &mut P, point: POINT, clock: &impl Clock) -> Vec<RECT> {
        let bounds = self.nodes[0].bounds;
        if !contains_point(bounds, point) {
            return Vec::new();
        }
        let deadline = clock.now() + QUERY_BUDGET;
        let mut current = 0;
        for _ in 0..MAX_STEPS {
            if !self.expand(current, provider, deadline, clock) {
                break;
            }
            let Children::Loaded(children) = &self.nodes[current].children else {
                break;
            };
            let Some(child) = children.hit(point) else {
                break;
            };
            current = child;
        }

        let mut path = Vec::with_capacity(8);
        while current != 0 && path.len() < MAX_RECTS - 1 {
            let node = &self.nodes[current];
            if !same_rect(node.bounds, bounds) && !path.contains(&node.bounds) {
                path.push(node.bounds);
            }
            current = node.parent.expect("non-root node has a parent");
        }
        path.push(bounds);
        path
    }

    fn expand(
        &mut self,
        node: usize,
        provider: &mut P,
        deadline: Duration,
        clock: &impl Clock,
    ) -> bool {
        if matches!(self.nodes[node].children, Children::Loaded(_)) {
            return true;
        }
        if matches!(self.nodes[node].children, Children::Failed) {
            return false;
        }
        let remaining = deadline.saturating_sub(clock.now());
        if remaining.is_zero() {
            return false;
        }
        let state = std::mem::replace(&mut self.nodes[node].children, Children::Failed);
        let (batch, mut next, mut entries) = match state {
            Children::Root(hwnd) => match provider.root(hwnd, remaining) {
                Ok(batch) => (batch, 0, Vec::new()),
                Err(_) => return false,
            },
            Children::Unloaded(element) => match provider.children(&element, remaining) {
                Ok(batch) => (batch, 0, Vec::new()),
                Err(_) => return false,
            },
            Children::Pending {
                batch,
                next,
                entries,
            } => (batch, next, entries),
            _ => unreachable!("loaded and failed nodes returned above"),
        };

        // Publish only complete sibling batches: a later overlapping child takes precedence.
        // Retaining the cached batch also lets decoding resume without another provider call.
        while next < batch.len() {
            if clock.now() >= deadline {
                self.nodes[node].children = Children::Pending {
                    batch,
                    next,
                    entries,
                };
                return false;
            }
            let candidate = batch.get(next);
            next += 1;
            let Ok(candidate) = candidate else { continue };
            if candidate.offscreen {
                continue;
            }
            let Some(bounds) = intersect_rect(candidate.bounds, self.nodes[0].bounds) else {
                continue;
            };
            let index = self.nodes.len();
            self.nodes.push(Node {
                bounds,
                parent: Some(node),
                children: Children::Unloaded(candidate.element),
            });
            entries.push(ChildEntry {
                bounds,
                node: index,
            });
        }
        self.nodes[node].children = Children::Loaded(ChildIndex::new(entries));
        // Even after expiry, use the completed batch before considering more acquisition.
        true
    }
}

#[cfg(test)]
#[path = "cache_tests.rs"]
mod tests;
