use snow_draw_engine_core::Point;
use snow_draw_engine_document::PenFilterData;

fn point_segment_distance_squared(point: Point<f64>, start: Point<f64>, end: Point<f64>) -> f64 {
    let dx = end.x - start.x;
    let dy = end.y - start.y;
    let length_squared = dx * dx + dy * dy;
    if length_squared <= f64::EPSILON {
        let px = point.x - start.x;
        let py = point.y - start.y;
        return px * px + py * py;
    }
    let t =
        (((point.x - start.x) * dx + (point.y - start.y) * dy) / length_squared).clamp(0.0, 1.0);
    let projected_x = start.x + t * dx;
    let projected_y = start.y + t * dy;
    let px = point.x - projected_x;
    let py = point.y - projected_y;
    px * px + py * py
}

fn retained_indices(points: &[Point<f64>], epsilon: f64) -> Vec<usize> {
    if points.len() <= 2 || !epsilon.is_finite() || epsilon <= 0.0 {
        return (0..points.len()).collect();
    }

    let threshold = epsilon * epsilon;
    let mut retained = vec![false; points.len()];
    retained[0] = true;
    retained[points.len() - 1] = true;
    let mut ranges = vec![(0usize, points.len() - 1)];

    while let Some((start, end)) = ranges.pop() {
        if end <= start + 1 {
            continue;
        }
        let mut farthest_index = start;
        let mut farthest_distance = threshold;
        for index in start + 1..end {
            let distance =
                point_segment_distance_squared(points[index], points[start], points[end]);
            if distance > farthest_distance {
                farthest_distance = distance;
                farthest_index = index;
            }
        }
        if farthest_index != start {
            retained[farthest_index] = true;
            ranges.push((start, farthest_index));
            ranges.push((farthest_index, end));
        }
    }

    retained
        .into_iter()
        .enumerate()
        .filter_map(|(index, keep)| keep.then_some(index))
        .collect()
}

pub(crate) fn simplify_polyline(points: &[Point<f64>], epsilon: f64) -> Vec<Point<f64>> {
    retained_indices(points, epsilon)
        .iter()
        .map(|index| points[*index])
        .collect()
}

pub(crate) fn simplify_pen_filter_geometry(filter: &mut PenFilterData) {
    let global_points = filter.global_points();
    let epsilon = (filter.stroke_width.max(0.0) * 0.04).clamp(0.75, 2.0);
    let retained = retained_indices(&global_points, epsilon);
    if retained.len() >= 2 && retained.len() < filter.points.len() {
        filter.points = retained
            .into_iter()
            .map(|index| filter.points[index])
            .collect();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preserves_endpoints_and_meaningful_turns() {
        let points = [
            Point::new(0.0, 0.0),
            Point::new(1.0, 0.02),
            Point::new(2.0, -0.02),
            Point::new(3.0, 2.0),
            Point::new(4.0, 2.0),
        ];
        let simplified = simplify_polyline(&points, 0.1);
        assert_eq!(simplified.first(), points.first());
        assert_eq!(simplified.last(), points.last());
        assert!(simplified.contains(&Point::new(2.0, -0.02)));
        assert!(simplified.len() < points.len());
    }

    #[test]
    fn handles_large_input_without_recursion() {
        let points = (0..128_000)
            .map(|index| Point::new(index as f64, (index % 2) as f64 * 0.001))
            .collect::<Vec<_>>();
        let simplified = simplify_polyline(&points, 0.01);
        assert_eq!(simplified, vec![points[0], *points.last().unwrap()]);
    }

    #[test]
    fn geometry_edit_simplification_preserves_element_transform() {
        let global = (0..100)
            .map(|index| Point::new(index as f64, (index % 2) as f64 * 0.05))
            .collect::<Vec<_>>();
        let mut filter = PenFilterData::from_global_points(
            &global,
            snow_draw_engine_document::CanvasFilterType::Inversion,
            1.0,
            20.0,
            1.0,
        )
        .unwrap();
        filter.rotation = 0.25;
        let transform = (
            filter.x,
            filter.y,
            filter.width,
            filter.height,
            filter.rotation,
        );
        simplify_pen_filter_geometry(&mut filter);
        assert!(filter.points.len() <= 3);
        assert_eq!(
            (
                filter.x,
                filter.y,
                filter.width,
                filter.height,
                filter.rotation
            ),
            transform
        );
    }
}
