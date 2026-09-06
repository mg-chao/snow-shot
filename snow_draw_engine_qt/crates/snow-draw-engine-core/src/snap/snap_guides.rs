use std::cmp::Ordering;

use super::*;

impl ObjectSnapService {
    pub(super) fn append_candidate_guides(
        candidate: Option<AxisCandidate>,
        perpendicular_candidate: Option<AxisCandidate>,
        snapped_rect: DrawRect,
        reference_rects: &[DrawRect],
        snap_distance: f64,
        guides: &mut Vec<SnapGuide>,
    ) {
        let Some(candidate) = candidate else {
            return;
        };

        for guide in
            Self::build_guides_for_candidate(candidate, snapped_rect, perpendicular_candidate)
        {
            if !guides.contains(&guide) {
                guides.push(guide);
            }
        }

        if !Self::is_gap_candidate(candidate) {
            return;
        }

        for guide in Self::build_associated_gap_guides(
            candidate,
            snapped_rect,
            reference_rects,
            snap_distance,
        ) {
            if !guides.contains(&guide) {
                guides.push(guide);
            }
        }
    }

    fn build_guides_for_candidate(
        candidate: AxisCandidate,
        target_rect: DrawRect,
        perpendicular_candidate: Option<AxisCandidate>,
    ) -> Vec<SnapGuide> {
        if candidate.kind == SnapKind::GapCenter {
            let split_guides = Self::build_split_gap_center_guides(candidate, target_rect);
            if !split_guides.is_empty() {
                return split_guides;
            }
        }

        vec![Self::build_guide(
            candidate,
            target_rect,
            perpendicular_candidate,
        )]
    }

    fn build_guide(
        candidate: AxisCandidate,
        target_rect: DrawRect,
        perpendicular_candidate: Option<AxisCandidate>,
    ) -> SnapGuide {
        match candidate.kind {
            SnapKind::Point => {
                Self::build_point_guide(candidate, target_rect, perpendicular_candidate)
            }
            SnapKind::GapCenter | SnapKind::GapSide => {
                Self::build_gap_guide(candidate, target_rect)
            }
        }
    }

    fn is_gap_candidate(candidate: AxisCandidate) -> bool {
        matches!(candidate.kind, SnapKind::GapCenter | SnapKind::GapSide)
    }

    fn build_associated_gap_guides(
        candidate: AxisCandidate,
        target_rect: DrawRect,
        reference_rects: &[DrawRect],
        snap_distance: f64,
    ) -> Vec<SnapGuide> {
        let gap_size = candidate.gap_size.expect("gap candidate requires gap_size");
        let gap_tolerance = Self::EPSILON.max(Self::distance_slack(snap_distance));

        let filtered =
            Self::resolve_gap_reference_rects(candidate.axis, target_rect, reference_rects);
        if filtered.len() < 2 {
            return Vec::new();
        }

        let segments = Self::build_gap_segments(candidate.axis, &filtered);
        if segments.is_empty() {
            return Vec::new();
        }

        let mut matching_segments = Vec::new();
        for segment in segments {
            if (segment.gap - gap_size).abs() > gap_tolerance {
                continue;
            }
            if Self::matches_gap_segment(candidate, segment) {
                continue;
            }
            matching_segments.push(segment);
        }

        if matching_segments.is_empty() {
            return Vec::new();
        }

        matching_segments.sort_by(|left, right| {
            Self::gap_segment_distance_to_target(*left, target_rect, candidate.axis)
                .partial_cmp(&Self::gap_segment_distance_to_target(
                    *right,
                    target_rect,
                    candidate.axis,
                ))
                .unwrap_or(Ordering::Equal)
        });

        let limit = Self::MAX_ASSOCIATED_GAP_GUIDES.min(matching_segments.len());
        let mut guides = Vec::with_capacity(limit);

        for segment in matching_segments.into_iter().take(limit) {
            let segment_candidate = AxisCandidate::gap_center(
                candidate.axis,
                0.0,
                segment.before,
                segment.after,
                gap_size,
                candidate.gap_frequency,
            );
            guides.push(Self::build_gap_guide(segment_candidate, target_rect));
        }

        guides
    }

    fn matches_gap_segment(candidate: AxisCandidate, segment: GapSegment) -> bool {
        if candidate.kind != SnapKind::GapCenter {
            return false;
        }

        candidate.gap_before_rect == Some(segment.before)
            && candidate.gap_after_rect == Some(segment.after)
    }

    fn gap_segment_distance_to_target(
        segment: GapSegment,
        target_rect: DrawRect,
        axis: SnapAxis,
    ) -> f64 {
        let target_center = Self::axis_center(target_rect, axis);
        let segment_center =
            (Self::axis_max(segment.before, axis) + Self::axis_min(segment.after, axis)) / 2.0;
        (segment_center - target_center).abs()
    }

    fn build_point_guide(
        candidate: AxisCandidate,
        target_rect: DrawRect,
        perpendicular_candidate: Option<AxisCandidate>,
    ) -> SnapGuide {
        let reference_rect = candidate
            .reference_rect
            .expect("point candidate requires reference_rect");
        let reference_anchor = candidate
            .reference_anchor
            .expect("point candidate requires reference_anchor");
        let snap_pos = Self::anchor_position(reference_rect, candidate.axis, reference_anchor);

        let markers = Self::resolve_point_markers(
            candidate,
            target_rect,
            reference_rect,
            snap_pos,
            perpendicular_candidate,
        );

        if candidate.axis == SnapAxis::X {
            let min_y = reference_rect.min_y.min(target_rect.min_y);
            let max_y = reference_rect.max_y.max(target_rect.max_y);
            return SnapGuide {
                kind: SnapGuideKind::Point,
                axis: SnapGuideAxis::Vertical,
                start: Point::new(snap_pos, min_y),
                end: Point::new(snap_pos, max_y),
                markers,
                label: None,
            };
        }

        let min_x = reference_rect.min_x.min(target_rect.min_x);
        let max_x = reference_rect.max_x.max(target_rect.max_x);
        SnapGuide {
            kind: SnapGuideKind::Point,
            axis: SnapGuideAxis::Horizontal,
            start: Point::new(min_x, snap_pos),
            end: Point::new(max_x, snap_pos),
            markers,
            label: None,
        }
    }

    fn build_gap_guide(candidate: AxisCandidate, target_rect: DrawRect) -> SnapGuide {
        let (start, end) = Self::gap_bounds(candidate, target_rect);
        Self::build_gap_guide_for_bounds(
            candidate.axis,
            target_rect,
            start,
            end,
            candidate.gap_size.expect("gap candidate requires gap_size"),
        )
    }

    fn build_split_gap_center_guides(
        candidate: AxisCandidate,
        target_rect: DrawRect,
    ) -> Vec<SnapGuide> {
        let before = candidate
            .gap_before_rect
            .expect("gapCenter candidate requires gap_before_rect");
        let after = candidate
            .gap_after_rect
            .expect("gapCenter candidate requires gap_after_rect");
        let axis = candidate.axis;

        let gap_start = Self::axis_max(before, axis);
        let gap_end = Self::axis_min(after, axis);
        let target_start = Self::axis_min(target_rect, axis);
        let target_end = Self::axis_max(target_rect, axis);

        if target_start <= gap_start + Self::EPSILON || target_end >= gap_end - Self::EPSILON {
            return Vec::new();
        }

        let gap_size = candidate
            .gap_size
            .expect("gapCenter candidate requires gap_size");
        vec![
            Self::build_gap_guide_for_bounds(axis, target_rect, gap_start, target_start, gap_size),
            Self::build_gap_guide_for_bounds(axis, target_rect, target_end, gap_end, gap_size),
        ]
    }

    fn build_gap_guide_for_bounds(
        axis: SnapAxis,
        target_rect: DrawRect,
        start: f64,
        end: f64,
        gap_size: f64,
    ) -> SnapGuide {
        if axis == SnapAxis::X {
            let y = target_rect.center_y();
            let start_point = Point::new(start, y);
            let end_point = Point::new(end, y);
            return SnapGuide {
                kind: SnapGuideKind::Gap,
                axis: SnapGuideAxis::Horizontal,
                start: start_point,
                end: end_point,
                markers: vec![start_point, end_point],
                label: Some(gap_size),
            };
        }

        let x = target_rect.center_x();
        let start_point = Point::new(x, start);
        let end_point = Point::new(x, end);
        SnapGuide {
            kind: SnapGuideKind::Gap,
            axis: SnapGuideAxis::Vertical,
            start: start_point,
            end: end_point,
            markers: vec![start_point, end_point],
            label: Some(gap_size),
        }
    }

    fn gap_bounds(candidate: AxisCandidate, target_rect: DrawRect) -> (f64, f64) {
        let axis = candidate.axis;
        match candidate.kind {
            SnapKind::GapCenter => {
                let before = candidate
                    .gap_before_rect
                    .expect("gapCenter candidate requires gap_before_rect");
                let after = candidate
                    .gap_after_rect
                    .expect("gapCenter candidate requires gap_after_rect");
                (Self::axis_max(before, axis), Self::axis_min(after, axis))
            }
            SnapKind::GapSide => {
                let reference_rect = candidate
                    .reference_rect
                    .expect("gapSide candidate requires reference_rect");
                if candidate.gap_side == Some(GapSide::After) {
                    (
                        Self::axis_max(reference_rect, axis),
                        Self::axis_min(target_rect, axis),
                    )
                } else {
                    (
                        Self::axis_max(target_rect, axis),
                        Self::axis_min(reference_rect, axis),
                    )
                }
            }
            SnapKind::Point => panic!("gap bounds requested for non-gap candidate"),
        }
    }

    fn resolve_point_markers(
        candidate: AxisCandidate,
        target_rect: DrawRect,
        reference_rect: DrawRect,
        snap_pos: f64,
        perpendicular_candidate: Option<AxisCandidate>,
    ) -> Vec<Point<f64>> {
        let axis = candidate.axis;
        let perpendicular_axis = Self::perpendicular_axis(axis);

        let target_perp_anchor = if perpendicular_candidate
            .is_some_and(|candidate| candidate.axis == perpendicular_axis)
        {
            perpendicular_candidate.and_then(|candidate| candidate.target_anchor)
        } else {
            None
        };

        let reference_perp_anchor = if perpendicular_candidate.is_some_and(|candidate| {
            candidate.axis == perpendicular_axis && candidate.reference_rect == Some(reference_rect)
        }) {
            perpendicular_candidate.and_then(|candidate| candidate.reference_anchor)
        } else {
            None
        };

        let target_perp = target_perp_anchor
            .map(|anchor| Self::anchor_position(target_rect, perpendicular_axis, anchor))
            .unwrap_or_else(|| Self::axis_center(target_rect, perpendicular_axis));
        let reference_perp = reference_perp_anchor
            .map(|anchor| Self::anchor_position(reference_rect, perpendicular_axis, anchor))
            .unwrap_or_else(|| Self::axis_center(reference_rect, perpendicular_axis));

        let primary = if axis == SnapAxis::X {
            Point::new(snap_pos, target_perp)
        } else {
            Point::new(target_perp, snap_pos)
        };
        let secondary = if axis == SnapAxis::X {
            Point::new(snap_pos, reference_perp)
        } else {
            Point::new(reference_perp, snap_pos)
        };

        if primary == secondary {
            vec![primary]
        } else {
            vec![primary, secondary]
        }
    }
}
