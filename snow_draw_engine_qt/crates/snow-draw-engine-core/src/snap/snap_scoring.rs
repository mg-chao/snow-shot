use super::*;

impl ObjectSnapService {
    pub(super) fn select_best_candidate(
        candidates: &[AxisCandidate],
        target_rect: DrawRect,
        snap_distance: f64,
    ) -> Option<AxisCandidate> {
        let mut best = None;
        let mut best_strength = 0.0;
        let distance_slack = Self::distance_slack(snap_distance);

        for &candidate in candidates {
            let candidate_strength =
                Self::candidate_strength(candidate, target_rect, snap_distance);
            let should_replace = match best {
                None => true,
                Some(current_best) => {
                    Self::compare_candidates(
                        candidate,
                        current_best,
                        candidate_strength,
                        best_strength,
                        distance_slack,
                    ) > 0
                }
            };

            if should_replace {
                best = Some(candidate);
                best_strength = candidate_strength;
            }
        }

        best
    }

    pub(super) fn distance_slack(snap_distance: f64) -> f64 {
        (snap_distance * Self::PRIORITY_DISTANCE_SLACK_FACTOR)
            .min(Self::PRIORITY_DISTANCE_SLACK_MAX)
    }

    fn candidate_strength(
        candidate: AxisCandidate,
        target_rect: DrawRect,
        snap_distance: f64,
    ) -> f64 {
        let distance_strength = Self::distance_strength(candidate.distance(), snap_distance);

        match candidate.kind {
            SnapKind::Point => Self::clamp01(
                distance_strength * Self::POINT_DISTANCE_WEIGHT
                    + Self::perpendicular_strength(candidate, target_rect, snap_distance)
                        * Self::POINT_PERPENDICULAR_WEIGHT
                    + Self::point_alignment_strength(candidate) * Self::POINT_ANCHOR_WEIGHT,
            ),
            SnapKind::GapCenter | SnapKind::GapSide => Self::clamp01(
                Self::gap_strength_score(
                    distance_strength,
                    candidate.gap_frequency.unwrap_or(0),
                    candidate.kind == SnapKind::GapCenter,
                ) * Self::GAP_STRENGTH_SCALE,
            ),
        }
    }

    fn gap_strength_score(distance_strength: f64, gap_frequency: i32, is_center: bool) -> f64 {
        let frequency_strength = Self::gap_frequency_strength(gap_frequency);
        let kind_strength = if is_center { 1.0 } else { 0.85 };

        distance_strength * Self::GAP_DISTANCE_WEIGHT
            + frequency_strength * Self::GAP_FREQUENCY_WEIGHT
            + kind_strength * Self::GAP_KIND_WEIGHT
    }

    fn distance_strength(distance: f64, snap_distance: f64) -> f64 {
        if snap_distance <= 0.0 {
            return 0.0;
        }

        1.0 - (distance / snap_distance).clamp(0.0, 1.0)
    }

    fn perpendicular_strength(
        candidate: AxisCandidate,
        target_rect: DrawRect,
        snap_distance: f64,
    ) -> f64 {
        let reference_rect = candidate
            .reference_rect
            .expect("point candidate requires reference_rect");
        let perpendicular_distance = candidate.perpendicular_distance.unwrap_or(0.0);

        let range =
            Self::perpendicular_range(target_rect, reference_rect, candidate.axis, snap_distance);
        if range <= 0.0 {
            return 0.0;
        }

        let ratio = (perpendicular_distance / range).min(1.0);
        1.0 - ratio
    }

    fn perpendicular_range(
        target_rect: DrawRect,
        reference_rect: DrawRect,
        axis: SnapAxis,
        snap_distance: f64,
    ) -> f64 {
        let perpendicular_axis = Self::perpendicular_axis(axis);
        let target_size = Self::axis_size(target_rect, perpendicular_axis);
        let reference_size = Self::axis_size(reference_rect, perpendicular_axis);

        let size_range = target_size.max(reference_size) * Self::PERPENDICULAR_SIZE_RANGE_FACTOR;
        let snap_range = snap_distance * Self::PERPENDICULAR_SNAP_RANGE_FACTOR;
        let range = size_range.max(snap_range);

        range.max(snap_distance)
    }

    fn point_alignment_strength(candidate: AxisCandidate) -> f64 {
        let target_anchor = candidate
            .target_anchor
            .expect("point candidate requires target_anchor");
        let reference_anchor = candidate
            .reference_anchor
            .expect("point candidate requires reference_anchor");

        let priority = Self::anchor_priority(target_anchor, reference_anchor) as f64;
        1.0 - (priority / Self::MAX_ANCHOR_PRIORITY)
    }

    fn gap_frequency_strength(gap_frequency: i32) -> f64 {
        if gap_frequency <= 0 {
            return 0.0;
        }

        1.0 - (1.0 / (f64::from(gap_frequency) + 1.0))
    }

    fn clamp01(value: f64) -> f64 {
        if value <= 0.0 {
            return 0.0;
        }
        if value >= 1.0 {
            return 1.0;
        }
        value
    }

    fn compare_candidates(
        left: AxisCandidate,
        right: AxisCandidate,
        left_strength: f64,
        right_strength: f64,
        distance_slack: f64,
    ) -> i32 {
        let strength_delta = left_strength - right_strength;
        if strength_delta.abs() > Self::STRENGTH_SLACK {
            return if strength_delta > 0.0 { 1 } else { -1 };
        }

        let left_exact = Self::is_exact(left.offset);
        let right_exact = Self::is_exact(right.offset);
        if left_exact != right_exact {
            return if left_exact { 1 } else { -1 };
        }

        let distance_delta = left.distance() - right.distance();
        if distance_delta.abs() > distance_slack {
            return if distance_delta < 0.0 { 1 } else { -1 };
        }

        let left_kind_priority = Self::snap_kind_priority(left.kind);
        let right_kind_priority = Self::snap_kind_priority(right.kind);
        if left_kind_priority != right_kind_priority {
            return if left_kind_priority < right_kind_priority {
                1
            } else {
                -1
            };
        }

        let kind_specific = Self::compare_kind_specific_tie_breakers(left, right);
        if kind_specific != 0 {
            return kind_specific;
        }

        if distance_delta.abs() > Self::EPSILON {
            return if distance_delta < 0.0 { 1 } else { -1 };
        }

        0
    }

    fn snap_kind_priority(kind: SnapKind) -> i32 {
        match kind {
            SnapKind::Point => 0,
            SnapKind::GapCenter | SnapKind::GapSide => 1,
        }
    }

    fn compare_kind_specific_tie_breakers(left: AxisCandidate, right: AxisCandidate) -> i32 {
        if left.kind == SnapKind::Point && right.kind == SnapKind::Point {
            return Self::compare_point_candidates(left, right);
        }
        if left.kind != SnapKind::Point && right.kind != SnapKind::Point {
            return Self::compare_gap_candidates(left, right);
        }
        0
    }

    fn compare_point_candidates(left: AxisCandidate, right: AxisCandidate) -> i32 {
        let left_point_priority = Self::point_priority(left);
        let right_point_priority = Self::point_priority(right);
        if left_point_priority != right_point_priority {
            return if left_point_priority < right_point_priority {
                1
            } else {
                -1
            };
        }

        let left_perpendicular_distance = left.perpendicular_distance.unwrap_or(f64::INFINITY);
        let right_perpendicular_distance = right.perpendicular_distance.unwrap_or(f64::INFINITY);
        let perpendicular_delta = left_perpendicular_distance - right_perpendicular_distance;
        if perpendicular_delta.abs() <= Self::EPSILON {
            return 0;
        }

        if perpendicular_delta < 0.0 { 1 } else { -1 }
    }

    fn compare_gap_candidates(left: AxisCandidate, right: AxisCandidate) -> i32 {
        let left_gap_frequency = left.gap_frequency.unwrap_or(0);
        let right_gap_frequency = right.gap_frequency.unwrap_or(0);
        if left_gap_frequency != right_gap_frequency {
            return if left_gap_frequency > right_gap_frequency {
                1
            } else {
                -1
            };
        }

        let left_gap_kind_priority = Self::gap_kind_priority(left);
        let right_gap_kind_priority = Self::gap_kind_priority(right);
        if left_gap_kind_priority == right_gap_kind_priority {
            return 0;
        }

        if left_gap_kind_priority < right_gap_kind_priority {
            1
        } else {
            -1
        }
    }

    fn point_priority(candidate: AxisCandidate) -> i32 {
        let target_anchor = candidate
            .target_anchor
            .expect("point candidate requires target_anchor");
        let reference_anchor = candidate
            .reference_anchor
            .expect("point candidate requires reference_anchor");

        Self::anchor_priority(target_anchor, reference_anchor)
    }

    fn gap_kind_priority(candidate: AxisCandidate) -> i32 {
        match candidate.kind {
            SnapKind::GapCenter => 0,
            SnapKind::Point | SnapKind::GapSide => 1,
        }
    }
}
