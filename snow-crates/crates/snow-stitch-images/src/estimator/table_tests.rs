use super::*;
use crate::{StitchOptions, stitch};

// Several short repeating labels in each column, with different periods across columns.
// Matching a single cell or even a whole row of grid corners admits several displacements.
fn table(axis: StitchAxis, extent: u32, scroll: u32) -> Frame {
    let across = 544;
    let (width, height) = match axis {
        StitchAxis::Vertical => (across, extent),
        StitchAxis::Horizontal => (extent, across),
    };
    let mut pixels = Vec::with_capacity((width * height) as usize);
    for y in 0..height {
        for x in 0..width {
            let (cross, along) = match axis {
                StitchAxis::Vertical => (x, y),
                StitchAxis::Horizontal => (y, x),
            };
            let value = if along < 40 || along >= extent - 24 {
                110 + ((cross / 13 + along.min(extent - along) / 3) % 5) as u8 * 12
            } else {
                let page = along - 40 + scroll;
                let row = page / 22;
                let col = cross / 68;
                let x = cross % 68;
                let y = page % 22;
                if x == 0 || y == 0 {
                    190
                } else if (6..53).contains(&x) && (5..16).contains(&y) {
                    let label = if col == 1 {
                        row
                    } else {
                        row % [3, 4, 5, 2][col as usize % 4]
                    };
                    let bits =
                        ((col * 61 + label * 17 + x / 7) * 701 + y / 2).wrapping_mul(0x9e37_79b9);
                    if x % 7 < 5 && (bits >> (x % 7)) & 1 != 0 {
                        48
                    } else {
                        248
                    }
                } else {
                    248
                }
            };
            pixels.push(value);
        }
    }
    Frame::new(width, height, PixelFormat::Gray8, pixels).unwrap()
}

#[test]
fn pixel_verification_requires_a_unique_supported_displacement_in_range() {
    let viewport = 256;
    let make_frame = |shift: u32, period: u32, cross_range: std::ops::Range<u32>| {
        let pixels = (0..viewport)
            .flat_map(|y| {
                let band = y >= 16 && y < viewport - 16;
                let cross_range = cross_range.clone();
                (0..viewport).map(move |x| {
                    let row = if band && cross_range.contains(&x) {
                        (y + shift) % period
                    } else {
                        y
                    };
                    ((row * 31) ^ (x * 7 + row / 3)) as u8
                })
            })
            .collect();
        Frame::new(viewport, viewport, PixelFormat::Gray8, pixels).unwrap()
    };
    let check = |reference, previous, incoming, limit| {
        RegistrationSearch::new(
            reference,
            previous,
            incoming,
            StitchAxis::Vertical,
            limit,
            MotionEstimatorOptions::default().min_confidence,
        )
        .register(&[])
    };
    let first = make_frame(0, 1000, 0..viewport);
    let second = make_frame(71, 1000, 0..viewport);
    let third = make_frame(96, 1000, 0..viewport);
    use registration::Registration::*;
    assert!(matches!(
        check(&first, &second, &third, 96),
        Verified {
            offset: -96,
            error: 0.0,
            ..
        }
    ));
    let out_of_range = check(&first, &second, &third, 95);
    assert!(!matches!(out_of_range, Verified { .. }), "{out_of_range:?}");
    assert!(matches!(
        check(&third, &second, &first, 96),
        Verified {
            offset: 96,
            error: 0.0,
            ..
        }
    ));
    assert!(matches!(check(&first, &first, &first, 120), Unavailable));
    let periodic = make_frame(0, 37, 0..viewport);
    let shifted = make_frame(71, 37, 0..viewport);
    assert!(matches!(
        check(&periodic, &periodic, &shifted, 120),
        Ambiguous(_)
    ));
    let animated = make_frame(71, 1000, 96..160);
    assert!(matches!(check(&first, &first, &animated, 120), Unavailable));
    let mut noisy = second.clone();
    noisy.pixels_mut()[128 * viewport as usize..129 * viewport as usize].fill(255);
    assert!(matches!(
        check(&first, &first, &noisy, 120),
        Verified {
            offset: -71,
            error,
            ..
        } if error>0.0
    ));
}

#[test]
fn repeated_table_cells_stitch_to_the_source_in_both_directions() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        for positions in [[0, 187, 408, 577], [577, 408, 187, 0]] {
            let frames = positions.map(|position| table(axis, 512, position));
            let result = stitch(
                &frames,
                StitchOptions {
                    axis,
                    record_decisions: true,
                    ..Default::default()
                },
            )
            .unwrap();
            for (d, pair) in result.decisions.iter().zip(positions.windows(2)) {
                assert_eq!(
                    d.accepted_offset,
                    Some(pair[0] as i32 - pair[1] as i32),
                    "{axis:?}: {d:?}"
                );
            }
            assert!(result.decisions.iter().any(|d| {
                d.motion_diagnostics.as_ref().unwrap().candidates[0]
                    .overlap_blocks
                    .is_some()
            }));
            assert!(
                result.image == table(axis, 512 + 577, 0),
                "{axis:?} {positions:?}"
            );
        }
    }
}

fn color_view(axis: StitchAxis, position: u32, marker: bool, dirty_line: bool) -> Frame {
    let size = 512;
    let mut pixels = Vec::new();
    for y in 0..size {
        for x in 0..size {
            let (along, cross) = match axis {
                StitchAxis::Vertical => (y, x),
                StitchAxis::Horizontal => (x, y),
            };
            let page = along + position;
            let mut seed = (page % 256).wrapping_mul(0x27d4_eb2d) ^ cross.wrapping_mul(0x85eb_ca6b);
            seed ^= seed >> 15;
            let value = (seed.wrapping_mul(0xc2b2_ae35) >> 24) as u8;
            let rgb = if marker && cross == 0 {
                if page % 509 < 255 {
                    [255, 0, 0]
                } else {
                    [0, 131, 0]
                }
            } else if dirty_line && along == 200 {
                [255 - value; 3]
            } else {
                [value; 3]
            };
            pixels.extend(rgb);
            // Alpha changes everywhere, independently of the motion.
            pixels.push(position as u8);
        }
    }
    Frame::new(size, size, PixelFormat::Rgba8, pixels).unwrap()
}

#[test]
fn periodic_aliases_preserve_the_canvas_and_a_full_color_marker_resolves_them() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        for dirty in [false, true] {
            let first = color_view(axis, 0, false, false);
            let incoming = color_view(axis, 275, false, dirty);
            let result = stitch(
                &[first.clone(), incoming],
                StitchOptions {
                    axis,
                    record_decisions: true,
                    ..Default::default()
                },
            )
            .unwrap();
            let d = &result.decisions[0];
            assert_eq!(
                d.motion,
                Some(crate::MotionOutcome::Indeterminate),
                "{axis:?}, dirty={dirty}: {d:?}"
            );
            assert_eq!(d.growth, 0);
            assert_eq!(result.image, first);
            assert!(
                d.motion_diagnostics
                    .as_ref()
                    .unwrap()
                    .ambiguous_offsets
                    .len()
                    >= 2
            );
        }
        let first = color_view(axis, 0, true, false);
        let incoming = color_view(axis, 275, true, false);
        let mut estimator =
            VerticalMotionEstimator::new_for_axis(first.geometry(), axis, Default::default())
                .unwrap();
        assert_eq!(
            estimator
                .estimate(&first, &first, &incoming)
                .unwrap()
                .offset(),
            Some(-275)
        );
        assert_eq!(
            estimator
                .estimate(&incoming, &incoming, &first)
                .unwrap()
                .offset(),
            Some(275)
        );
    }
}

#[test]
fn excluding_a_repaint_from_a_shorter_overlap_cannot_disambiguate_periodic_motion() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        let first = color_view(axis, 0, false, false);
        let mut incoming = color_view(axis, 275, false, false);
        for along in 200..224 {
            for cross in 129..153 {
                let (x, y) = match axis {
                    StitchAxis::Vertical => (cross, along),
                    StitchAxis::Horizontal => (along, cross),
                };
                let start = (512 * y + x) * 4;
                for channel in &mut incoming.pixels_mut()[start..start + 3] {
                    *channel ^= 255;
                }
            }
        }
        for confidence in [MotionEstimatorOptions::default().min_confidence, 1.0] {
            let options = MotionEstimatorOptions {
                min_confidence: confidence,
                ..Default::default()
            };
            let mut estimator =
                VerticalMotionEstimator::new_for_axis(first.geometry(), axis, options).unwrap();
            for (reference, next) in [(&first, &incoming), (&incoming, &first)] {
                let result = estimator.estimate(reference, reference, next).unwrap();
                assert_eq!(
                    result.outcome,
                    crate::MotionOutcome::Indeterminate,
                    "{result:?}"
                );
                assert!(result.diagnostics.ambiguous_offsets.len() >= 2);
            }
        }
    }
}

#[test]
fn featureless_translation_survives_a_redrawn_line_in_both_directions() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        let make = |position: u32, dirty| {
            let size = 256;
            let mut pixels = Vec::new();
            for y in 0..size {
                for x in 0..size {
                    let along = match axis {
                        StitchAxis::Vertical => y,
                        StitchAxis::Horizontal => x,
                    };
                    let hash = (along + position).wrapping_mul(0x27d4_eb2d);
                    let value = ((hash ^ (hash >> 15)).wrapping_mul(0xc2b2_ae35) >> 24) as u8;
                    pixels.push(if dirty && along == 80 {
                        255 - value
                    } else {
                        value
                    });
                }
            }
            Frame::new(size, size, PixelFormat::Gray8, pixels).unwrap()
        };
        let first = make(0, false);
        let next = make(71, true);
        let mut estimator =
            VerticalMotionEstimator::new_for_axis(first.geometry(), axis, Default::default())
                .unwrap();
        for (reference, incoming, d) in [(&first, &next, -71), (&next, &first, 71)] {
            let estimate = estimator.estimate(reference, reference, incoming).unwrap();
            assert_eq!(estimate.offset(), Some(d), "{estimate:?}");
            assert_eq!(estimate.diagnostics.mutual_matches, 0);
            let best = &estimate.diagnostics.candidates[0];
            assert!(best.overlap_blocks.is_some());
            assert!(
                best.precise_alignment_error
                    .is_some_and(|e| e > 0.0 && e < 1.0)
            );
        }
        let mut strict = VerticalMotionEstimator::new_for_axis(
            first.geometry(),
            axis,
            MotionEstimatorOptions {
                min_confidence: 1.0,
                ..Default::default()
            },
        )
        .unwrap();
        assert_eq!(
            strict.estimate(&first, &first, &next).unwrap().offset(),
            None
        );
        assert_eq!(
            strict
                .estimate(&first, &first, &make(71, false))
                .unwrap()
                .offset(),
            Some(-71)
        );
    }
}

#[test]
fn translating_a_small_widget_preserves_a_stationary_viewport() {
    let axis = StitchAxis::Vertical;
    let first = color_view(axis, 0, true, false);
    let mut incoming = first.clone();
    for y in 192..320 {
        let from = 192 + (y - 192 + 15) % 128;
        let source = (from * 512 + 192) * 4;
        let target = (y * 512 + 192) * 4;
        incoming.pixels_mut()[target..target + 128 * 4]
            .copy_from_slice(&first.pixels()[source..source + 128 * 4]);
    }
    let result = stitch(
        &[first.clone(), incoming],
        StitchOptions {
            axis,
            record_decisions: true,
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(result.decisions[0].accepted_offset, None);
    assert_eq!(result.image, first);
}

#[test]
fn pixel_verification_distinguishes_local_animation_from_narrow_scroll() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        let make = |position: u32, local_animation| {
            let extent = 512;
            let active = if local_animation { 206..306 } else { 25..487 };
            let mut pixels = Vec::with_capacity((extent * extent) as usize);
            for y in 0..extent {
                for x in 0..extent {
                    let (along, cross) = match axis {
                        StitchAxis::Vertical => (y, x),
                        StitchAxis::Horizontal => (x, y),
                    };
                    let value = if active.contains(&along) && (224..288).contains(&cross) {
                        let hash = (along + position).wrapping_mul(0x27d4_eb2d_u32);
                        let stripe = (hash ^ (hash >> 15)).wrapping_mul(0xc2b2_ae35) >> 24;
                        20 + (stripe % 210) as u8
                    } else if local_animation && (18..50).contains(&x) && (20..52).contains(&y) {
                        // A small anchor confirms that the surrounding viewport is stationary.
                        (((y - 20) * 29 + (x - 18) * 37) % 251) as u8
                    } else {
                        248
                    };
                    pixels.push(value);
                }
            }
            Frame::new(extent, extent, PixelFormat::Gray8, pixels).unwrap()
        };
        let options = StitchOptions {
            axis,
            record_decisions: true,
            ..Default::default()
        };

        let first = make(0, true);
        let next = make(15, true);
        for (reference, incoming) in [(&first, &next), (&next, &first)] {
            let result = stitch(&[reference.clone(), incoming.clone()], options).unwrap();
            let decision = &result.decisions[0];
            assert_eq!(decision.accepted_offset, None, "{axis:?}: {decision:?}");
            assert_eq!(decision.growth, 0);
            assert_eq!(result.image, *reference);
        }

        let first = make(0, false);
        let next = make(15, false);
        for (reference, incoming, offset) in [(&first, &next, -15), (&next, &first, 15)] {
            let result = stitch(&[reference.clone(), incoming.clone()], options).unwrap();
            let decision = &result.decisions[0];
            assert_eq!(
                decision.accepted_offset,
                Some(offset),
                "{axis:?}: {decision:?}"
            );
            assert_eq!(decision.growth, 15);
            assert!(
                decision.motion_diagnostics.as_ref().unwrap().candidates[0]
                    .overlap_blocks
                    .is_some()
            );
        }
    }
}

fn striped_motion_frames(axis: StitchAxis, background_shift: u32) -> [Frame; 2] {
    let size = 512_u32;
    let frame = |stripe_shift: u32, background_shift: u32| {
        let pixels = (0..size)
            .flat_map(|y| {
                (0..size).map(move |x| {
                    let (along, cross) = match axis {
                        StitchAxis::Vertical => (y, x),
                        StitchAxis::Horizontal => (x, y),
                    };
                    // Every eighth pixel is the center of a proposal sampling column.
                    let shift = if cross % 8 == 4 {
                        stripe_shift
                    } else {
                        background_shift
                    };
                    let hash =
                        (along + shift).wrapping_mul(0x27d4_eb2d) ^ cross.wrapping_mul(0x85eb_ca6b);
                    ((hash ^ (hash >> 15)).wrapping_mul(0xc2b2_ae35) >> 24) as u8
                })
            })
            .collect();
        Frame::new(size, size, PixelFormat::Gray8, pixels).unwrap()
    };
    [frame(0, 0), frame(71, background_shift)]
}

#[test]
fn sampled_stripes_do_not_extend_a_stationary_canvas() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        let frames = striped_motion_frames(axis, 0);
        let result = stitch(
            &frames,
            StitchOptions {
                axis,
                record_decisions: true,
                ..Default::default()
            },
        )
        .unwrap();
        let decision = &result.decisions[0];
        assert_eq!(decision.accepted_offset, None, "{axis:?}: {decision:?}");
        assert_eq!(result.image, frames[0]);
    }
}

#[test]
fn sampled_stripes_do_not_override_the_viewport_displacement() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        let result = stitch(
            &striped_motion_frames(axis, 83),
            StitchOptions {
                axis,
                record_decisions: true,
                ..Default::default()
            },
        )
        .unwrap();
        let decision = &result.decisions[0];
        assert_eq!(
            decision.accepted_offset,
            Some(-83),
            "{axis:?}: {decision:?}"
        );
        assert_eq!(decision.growth, 83);
    }
}

#[test]
fn repeated_table_cells_keep_their_offsets_across_local_repaints() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        let positions = [0, 187, 408, 577];
        let mut frames = positions.map(|p| table(axis, 512, p));
        for (index, frame) in frames.iter_mut().enumerate().skip(1) {
            let width = frame.width() as usize;
            let line = 80 + index * 7;
            for cross in 0..544 {
                let pixel = match axis {
                    StitchAxis::Vertical => line * width + cross,
                    StitchAxis::Horizontal => cross * width + line,
                };
                frame.pixels_mut()[pixel] ^= 255;
            }
        }
        let result = stitch(
            &frames,
            StitchOptions {
                axis,
                record_decisions: true,
                ..Default::default()
            },
        )
        .unwrap();
        assert_eq!(
            axis.primary_extent(result.image.width(), result.image.height()),
            1089
        );
        for (d, pair) in result.decisions.iter().zip(positions.windows(2)) {
            assert_eq!(
                d.accepted_offset,
                Some(pair[0] as i32 - pair[1] as i32),
                "{axis:?}: {d:?}"
            );
        }
    }
}

#[test]
fn competing_offsets_are_refined_inside_a_narrow_scrolling_pane() {
    // Only the central pane scrolls. Most of its texture repeats every 40 pixels.
    // A narrow column of nonrepeating content distinguishes the neighbouring copies.
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        let make = |position: u32, repaint: bool| {
            let mut pixels = Vec::new();
            for y in 0..320 {
                for x in 0..320 {
                    let (along, cross) = match axis {
                        StitchAxis::Vertical => (y, x),
                        StitchAxis::Horizontal => (x, y),
                    };
                    let page = along + position;
                    let mut value = if (16..304).contains(&along) && (108..212).contains(&cross) {
                        let row = if (139..151).contains(&cross) {
                            page
                        } else {
                            page % 40
                        };
                        let hash = (row * 19 + cross).wrapping_mul(0x9e37_79b9);
                        (hash ^ (hash >> 17)) as u8
                    } else {
                        196
                    };
                    if repaint {
                        value = value.saturating_add(6);
                        if (72..86).contains(&along) && (166..182).contains(&cross) {
                            value ^= 255;
                        }
                    }
                    pixels.push(value);
                }
            }
            Frame::new(320, 320, PixelFormat::Gray8, pixels).unwrap()
        };
        let first = make(0, false);
        let next = make(69, true);
        for (reference, incoming, truth) in [(&first, &next, -69), (&next, &first, 69)] {
            let decision = RegistrationSearch::new(
                reference,
                reference,
                incoming,
                axis,
                150,
                MotionEstimatorOptions::default().min_confidence,
            )
            .register(&[0, truth + 40, truth - 1]);
            assert!(
                matches!(decision, registration::Registration::Verified {offset, ..} if offset == truth),
                "{axis:?}: {decision:?}"
            );
            let mut estimator = VerticalMotionEstimator::new_for_axis(
                reference.geometry(),
                axis,
                Default::default(),
            )
            .unwrap();
            let estimate = estimator.estimate(reference, reference, incoming).unwrap();
            assert_eq!(estimate.offset(), Some(truth), "{axis:?}: {estimate:?}");
        }
    }
}

#[test]
fn synthetic_references_keep_their_offsets_across_exposure_changes_and_repaints() {
    for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
        for positions in [[0, 187, 408, 577], [577, 408, 187, 0]] {
            let mut frames = positions.map(|position| table(axis, 512, position));
            for (index, frame) in frames.iter_mut().enumerate() {
                if index % 2 == 1 {
                    for value in frame.pixels_mut().iter_mut() {
                        *value = value.saturating_add(1);
                    }
                }
                let width = frame.width() as usize;
                for y in 72..96 {
                    let start = y * width + 150 + index * 11;
                    for value in &mut frame.pixels_mut()[start..start + 24] {
                        *value ^= 255;
                    }
                }
            }
            let result = stitch(
                &frames,
                StitchOptions {
                    axis,
                    record_decisions: true,
                    ..Default::default()
                },
            )
            .unwrap();
            for (decision, pair) in result.decisions.iter().zip(positions.windows(2)) {
                assert_eq!(
                    decision.accepted_offset,
                    Some(pair[0] as i32 - pair[1] as i32),
                    "{axis:?}: {decision:?}"
                );
            }
        }
    }
}
