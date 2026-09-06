#[path = "elbow_router_search.rs"]
mod search;

pub(crate) use search::{
    ensure_orthogonal, offset_from_heading, point_bounds, remove_short_segments,
    route_between_points,
};

use crate::arrow_geom::Heading;
use crate::{Bounds, Point};

pub(crate) const BASE_BINDING_GAP_ELBOW: f64 = 5.0;
pub(crate) const DEDUP_THRESHOLD: f64 = 1.0;
pub const BASE_PADDING: f64 = 40.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct EndpointRoute {
    pub(crate) point: Point,
    pub(crate) heading: Heading,
    pub(crate) element_bounds: Bounds,
    pub(crate) overlap_bounds: Bounds,
    pub(crate) bindable_bounds: Option<Bounds>,
    pub(crate) has_arrowhead: bool,
}
