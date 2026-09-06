use std::collections::BTreeMap;

use crate::arrow_geom::{Heading, dedupe_collinear_points, distance, manhattan, reverse_heading};
use crate::{Bounds, Point};

use super::{BASE_BINDING_GAP_ELBOW, BASE_PADDING, DEDUP_THRESHOLD, EndpointRoute};

#[derive(Clone, Debug, PartialEq)]
struct GridNode {
    x: f64,
    y: f64,
    col: usize,
    row: usize,
}

#[derive(Clone, Debug, PartialEq)]
struct QueueNode {
    key: String,
    node: GridNode,
    g: f64,
    f: f64,
    closed: bool,
    visited: bool,
    parent_key: Option<String>,
}

#[derive(Clone, Debug, PartialEq)]
struct Grid {
    nodes: Vec<GridNode>,
    by_coord: BTreeMap<String, GridNode>,
    cols: usize,
    rows: usize,
}

pub(crate) fn point_bounds(point: Point) -> Bounds {
    [
        point[0] - 2.0,
        point[1] - 2.0,
        point[0] + 2.0,
        point[1] + 2.0,
    ]
}

fn point_in_bounds(point: Point, bounds: Bounds) -> bool {
    point[0] > bounds[0] && point[0] < bounds[2] && point[1] > bounds[1] && point[1] < bounds[3]
}

fn approximate_push(values: &mut Vec<f64>, value: f64) {
    if values
        .iter()
        .any(|existing| (existing - value).abs() <= 1e-6)
    {
        return;
    }
    values.push(value);
}

fn common_aabb(bounds: &[Bounds]) -> Bounds {
    let mut min_x = bounds[0][0];
    let mut min_y = bounds[0][1];
    let mut max_x = bounds[0][2];
    let mut max_y = bounds[0][3];
    for bounds in bounds.iter().skip(1) {
        min_x = min_x.min(bounds[0]);
        min_y = min_y.min(bounds[1]);
        max_x = max_x.max(bounds[2]);
        max_y = max_y.max(bounds[3]);
    }
    [min_x, min_y, max_x, max_y]
}

pub(crate) fn offset_from_heading(heading: Heading, head: f64, side: f64) -> [f64; 4] {
    match heading {
        Heading::Up => [head, side, side, side],
        Heading::Right => [side, head, side, side],
        Heading::Down => [side, side, head, side],
        Heading::Left => [side, side, side, head],
    }
}

fn get_dongle_position(bounds: Bounds, heading: Heading, point: Point) -> Point {
    match heading {
        Heading::Up => [point[0], bounds[1]],
        Heading::Right => [bounds[2], point[1]],
        Heading::Down => [point[0], bounds[3]],
        Heading::Left => [bounds[0], point[1]],
    }
}

struct DynamicAabbInput {
    start_bounds: Bounds,
    end_bounds: Bounds,
    common: Bounds,
    start_difference: Option<[f64; 4]>,
    end_difference: Option<[f64; 4]>,
    disable_side_hack: bool,
    start_bindable_bounds: Option<Bounds>,
    end_bindable_bounds: Option<Bounds>,
}

fn generate_dynamic_aabbs(input: DynamicAabbInput) -> [Bounds; 2] {
    let DynamicAabbInput {
        start_bounds,
        end_bounds,
        common,
        start_difference,
        end_difference,
        disable_side_hack,
        start_bindable_bounds,
        end_bindable_bounds,
    } = input;
    let start_bindable = start_bindable_bounds.unwrap_or(start_bounds);
    let end_bindable = end_bindable_bounds.unwrap_or(end_bounds);
    let [start_up, start_right, start_down, start_left] = start_difference.unwrap_or([0.0; 4]);
    let [end_up, end_right, end_down, end_left] = end_difference.unwrap_or([0.0; 4]);

    let first = [
        if start_bounds[0] > end_bounds[2] {
            if start_bounds[1] > end_bounds[3] || start_bounds[3] < end_bounds[1] {
                ((start_bindable[0] + end_bindable[2]) / 2.0).min(start_bounds[0] - start_left)
            } else {
                (start_bindable[0] + end_bindable[2]) / 2.0
            }
        } else if start_bounds[0] > end_bounds[0] {
            start_bounds[0] - start_left
        } else {
            common[0] - start_left
        },
        if start_bounds[1] > end_bounds[3] {
            if start_bounds[0] > end_bounds[2] || start_bounds[2] < end_bounds[0] {
                ((start_bindable[1] + end_bindable[3]) / 2.0).min(start_bounds[1] - start_up)
            } else {
                (start_bindable[1] + end_bindable[3]) / 2.0
            }
        } else if start_bounds[1] > end_bounds[1] {
            start_bounds[1] - start_up
        } else {
            common[1] - start_up
        },
        if start_bounds[2] < end_bounds[0] {
            if start_bounds[1] > end_bounds[3] || start_bounds[3] < end_bounds[1] {
                ((start_bindable[2] + end_bindable[0]) / 2.0).max(start_bounds[2] + start_right)
            } else {
                (start_bindable[2] + end_bindable[0]) / 2.0
            }
        } else if start_bounds[2] < end_bounds[2] {
            start_bounds[2] + start_right
        } else {
            common[2] + start_right
        },
        if start_bounds[3] < end_bounds[1] {
            if start_bounds[0] > end_bounds[2] || start_bounds[2] < end_bounds[0] {
                ((start_bindable[3] + end_bindable[1]) / 2.0).max(start_bounds[3] + start_down)
            } else {
                (start_bindable[3] + end_bindable[1]) / 2.0
            }
        } else if start_bounds[3] < end_bounds[3] {
            start_bounds[3] + start_down
        } else {
            common[3] + start_down
        },
    ];

    let second = [
        if end_bounds[0] > start_bounds[2] {
            if end_bounds[1] > start_bounds[3] || end_bounds[3] < start_bounds[1] {
                ((end_bindable[0] + start_bindable[2]) / 2.0).min(end_bounds[0] - end_left)
            } else {
                (end_bindable[0] + start_bindable[2]) / 2.0
            }
        } else if end_bounds[0] > start_bounds[0] {
            end_bounds[0] - end_left
        } else {
            common[0] - end_left
        },
        if end_bounds[1] > start_bounds[3] {
            if end_bounds[0] > start_bounds[2] || end_bounds[2] < start_bounds[0] {
                ((end_bindable[1] + start_bindable[3]) / 2.0).min(end_bounds[1] - end_up)
            } else {
                (end_bindable[1] + start_bindable[3]) / 2.0
            }
        } else if end_bounds[1] > start_bounds[1] {
            end_bounds[1] - end_up
        } else {
            common[1] - end_up
        },
        if end_bounds[2] < start_bounds[0] {
            if end_bounds[1] > start_bounds[3] || end_bounds[3] < start_bounds[1] {
                ((end_bindable[2] + start_bindable[0]) / 2.0).max(end_bounds[2] + end_right)
            } else {
                (end_bindable[2] + start_bindable[0]) / 2.0
            }
        } else if end_bounds[2] < start_bounds[2] {
            end_bounds[2] + end_right
        } else {
            common[2] + end_right
        },
        if end_bounds[3] < start_bounds[1] {
            if end_bounds[0] > start_bounds[2] || end_bounds[2] < start_bounds[0] {
                ((end_bindable[3] + start_bindable[1]) / 2.0).max(end_bounds[3] + end_down)
            } else {
                (end_bindable[3] + start_bindable[1]) / 2.0
            }
        } else if end_bounds[3] < start_bounds[3] {
            end_bounds[3] + end_down
        } else {
            common[3] + end_down
        },
    ];

    let combined = common_aabb(&[first, second]);
    if !disable_side_hack
        && (first[2] - first[0] + second[2] - second[0]) > (combined[2] - combined[0] + 1e-11)
        && (first[3] - first[1] + second[3] - second[1]) > (combined[3] - combined[1] + 1e-11)
    {
        let end_center = [(second[0] + second[2]) / 2.0, (second[1] + second[3]) / 2.0];
        let cross = |a: Point, b: Point| a[0] * b[1] - a[1] * b[0];

        if end_bounds[0] > start_bounds[2] && start_bounds[1] > end_bounds[3] {
            let c_x = first[2] + (second[0] - first[2]) / 2.0;
            let c_y = second[3] + (first[1] - second[3]) / 2.0;
            if cross(
                [
                    start_bounds[2] - end_center[0],
                    start_bounds[1] - end_center[1],
                ],
                [
                    start_bounds[0] - end_center[0],
                    start_bounds[3] - end_center[1],
                ],
            ) > 0.0
            {
                return [
                    [first[0], first[1], c_x, first[3]],
                    [c_x, second[1], second[2], second[3]],
                ];
            }
            return [
                [first[0], c_y, first[2], first[3]],
                [second[0], second[1], second[2], c_y],
            ];
        }

        if start_bounds[2] < end_bounds[0] && start_bounds[3] < end_bounds[1] {
            let c_x = first[2] + (second[0] - first[2]) / 2.0;
            let c_y = first[3] + (second[1] - first[3]) / 2.0;
            if cross(
                [
                    start_bounds[0] - end_center[0],
                    start_bounds[1] - end_center[1],
                ],
                [
                    start_bounds[2] - end_center[0],
                    start_bounds[3] - end_center[1],
                ],
            ) > 0.0
            {
                return [
                    [first[0], first[1], first[2], c_y],
                    [second[0], c_y, second[2], second[3]],
                ];
            }
            return [
                [first[0], first[1], c_x, first[3]],
                [c_x, second[1], second[2], second[3]],
            ];
        }

        if start_bounds[0] > end_bounds[2] && start_bounds[3] < end_bounds[1] {
            let c_x = second[2] + (first[0] - second[2]) / 2.0;
            let c_y = first[3] + (second[1] - first[3]) / 2.0;
            if cross(
                [
                    start_bounds[2] - end_center[0],
                    start_bounds[1] - end_center[1],
                ],
                [
                    start_bounds[0] - end_center[0],
                    start_bounds[3] - end_center[1],
                ],
            ) > 0.0
            {
                return [
                    [c_x, first[1], first[2], first[3]],
                    [second[0], second[1], c_x, second[3]],
                ];
            }
            return [
                [first[0], first[1], first[2], c_y],
                [second[0], c_y, second[2], second[3]],
            ];
        }

        if start_bounds[0] > end_bounds[2] && start_bounds[1] > end_bounds[3] {
            let c_x = second[2] + (first[0] - second[2]) / 2.0;
            let c_y = second[3] + (first[1] - second[3]) / 2.0;
            if cross(
                [
                    start_bounds[0] - end_center[0],
                    start_bounds[1] - end_center[1],
                ],
                [
                    start_bounds[2] - end_center[0],
                    start_bounds[3] - end_center[1],
                ],
            ) > 0.0
            {
                return [
                    [c_x, first[1], first[2], first[3]],
                    [second[0], second[1], c_x, second[3]],
                ];
            }
            return [
                [first[0], c_y, first[2], first[3]],
                [second[0], second[1], second[2], c_y],
            ];
        }
    }

    [first, second]
}

fn make_grid(
    aabbs: &[Bounds],
    start: Point,
    start_heading: Heading,
    end: Point,
    end_heading: Heading,
    common: Bounds,
) -> Grid {
    let mut xs = Vec::new();
    let mut ys = Vec::new();

    if matches!(start_heading, Heading::Left | Heading::Right) {
        approximate_push(&mut ys, start[1]);
    } else {
        approximate_push(&mut xs, start[0]);
    }

    if matches!(end_heading, Heading::Left | Heading::Right) {
        approximate_push(&mut ys, end[1]);
    } else {
        approximate_push(&mut xs, end[0]);
    }

    for aabb in aabbs {
        approximate_push(&mut xs, aabb[0]);
        approximate_push(&mut xs, aabb[2]);
        approximate_push(&mut ys, aabb[1]);
        approximate_push(&mut ys, aabb[3]);
    }

    approximate_push(&mut xs, common[0]);
    approximate_push(&mut xs, common[2]);
    approximate_push(&mut ys, common[1]);
    approximate_push(&mut ys, common[3]);

    xs.sort_by(|left, right| left.partial_cmp(right).unwrap_or(std::cmp::Ordering::Equal));
    ys.sort_by(|left, right| left.partial_cmp(right).unwrap_or(std::cmp::Ordering::Equal));

    let mut nodes = Vec::new();
    let mut by_coord = BTreeMap::new();
    for (row, y) in ys.iter().enumerate() {
        for (col, x) in xs.iter().enumerate() {
            let node = GridNode {
                x: *x,
                y: *y,
                col,
                row,
            };
            by_coord.insert(grid_coord_key([node.x, node.y]), node.clone());
            nodes.push(node);
        }
    }

    Grid {
        nodes,
        by_coord,
        cols: xs.len(),
        rows: ys.len(),
    }
}

fn grid_coord_key(point: Point) -> String {
    format!("{:.6}:{:.6}", point[0], point[1])
}

fn point_to_grid_node(point: Point, grid: &Grid) -> Option<&GridNode> {
    grid.by_coord.get(&grid_coord_key(point))
}

fn neighbors_of<'a>(node: &GridNode, grid: &'a Grid) -> Vec<&'a GridNode> {
    let mut candidates = Vec::new();
    let at = |col: isize, row: isize| -> Option<&GridNode> {
        if col < 0 || row < 0 {
            return None;
        }
        let col = col as usize;
        let row = row as usize;
        if col >= grid.cols || row >= grid.rows {
            return None;
        }
        grid.nodes.get(row * grid.cols + col)
    };

    if let Some(node) = at(node.col as isize, node.row as isize - 1) {
        candidates.push(node);
    }
    if let Some(node) = at(node.col as isize + 1, node.row as isize) {
        candidates.push(node);
    }
    if let Some(node) = at(node.col as isize, node.row as isize + 1) {
        candidates.push(node);
    }
    if let Some(node) = at(node.col as isize - 1, node.row as isize) {
        candidates.push(node);
    }

    candidates
}

fn heading_between(from: &GridNode, to: &GridNode) -> Heading {
    if to.x > from.x {
        Heading::Right
    } else if to.x < from.x {
        Heading::Left
    } else if to.y > from.y {
        Heading::Down
    } else {
        Heading::Up
    }
}

fn estimate_segment_count(
    start: &GridNode,
    end: &GridNode,
    start_heading: Heading,
    end_heading: Heading,
) -> f64 {
    match end_heading {
        Heading::Right => match start_heading {
            Heading::Right => {
                if start.x >= end.x {
                    4.0
                } else if (start.y - end.y).abs() <= 1e-6 {
                    0.0
                } else {
                    2.0
                }
            }
            Heading::Up => {
                if start.y > end.y && start.x < end.x {
                    1.0
                } else {
                    3.0
                }
            }
            Heading::Down => {
                if start.y < end.y && start.x < end.x {
                    1.0
                } else {
                    3.0
                }
            }
            Heading::Left => {
                if (start.y - end.y).abs() <= 1e-6 {
                    4.0
                } else {
                    2.0
                }
            }
        },
        Heading::Left => match start_heading {
            Heading::Right => {
                if (start.y - end.y).abs() <= 1e-6 {
                    4.0
                } else {
                    2.0
                }
            }
            Heading::Up => {
                if start.y > end.y && start.x > end.x {
                    1.0
                } else {
                    3.0
                }
            }
            Heading::Down => {
                if start.y < end.y && start.x > end.x {
                    1.0
                } else {
                    3.0
                }
            }
            Heading::Left => {
                if start.x <= end.x {
                    4.0
                } else if (start.y - end.y).abs() <= 1e-6 {
                    0.0
                } else {
                    2.0
                }
            }
        },
        Heading::Up => match start_heading {
            Heading::Right => {
                if start.y > end.y && start.x < end.x {
                    1.0
                } else {
                    3.0
                }
            }
            Heading::Up => {
                if start.y >= end.y {
                    4.0
                } else if (start.x - end.x).abs() <= 1e-6 {
                    0.0
                } else {
                    2.0
                }
            }
            Heading::Down => {
                if (start.x - end.x).abs() <= 1e-6 {
                    4.0
                } else {
                    2.0
                }
            }
            Heading::Left => {
                if start.y > end.y && start.x > end.x {
                    1.0
                } else {
                    3.0
                }
            }
        },
        Heading::Down => match start_heading {
            Heading::Right => {
                if start.y < end.y && start.x < end.x {
                    1.0
                } else {
                    3.0
                }
            }
            Heading::Up => {
                if (start.x - end.x).abs() <= 1e-6 {
                    4.0
                } else {
                    2.0
                }
            }
            Heading::Down => {
                if start.y <= end.y {
                    4.0
                } else if (start.x - end.x).abs() <= 1e-6 {
                    0.0
                } else {
                    2.0
                }
            }
            Heading::Left => {
                if start.y < end.y && start.x > end.x {
                    1.0
                } else {
                    3.0
                }
            }
        },
    }
}

fn reconstruct_path(end_key: &str, visited: &BTreeMap<String, QueueNode>) -> Vec<Point> {
    let mut out = Vec::new();
    let mut key = Some(end_key.to_owned());
    while let Some(current_key) = key {
        let Some(node) = visited.get(&current_key) else {
            break;
        };
        out.push([node.node.x, node.node.y]);
        key = node.parent_key.clone();
    }
    out.reverse();
    out
}

fn route_a_star(
    start: &GridNode,
    end: &GridNode,
    start_heading: Heading,
    end_heading: Heading,
    grid: &Grid,
    aabbs: &[Bounds],
    closed_keys: &[String],
) -> Option<Vec<Point>> {
    let start_key = grid_coord_key([start.x, start.y]);
    let end_key = grid_coord_key([end.x, end.y]);
    let mut open = vec![start_key.clone()];
    let mut visited = BTreeMap::new();
    visited.insert(
        start_key.clone(),
        QueueNode {
            key: start_key.clone(),
            node: start.clone(),
            g: 0.0,
            f: 0.0,
            closed: false,
            visited: true,
            parent_key: None,
        },
    );

    let bend_multiplier = manhattan([start.x, start.y], [end.x, end.y]);

    while !open.is_empty() {
        open.sort_by(|left, right| {
            visited
                .get(left)
                .expect("open node should be tracked")
                .f
                .partial_cmp(&visited.get(right).expect("open node should be tracked").f)
                .unwrap_or(std::cmp::Ordering::Equal)
        });
        let current_key = open.remove(0);
        let Some(current) = visited.get(&current_key).cloned() else {
            continue;
        };
        if current.closed {
            continue;
        }

        if current.key == end_key {
            return Some(reconstruct_path(&current.key, &visited));
        }

        let Some(current_state) = visited.get_mut(&current.key) else {
            continue;
        };
        current_state.closed = true;

        for candidate in neighbors_of(&current.node, grid) {
            let candidate_key = grid_coord_key([candidate.x, candidate.y]);
            if closed_keys
                .iter()
                .any(|closed_key| closed_key == &candidate_key)
            {
                continue;
            }
            if visited
                .get(&candidate_key)
                .is_some_and(|state| state.closed)
            {
                continue;
            }

            let midpoint = [
                (current.node.x + candidate.x) / 2.0,
                (current.node.y + candidate.y) / 2.0,
            ];
            if aabbs.iter().any(|aabb| point_in_bounds(midpoint, *aabb)) {
                continue;
            }

            let next_heading = heading_between(&current.node, candidate);
            let previous_direction = if let Some(parent_key) = current.parent_key.as_ref() {
                let parent = visited.get(parent_key)?;
                heading_between(&parent.node, &current.node)
            } else {
                start_heading
            };
            let reverse_heading_for_current = reverse_heading(previous_direction);
            let candidate_is_reverse_route = next_heading == reverse_heading_for_current
                || (candidate_key == start_key && next_heading == start_heading)
                || (candidate_key == end_key && next_heading == end_heading);
            if candidate_is_reverse_route {
                continue;
            }

            let direction_change = previous_direction != next_heading;
            let g = current.g
                + manhattan([current.node.x, current.node.y], [candidate.x, candidate.y])
                + if direction_change {
                    bend_multiplier.powi(3)
                } else {
                    0.0
                };

            let been_visited = visited
                .get(&candidate_key)
                .is_some_and(|state| state.visited);
            if !been_visited
                || g < visited
                    .get(&candidate_key)
                    .map_or(f64::INFINITY, |state| state.g)
            {
                let h = manhattan([end.x, end.y], [candidate.x, candidate.y])
                    + estimate_segment_count(candidate, end, next_heading, end_heading)
                        * bend_multiplier.powi(2);
                let next = QueueNode {
                    key: candidate_key.clone(),
                    node: candidate.clone(),
                    g,
                    f: g + h,
                    closed: false,
                    visited: true,
                    parent_key: Some(current.key.clone()),
                };
                visited.insert(candidate_key.clone(), next.clone());
                if !open.iter().any(|queued| queued == &candidate_key) {
                    open.push(candidate_key);
                }
            }
        }
    }

    None
}

pub(crate) fn remove_short_segments(points: &[Point]) -> Vec<Point> {
    if points.len() < 3 {
        return points.to_vec();
    }

    let mut out = vec![points[0]];
    for &current in points.iter().take(points.len() - 1).skip(1) {
        let previous = *out.last().unwrap();
        if distance(previous, current) <= DEDUP_THRESHOLD {
            continue;
        }
        out.push(current);
    }
    out.push(points[points.len() - 1]);
    out
}

pub(crate) fn ensure_orthogonal(points: &[Point]) -> Vec<Point> {
    if points.len() < 2 {
        return points.to_vec();
    }

    let mut result = vec![points[0]];
    for point in points.iter().skip(1) {
        let previous = *result.last().unwrap();
        let same_x = (previous[0] - point[0]).abs() <= 1e-6;
        let same_y = (previous[1] - point[1]).abs() <= 1e-6;
        if same_x || same_y {
            result.push(*point);
            continue;
        }

        let preferred_horizontal = (point[0] - previous[0]).abs() >= (point[1] - previous[1]).abs();
        let bend = if preferred_horizontal {
            [point[0], previous[1]]
        } else {
            [previous[0], point[1]]
        };
        result.push(bend);
        result.push(*point);
    }

    dedupe_collinear_points(&result)
}

pub(crate) fn route_between_points(
    start: EndpointRoute,
    end: EndpointRoute,
    _obstacles: &[Bounds],
) -> Option<Vec<Point>> {
    let midpoint_route = |points: &[Point]| {
        dedupe_collinear_points(&remove_short_segments(&ensure_orthogonal(points)))
    };
    let start_point_bounds = point_bounds(start.point);
    let end_point_bounds = point_bounds(end.point);
    let bounds_overlap = point_in_bounds(start.point, end.overlap_bounds)
        || point_in_bounds(end.point, start.overlap_bounds);
    let any_bindable = start.bindable_bounds.is_some() || end.bindable_bounds.is_some();
    let common_bounds = common_aabb(&[
        if bounds_overlap {
            start_point_bounds
        } else {
            start.element_bounds
        },
        if bounds_overlap {
            end_point_bounds
        } else {
            end.element_bounds
        },
    ]);
    let dynamic_aabbs = generate_dynamic_aabbs(DynamicAabbInput {
        start_bounds: if bounds_overlap {
            start_point_bounds
        } else {
            start.element_bounds
        },
        end_bounds: if bounds_overlap {
            end_point_bounds
        } else {
            end.element_bounds
        },
        common: common_bounds,
        start_difference: Some(if bounds_overlap {
            offset_from_heading(
                start.heading,
                if any_bindable { BASE_PADDING } else { 0.0 },
                0.0,
            )
        } else {
            offset_from_heading(
                start.heading,
                if any_bindable {
                    BASE_PADDING
                        - if start.has_arrowhead {
                            BASE_BINDING_GAP_ELBOW * 6.0
                        } else {
                            BASE_BINDING_GAP_ELBOW * 2.0
                        }
                } else {
                    0.0
                },
                BASE_PADDING,
            )
        }),
        end_difference: Some(if bounds_overlap {
            offset_from_heading(
                end.heading,
                if any_bindable { BASE_PADDING } else { 0.0 },
                0.0,
            )
        } else {
            offset_from_heading(
                end.heading,
                if any_bindable {
                    BASE_PADDING
                        - if end.has_arrowhead {
                            BASE_BINDING_GAP_ELBOW * 6.0
                        } else {
                            BASE_BINDING_GAP_ELBOW * 2.0
                        }
                } else {
                    0.0
                },
                BASE_PADDING,
            )
        }),
        disable_side_hack: bounds_overlap,
        start_bindable_bounds: start.bindable_bounds,
        end_bindable_bounds: end.bindable_bounds,
    });
    let route_start = get_dongle_position(dynamic_aabbs[0], start.heading, start.point);
    let route_end = get_dongle_position(dynamic_aabbs[1], end.heading, end.point);
    let grid = make_grid(
        &dynamic_aabbs,
        route_start,
        start.heading,
        route_end,
        end.heading,
        common_bounds,
    );
    let start_dongle = point_to_grid_node(route_start, &grid);
    let end_dongle = point_to_grid_node(route_end, &grid);
    let start_node = point_to_grid_node(start.point, &grid);
    let end_node = point_to_grid_node(end.point, &grid);

    let mut closed_keys = Vec::new();
    if start.bindable_bounds.is_some()
        && let Some(node) = start_node
    {
        let key = grid_coord_key([node.x, node.y]);
        if key != grid_coord_key(route_start) {
            closed_keys.push(key);
        }
    }
    if end.bindable_bounds.is_some()
        && let Some(node) = end_node
    {
        let key = grid_coord_key([node.x, node.y]);
        if key != grid_coord_key(route_end) {
            closed_keys.push(key);
        }
    }

    let dongle_overlap = point_in_bounds(route_start, dynamic_aabbs[1])
        || point_in_bounds(route_end, dynamic_aabbs[0]);
    let route = route_a_star(
        start_dongle.or(start_node)?,
        end_dongle.or(end_node)?,
        start.heading,
        end.heading,
        &grid,
        if dongle_overlap { &[] } else { &dynamic_aabbs },
        &closed_keys,
    )?;

    let mut full_path = route;
    if start_dongle.is_some() {
        full_path.insert(0, start.point);
    }
    if end_dongle.is_some() {
        full_path.push(end.point);
    }

    Some(midpoint_route(&full_path))
}
