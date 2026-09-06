use crate::arrow_elbow_router::BASE_PADDING;
use crate::arrow_geom::{Heading, is_horizontal_heading, vector_to_heading};
use crate::{ArrowPatch, ArrowState, BindableState, FixedSegment, Point};

use super::{
    heading_for_point_is_horizontal, normalize_arrow_element_update, to_global_point,
    to_local_point,
};

pub(super) struct EndpointDragRouteInput<'a> {
    pub(super) arrow: &'a ArrowState,
    pub(super) updated_points: &'a [Point],
    pub(super) fixed_segments: &'a [FixedSegment],
    pub(super) start_heading: Heading,
    pub(super) end_heading: Heading,
    pub(super) start_global_point: Point,
    pub(super) end_global_point: Point,
    pub(super) hovered_start_bindable: Option<&'a BindableState>,
    pub(super) hovered_end_bindable: Option<&'a BindableState>,
    pub(super) max_coordinate: f64,
}

pub(super) fn handle_endpoint_drag(input: EndpointDragRouteInput<'_>) -> ArrowPatch {
    let EndpointDragRouteInput {
        arrow,
        updated_points,
        fixed_segments,
        start_heading,
        end_heading,
        start_global_point,
        end_global_point,
        hovered_start_bindable,
        hovered_end_bindable,
        max_coordinate,
    } = input;
    let mut start_is_special = arrow.start_is_special;
    let mut end_is_special = arrow.end_is_special;
    let global_updated_points = updated_points
        .iter()
        .enumerate()
        .map(|(index, point)| {
            if index == 0 || index + 1 == updated_points.len() {
                to_global_point([arrow.x, arrow.y], *point)
            } else {
                to_global_point([arrow.x, arrow.y], arrow.points[index])
            }
        })
        .collect::<Vec<_>>();
    let mut next_fixed_segments = fixed_segments
        .iter()
        .map(|segment| FixedSegment {
            index: segment.index,
            start: [
                arrow.x + (segment.start[0] - updated_points[0][0]),
                arrow.y + (segment.start[1] - updated_points[0][1]),
            ],
            end: [
                arrow.x + (segment.end[0] - updated_points[0][0]),
                arrow.y + (segment.end[1] - updated_points[0][1]),
            ],
        })
        .collect::<Vec<_>>();
    let mut new_points = Vec::new();

    let offset = 2 + usize::from(start_is_special.unwrap_or(false));
    let end_offset = 2 + usize::from(end_is_special.unwrap_or(false));
    while new_points.len() + offset < global_updated_points.len().saturating_sub(end_offset) {
        new_points.push(global_updated_points[new_points.len() + offset]);
    }

    {
        let second_point_index = if start_is_special.unwrap_or(false) {
            2
        } else {
            1
        };
        let third_point_index = if start_is_special.unwrap_or(false) {
            3
        } else {
            2
        };
        let second_point = global_updated_points[second_point_index];
        let third_point = global_updated_points[third_point_index];

        let start_is_horizontal = is_horizontal_heading(start_heading);
        let second_is_horizontal =
            is_horizontal_heading(vector_to_heading(second_point, third_point));

        if hovered_start_bindable.is_some() && start_is_horizontal == second_is_horizontal {
            let positive = if start_is_horizontal {
                start_heading == Heading::Right
            } else {
                start_heading == Heading::Down
            };
            let padding = if positive {
                BASE_PADDING
            } else {
                -BASE_PADDING
            };
            new_points.insert(
                0,
                [
                    if second_is_horizontal {
                        start_global_point[0] + padding
                    } else {
                        third_point[0]
                    },
                    if second_is_horizontal {
                        third_point[1]
                    } else {
                        start_global_point[1] + padding
                    },
                ],
            );
            new_points.insert(
                0,
                [
                    if start_is_horizontal {
                        start_global_point[0] + padding
                    } else {
                        start_global_point[0]
                    },
                    if start_is_horizontal {
                        start_global_point[1]
                    } else {
                        start_global_point[1] + padding
                    },
                ],
            );
            if !start_is_special.unwrap_or(false) {
                start_is_special = Some(true);
                for segment in &mut next_fixed_segments {
                    if segment.index > 1 {
                        segment.index += 1;
                    }
                }
            }
        } else {
            new_points.insert(
                0,
                [
                    if second_is_horizontal {
                        start_global_point[0]
                    } else {
                        second_point[0]
                    },
                    if second_is_horizontal {
                        second_point[1]
                    } else {
                        start_global_point[1]
                    },
                ],
            );
            if start_is_special.unwrap_or(false) {
                start_is_special = Some(false);
                for segment in &mut next_fixed_segments {
                    if segment.index > 1 {
                        segment.index -= 1;
                    }
                }
            }
        }
        new_points.insert(0, start_global_point);
    }

    {
        let second_to_last_index = global_updated_points.len()
            - if end_is_special.unwrap_or(false) {
                3
            } else {
                2
            };
        let third_to_last_index = global_updated_points.len()
            - if end_is_special.unwrap_or(false) {
                4
            } else {
                3
            };
        let second_to_last_point = global_updated_points[second_to_last_index];
        let third_to_last_point = global_updated_points[third_to_last_index];

        let end_is_horizontal = is_horizontal_heading(end_heading);
        let second_is_horizontal =
            heading_for_point_is_horizontal(third_to_last_point, second_to_last_point);
        if hovered_end_bindable.is_some() && end_is_horizontal == second_is_horizontal {
            let positive = if end_is_horizontal {
                end_heading == Heading::Right
            } else {
                end_heading == Heading::Down
            };
            let padding = if positive {
                BASE_PADDING
            } else {
                -BASE_PADDING
            };
            new_points.push([
                if second_is_horizontal {
                    end_global_point[0] + padding
                } else {
                    third_to_last_point[0]
                },
                if second_is_horizontal {
                    third_to_last_point[1]
                } else {
                    end_global_point[1] + padding
                },
            ]);
            new_points.push([
                if end_is_horizontal {
                    end_global_point[0] + padding
                } else {
                    end_global_point[0]
                },
                if end_is_horizontal {
                    end_global_point[1]
                } else {
                    end_global_point[1] + padding
                },
            ]);
            if !end_is_special.unwrap_or(false) {
                end_is_special = Some(true);
            }
        } else {
            new_points.push([
                if second_is_horizontal {
                    end_global_point[0]
                } else {
                    second_to_last_point[0]
                },
                if second_is_horizontal {
                    second_to_last_point[1]
                } else {
                    end_global_point[1]
                },
            ]);
            if end_is_special.unwrap_or(false) {
                end_is_special = Some(false);
            }
        }
    }

    new_points.push(end_global_point);

    normalize_arrow_element_update(
        &new_points,
        Some(
            next_fixed_segments
                .iter()
                .map(|segment| FixedSegment {
                    index: segment.index,
                    start: new_points[segment.index - 1],
                    end: new_points[segment.index],
                })
                .map(|segment| FixedSegment {
                    index: segment.index,
                    start: to_local_point(start_global_point, segment.start),
                    end: to_local_point(start_global_point, segment.end),
                })
                .collect(),
        ),
        Some(start_is_special),
        Some(end_is_special),
        max_coordinate,
    )
}
