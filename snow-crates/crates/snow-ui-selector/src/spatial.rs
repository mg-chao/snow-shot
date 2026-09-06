use rstar::{AABB, Envelope, RTree, RTreeObject};

/// Window counts below this threshold use a linear scan instead of an R-tree.
/// Typical desktops have 10-30 visible windows; the R-tree overhead only pays
/// off beyond this point.
pub(crate) const SMALL_WINDOW_LINEAR_SCAN_THRESHOLD: usize = 16;

#[derive(Clone, Copy, Debug)]
pub(crate) struct IndexedWindow {
    pub(crate) envelope: AABB<[i32; 2]>,
    pub(crate) cache_index: usize,
    /// Lower values are closer to the front of the desktop Z order.
    pub(crate) z_order: usize,
}

impl RTreeObject for IndexedWindow {
    type Envelope = AABB<[i32; 2]>;

    fn envelope(&self) -> Self::Envelope {
        self.envelope
    }
}

/// Spatial index for the top-level window set.  For small window counts (the
/// common case on most desktops) a simple linear scan beats the R-tree due to
/// lower constant overhead and better cache locality.
#[derive(Clone, Debug)]
pub(crate) enum WindowSpatialIndex {
    Small(Vec<IndexedWindow>),
    Tree(RTree<IndexedWindow>),
}

impl WindowSpatialIndex {
    pub(crate) fn build(entries: Vec<IndexedWindow>) -> Self {
        if entries.len() <= SMALL_WINDOW_LINEAR_SCAN_THRESHOLD {
            Self::Small(entries)
        } else {
            Self::Tree(RTree::bulk_load(entries))
        }
    }

    /// Drop the indexed window snapshot and its backing allocation.
    pub(crate) fn release_cache(&mut self) {
        *self = Self::Small(Vec::new());
    }

    /// Find the topmost (lowest z-order) window whose envelope contains `point`.
    pub(crate) fn window_at_point(&self, point: [i32; 2]) -> Option<&IndexedWindow> {
        match self {
            Self::Small(windows) => {
                let mut best: Option<&IndexedWindow> = None;
                for w in windows {
                    if w.envelope.contains_point(&point) {
                        match best {
                            Some(b) if w.z_order < b.z_order => best = Some(w),
                            None => best = Some(w),
                            _ => {}
                        }
                    }
                }
                best
            }
            Self::Tree(tree) => {
                let point_box = AABB::from_point(point);
                tree.locate_in_envelope_intersecting(&point_box)
                    .filter(|w| w.envelope.contains_point(&point))
                    .min_by_key(|w| w.z_order)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn release_cache_clears_window_index() {
        let mut index = WindowSpatialIndex::build(vec![IndexedWindow {
            envelope: AABB::from_corners([0, 0], [10, 10]),
            cache_index: 0,
            z_order: 0,
        }]);

        assert!(index.window_at_point([5, 5]).is_some());
        index.release_cache();
        assert!(index.window_at_point([5, 5]).is_none());
    }
}
