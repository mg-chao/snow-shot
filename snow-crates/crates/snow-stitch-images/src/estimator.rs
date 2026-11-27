use std::{
    collections::BTreeMap,
    sync::{Arc, OnceLock},
};

use rayon::prelude::*;

mod registration;
use registration::{Registration, RegistrationSearch};
#[cfg(test)]
mod table_tests;

use crate::{
    CandidateDiagnostics, Frame, Geometry, MotionDiagnostics, MotionEstimate,
    MotionEstimatorOptions, MotionStage, PixelFormat, StitchAxis, StitchError,
    region::{GrayImage, SimilarityMap, TileLayout},
    sampling::SamplingPlan,
};

const LOWE_RATIO: f32 = 0.8;
const MAX_HAMMING_DISTANCE: f32 = 64.0;
const MAX_CROSS_AXIS_DELTA: i32 = 4;
const INLIER_TOLERANCE: i32 = 2;
const MAX_CANDIDATES: usize = 8;
const MAX_FEATURES_PER_TILE: usize = 8;
const MIN_INLIER_MATCHES: u32 = 8;
const MIN_INLIER_TILES: u32 = 4;
const MIN_RESIDUAL_GAIN: f32 = 0.15;

#[derive(Debug, Clone, Copy, PartialEq)]
struct MatchObservation {
    distance: f32,
    reference_x: f32,
    reference_y: f32,
    incoming_x: f32,
    incoming_y: f32,
    reference_tile: usize,
    dx: i32,
    dy: i32,
}

#[derive(Debug, Clone, Copy, PartialEq)]
struct BinaryFeature {
    x: f32,
    y: f32,
    descriptor: [u8; 32],
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct DescriptorMatch {
    train_index: usize,
    distance: u32,
}

struct FeatureEvidence {
    reference_features: u32,
    incoming_features: u32,
    observations: Vec<MatchObservation>,
}

struct EvaluationImages<'a> {
    reference: &'a Frame,
    previous_raw: &'a Frame,
    incoming: &'a Frame,
    reference_gray: &'a GrayImage,
    incoming_gray: &'a GrayImage,
    direct: &'a SimilarityMap,
    zero_alignment: &'a SimilarityMap,
}

struct EvaluatedMotion {
    estimate: MotionEstimate,
    scene_cut: bool,
    margin: f32,
}

impl EvaluatedMotion {
    fn rejected(scene_cut: bool, confidence: f32, diagnostics: MotionDiagnostics) -> Self {
        Self {
            estimate: MotionEstimate::indeterminate(confidence, diagnostics),
            scene_cut,
            margin: 0.0,
        }
    }

    fn needs_full_resolution(&self) -> bool {
        let Some(best) = self.estimate.diagnostics.candidates.first() else {
            return true;
        };
        let precise = best
            .precise_alignment_error
            .is_some_and(|error| error < 1.0);
        self.estimate.offset().is_none()
            || !(best.overlap_blocks.is_some() || (self.margin > 0.0 && precise))
    }
}

fn round_coordinate(value: f32) -> i32 {
    (value + 0.5).trunc() as i32
}

fn hamming_distance(left: &[u8; 32], right: &[u8; 32]) -> u32 {
    left.chunks_exact(8)
        .zip(right.chunks_exact(8))
        .map(|(left, right)| {
            (u64::from_ne_bytes(left.try_into().expect("eight-byte descriptor word"))
                ^ u64::from_ne_bytes(right.try_into().expect("eight-byte descriptor word")))
            .count_ones()
        })
        .sum()
}

fn nearest_two(query: &BinaryFeature, train: &[BinaryFeature]) -> Option<[DescriptorMatch; 2]> {
    let mut first = DescriptorMatch {
        train_index: usize::MAX,
        distance: u32::MAX,
    };
    let mut second = first;
    for (train_index, candidate) in train.iter().enumerate() {
        let distance = hamming_distance(&query.descriptor, &candidate.descriptor);
        let matched = DescriptorMatch {
            train_index,
            distance,
        };
        if (distance, train_index) < (first.distance, first.train_index) {
            second = first;
            first = matched;
        } else if (distance, train_index) < (second.distance, second.train_index) {
            second = matched;
        }
    }
    (second.train_index != usize::MAX).then_some([first, second])
}

fn match_descriptors_portable(
    queries: &[BinaryFeature],
    train: &[BinaryFeature],
) -> Vec<Option<DescriptorMatch>> {
    queries
        .par_iter()
        .map(|feature| nearest_two(feature, train).and_then(passes_ratio))
        .collect()
}

#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
#[target_feature(enable = "popcnt")]
unsafe fn nearest_two_popcnt(
    query: &BinaryFeature,
    train: &[BinaryFeature],
) -> Option<[DescriptorMatch; 2]> {
    let query_words = query.descriptor.as_ptr().cast::<u64>();
    let query_words = unsafe {
        [
            std::ptr::read_unaligned(query_words),
            std::ptr::read_unaligned(query_words.add(1)),
            std::ptr::read_unaligned(query_words.add(2)),
            std::ptr::read_unaligned(query_words.add(3)),
        ]
    };
    let mut first = DescriptorMatch {
        train_index: usize::MAX,
        distance: u32::MAX,
    };
    let mut second = first;
    for (train_index, candidate) in train.iter().enumerate() {
        let candidate_words = candidate.descriptor.as_ptr().cast::<u64>();
        let candidate_words = unsafe {
            [
                std::ptr::read_unaligned(candidate_words),
                std::ptr::read_unaligned(candidate_words.add(1)),
                std::ptr::read_unaligned(candidate_words.add(2)),
                std::ptr::read_unaligned(candidate_words.add(3)),
            ]
        };
        let distance = query_words
            .into_iter()
            .zip(candidate_words)
            .map(|(query, candidate)| (query ^ candidate).count_ones())
            .sum();
        let matched = DescriptorMatch {
            train_index,
            distance,
        };
        if (distance, train_index) < (first.distance, first.train_index) {
            second = first;
            first = matched;
        } else if (distance, train_index) < (second.distance, second.train_index) {
            second = matched;
        }
    }
    (second.train_index != usize::MAX).then_some([first, second])
}

#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
#[target_feature(enable = "popcnt")]
unsafe fn match_descriptors_popcnt(
    queries: &[BinaryFeature],
    train: &[BinaryFeature],
) -> Vec<Option<DescriptorMatch>> {
    queries
        .par_iter()
        .map(|query| unsafe { nearest_two_popcnt(query, train).and_then(passes_ratio) })
        .collect()
}

#[cfg(target_arch = "x86")]
use std::arch::x86::{
    __m256i, _mm256_add_epi8, _mm256_and_si256, _mm256_loadu_si256, _mm256_sad_epu8,
    _mm256_set1_epi8, _mm256_setr_epi8, _mm256_setzero_si256, _mm256_shuffle_epi8,
    _mm256_srli_epi16, _mm256_storeu_si256, _mm256_xor_si256,
};
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::{
    __m256i, _mm256_add_epi8, _mm256_and_si256, _mm256_loadu_si256, _mm256_sad_epu8,
    _mm256_set1_epi8, _mm256_setr_epi8, _mm256_setzero_si256, _mm256_shuffle_epi8,
    _mm256_srli_epi16, _mm256_storeu_si256, _mm256_xor_si256,
};

#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
#[target_feature(enable = "avx2")]
unsafe fn nearest_two_avx2(
    query: &BinaryFeature,
    train: &[BinaryFeature],
) -> Option<[DescriptorMatch; 2]> {
    let lookup = _mm256_setr_epi8(
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3,
        3, 4,
    );
    let low_nibble = _mm256_set1_epi8(0x0f);
    let zero = _mm256_setzero_si256();
    let query = unsafe { _mm256_loadu_si256(query.descriptor.as_ptr().cast::<__m256i>()) };
    let mut first = DescriptorMatch {
        train_index: usize::MAX,
        distance: u32::MAX,
    };
    let mut second = first;
    for (train_index, candidate) in train.iter().enumerate() {
        let candidate =
            unsafe { _mm256_loadu_si256(candidate.descriptor.as_ptr().cast::<__m256i>()) };
        let difference = _mm256_xor_si256(query, candidate);
        let low = _mm256_and_si256(difference, low_nibble);
        let high = _mm256_and_si256(_mm256_srli_epi16(difference, 4), low_nibble);
        let counts = _mm256_add_epi8(
            _mm256_shuffle_epi8(lookup, low),
            _mm256_shuffle_epi8(lookup, high),
        );
        let sums = _mm256_sad_epu8(counts, zero);
        let mut lanes = [0_u64; 4];
        unsafe { _mm256_storeu_si256(lanes.as_mut_ptr().cast::<__m256i>(), sums) };
        let distance = lanes.into_iter().sum::<u64>() as u32;
        let matched = DescriptorMatch {
            train_index,
            distance,
        };
        if (distance, train_index) < (first.distance, first.train_index) {
            second = first;
            first = matched;
        } else if (distance, train_index) < (second.distance, second.train_index) {
            second = matched;
        }
    }
    (second.train_index != usize::MAX).then_some([first, second])
}

#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
#[target_feature(enable = "avx2")]
unsafe fn match_descriptors_avx2(
    queries: &[BinaryFeature],
    train: &[BinaryFeature],
) -> Vec<Option<DescriptorMatch>> {
    queries
        .par_iter()
        .map(|query| unsafe { nearest_two_avx2(query, train).and_then(passes_ratio) })
        .collect()
}

#[cfg(target_arch = "aarch64")]
#[target_feature(enable = "neon")]
unsafe fn nearest_two_neon(
    query: &BinaryFeature,
    train: &[BinaryFeature],
) -> Option<[DescriptorMatch; 2]> {
    use std::arch::aarch64::*;
    unsafe {
        let query_low = vld1q_u8(query.descriptor.as_ptr());
        let query_high = vld1q_u8(query.descriptor.as_ptr().add(16));
        let mut first = DescriptorMatch {
            train_index: usize::MAX,
            distance: u32::MAX,
        };
        let mut second = first;
        let counts = |candidate: &BinaryFeature| {
            let low = vcntq_u8(veorq_u8(query_low, vld1q_u8(candidate.descriptor.as_ptr())));
            let high = vcntq_u8(veorq_u8(
                query_high,
                vld1q_u8(candidate.descriptor.as_ptr().add(16)),
            ));
            vaddq_u8(low, high)
        };
        let retain =
            |first: &mut DescriptorMatch, second: &mut DescriptorMatch, train_index, distance| {
                // Candidates arrive in index order. Earlier entries win equal distances.
                let matched = DescriptorMatch {
                    train_index,
                    distance,
                };
                if distance < first.distance {
                    *second = *first;
                    *first = matched;
                } else if distance < second.distance {
                    *second = matched;
                }
            };
        let mut blocks = train.chunks_exact(4);
        for (index, block) in blocks.by_ref().enumerate() {
            let a = vpaddq_u8(counts(&block[0]), counts(&block[1]));
            let b = vpaddq_u8(counts(&block[2]), counts(&block[3]));
            // Each lane sums all 256 descriptor bits. Widen before the final reductions.
            let distances = vpaddlq_u16(vpaddlq_u8(vpaddq_u8(a, b)));
            if vminvq_u32(distances) < second.distance {
                let mut lanes = [0; 4];
                vst1q_u32(lanes.as_mut_ptr(), distances);
                for (lane, distance) in lanes.into_iter().enumerate() {
                    retain(&mut first, &mut second, index * 4 + lane, distance);
                }
            }
        }
        let remainder = blocks.remainder();
        for (lane, candidate) in remainder.iter().enumerate() {
            retain(
                &mut first,
                &mut second,
                train.len() - remainder.len() + lane,
                u32::from(vaddlvq_u8(counts(candidate))),
            );
        }
        #[cfg(test)]
        NEON_MATCHES.fetch_add(train.len(), std::sync::atomic::Ordering::Relaxed);
        (second.train_index != usize::MAX).then_some([first, second])
    }
}

#[cfg(all(test, target_arch = "aarch64"))]
static NEON_MATCHES: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);

#[cfg(target_arch = "aarch64")]
unsafe fn match_descriptors_neon(
    queries: &[BinaryFeature],
    train: &[BinaryFeature],
) -> Vec<Option<DescriptorMatch>> {
    queries
        .par_iter()
        .map(|query| unsafe { nearest_two_neon(query, train).and_then(passes_ratio) })
        .collect()
}

fn match_descriptors(
    queries: &[BinaryFeature],
    train: &[BinaryFeature],
) -> Vec<Option<DescriptorMatch>> {
    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    if std::arch::is_x86_feature_detected!("avx2") {
        return unsafe { match_descriptors_avx2(queries, train) };
    }
    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    if std::arch::is_x86_feature_detected!("popcnt") {
        return unsafe { match_descriptors_popcnt(queries, train) };
    }
    #[cfg(target_arch = "aarch64")]
    if std::arch::is_aarch64_feature_detected!("neon") {
        return unsafe { match_descriptors_neon(queries, train) };
    }
    match_descriptors_portable(queries, train)
}

fn beats_lowe_ratio(distance: u32, alternative: u32) -> bool {
    (distance as f32) < LOWE_RATIO * alternative as f32
}

fn passes_ratio(matches: [DescriptorMatch; 2]) -> Option<DescriptorMatch> {
    let first = matches[0];
    let second = matches[1];
    (first.distance as f32 <= MAX_HAMMING_DISTANCE
        && beats_lowe_ratio(first.distance, second.distance))
    .then_some(first)
}

fn prune_reverse_candidates(forward: &mut [Option<DescriptorMatch>], incoming_count: usize) {
    // The reverse winner must beat even the other references that chose the same
    // incoming feature. Reuse those distances to leave at most one full scan per feature.
    let mut nearest = vec![[u32::MAX; 2]; incoming_count];
    for matched in forward.iter().flatten() {
        let [best, second] = &mut nearest[matched.train_index];
        if matched.distance < *best {
            *second = *best;
            *best = matched.distance;
        } else {
            *second = (*second).min(matched.distance);
        }
    }
    for matched in forward {
        if matched.is_some_and(|candidate| {
            let [best, second] = nearest[candidate.train_index];
            candidate.distance != best || !beats_lowe_ratio(best, second)
        }) {
            *matched = None;
        }
    }
}

fn match_with_exact_lookup(
    reference: &[BinaryFeature],
    incoming: &[BinaryFeature],
) -> Vec<Option<DescriptorMatch>> {
    if incoming.len() < 32 || reference.len() < 32 {
        return match_descriptors(reference, incoming);
    }
    // A zero-distance descriptor wins the ratio test exactly when it is unique.
    // Keep all incoming rivals for the remaining queries' nearest-two search.
    let mut exact = std::collections::HashMap::with_capacity(incoming.len());
    for (i, feature) in incoming.iter().enumerate() {
        exact
            .entry(&feature.descriptor)
            .and_modify(|index| *index = None)
            .or_insert(Some(i));
    }
    let mut forward = vec![None; reference.len()];
    let mut unresolved = Vec::with_capacity(reference.len());
    let mut indices = Vec::with_capacity(reference.len());
    for (i, feature) in reference.iter().enumerate() {
        match exact.get(&feature.descriptor) {
            Some(index) => {
                forward[i] = index.map(|train_index| DescriptorMatch {
                    train_index,
                    distance: 0,
                })
            }
            None => {
                unresolved.push(*feature);
                indices.push(i);
            }
        }
    }
    for (i, matched) in indices
        .into_iter()
        .zip(match_descriptors(&unresolved, incoming))
    {
        forward[i] = matched;
    }
    forward
}

fn mutual_observations_rust(
    reference: &[BinaryFeature],
    incoming: &[BinaryFeature],
    layout: TileLayout,
    axis: StitchAxis,
) -> Vec<MatchObservation> {
    if reference.len() < 2 || incoming.len() < 2 {
        return Vec::new();
    }
    let mut forward = match_with_exact_lookup(reference, incoming);
    prune_reverse_candidates(&mut forward, incoming.len());
    forward
        .into_par_iter()
        .enumerate()
        .filter_map(|(reference_index, matched)| {
            let matched = matched?;
            let feature = &reference[reference_index];
            let incoming_feature = &incoming[matched.train_index];
            let dx = round_coordinate(incoming_feature.x) - round_coordinate(feature.x);
            let dy = round_coordinate(incoming_feature.y) - round_coordinate(feature.y);
            if axis.cross_delta(dx, dy).abs() > MAX_CROSS_AXIS_DELTA {
                return None;
            }
            let reference_tile = layout.index(feature.x, feature.y)?;
            // References that did not choose this incoming feature can still rule it out.
            // Identical references choose the same unique incoming descriptor and
            // were already removed as ties by prune_reverse_candidates.
            if matched.distance != 0
                && reference.iter().enumerate().any(|(index, other)| {
                    index != reference_index
                        && !beats_lowe_ratio(
                            matched.distance,
                            hamming_distance(&incoming_feature.descriptor, &other.descriptor),
                        )
                })
            {
                return None;
            }
            Some(MatchObservation {
                distance: matched.distance as f32,
                reference_x: feature.x,
                reference_y: feature.y,
                incoming_x: incoming_feature.x,
                incoming_y: incoming_feature.y,
                reference_tile,
                dx,
                dy,
            })
        })
        .collect()
}

fn detect_balanced_rust(
    frame: &Frame,
    layout: TileLayout,
    max_features: usize,
    pyramid_plan: &crate::orb::PyramidPlan,
    sampling: Option<&SamplingPlan>,
) -> Vec<BinaryFeature> {
    let candidate_limit = max_features.saturating_mul(2).max(max_features);
    let image = if let Some(plan) = sampling {
        plan.apply_frame(frame)
    } else {
        crate::orb::grayscale(frame)
    };
    let detection = crate::orb::detect(image, candidate_limit, pyramid_plan);
    let mut ranked = detection
        .keypoints()
        .iter()
        .copied()
        .filter_map(|keypoint| {
            let (x, y) = sampling.map_or((keypoint.x, keypoint.y), |plan| {
                plan.source_coordinates(keypoint.x, keypoint.y)
            });
            let tile = layout.index(x, y)?;
            let score = keypoint.response.max(0.001);
            Some((score, tile, keypoint))
        })
        .collect::<Vec<_>>();
    ranked.sort_by(|left, right| {
        right
            .0
            .total_cmp(&left.0)
            .then_with(|| left.2.x.total_cmp(&right.2.x))
            .then_with(|| left.2.y.total_cmp(&right.2.y))
            .then_with(|| left.2.octave.cmp(&right.2.octave))
            .then_with(|| left.2.response.total_cmp(&right.2.response))
            .then_with(|| left.2.angle.total_cmp(&right.2.angle))
    });

    let mut tile_counts = vec![0_usize; layout.len()];
    let mut selected = Vec::new();
    for (_, tile, keypoint) in ranked {
        if selected.len() >= max_features {
            break;
        }
        if tile_counts[tile] >= MAX_FEATURES_PER_TILE {
            continue;
        }
        tile_counts[tile] += 1;
        selected.push(keypoint);
    }
    detection
        .compute(selected)
        .into_iter()
        .map(|feature| {
            let (x, y) = sampling.map_or((feature.keypoint.x, feature.keypoint.y), |plan| {
                plan.source_coordinates(feature.keypoint.x, feature.keypoint.y)
            });
            BinaryFeature {
                x,
                y,
                descriptor: feature.descriptor,
            }
        })
        .collect()
}

// The estimator hands this its frames, tiling, and feature-extraction tuning;
// grouping them into a struct would churn every call site for no clarity.
#[allow(clippy::too_many_arguments)]
fn pure_rust_feature_evidence(
    reference: &Frame,
    incoming: &Frame,
    layout: TileLayout,
    max_features: usize,
    axis: StitchAxis,
    parallel_work: bool,
    pyramid_plan: &crate::orb::PyramidPlan,
    sampling: Option<&SamplingPlan>,
    cached_reference: &mut Option<Vec<BinaryFeature>>,
) -> (FeatureEvidence, Vec<BinaryFeature>) {
    let feature_perf = crate::perf::Scope::new(crate::perf::Stage::FeatureExtraction);
    let detect = |frame| detect_balanced_rust(frame, layout, max_features, pyramid_plan, sampling);
    let incoming_features = if cached_reference.is_some() {
        detect(incoming)
    } else {
        let (reference_features, incoming_features) = if parallel_work {
            rayon::join(|| detect(reference), || detect(incoming))
        } else {
            (detect(reference), detect(incoming))
        };
        *cached_reference = Some(reference_features);
        incoming_features
    };
    let reference_features = cached_reference
        .as_ref()
        .expect("prepared reference features");
    feature_perf.finish();
    let _perf = crate::perf::Scope::new(crate::perf::Stage::DescriptorMatching);
    let observations =
        mutual_observations_rust(reference_features, &incoming_features, layout, axis);

    let evidence = FeatureEvidence {
        reference_features: reference_features.len() as u32,
        incoming_features: incoming_features.len() as u32,
        observations,
    };
    (evidence, incoming_features)
}

fn candidate_offsets(
    observations: &[MatchObservation],
    axis: StitchAxis,
    maximum_shift: i32,
) -> Vec<i32> {
    let mut exact = BTreeMap::<i32, f32>::new();
    for observation in observations {
        let primary = axis.primary_delta(observation.dx, observation.dy);
        let offset = if primary.abs() < 2 { 0 } else { primary };
        if offset.abs() <= maximum_shift {
            *exact.entry(offset).or_default() +=
                (1.0 - observation.distance / MAX_HAMMING_DISTANCE).clamp(0.05, 1.0);
        }
    }

    let mut modes = exact
        .keys()
        .copied()
        .map(|candidate| {
            let support = exact
                .range(candidate - INLIER_TOLERANCE..=candidate + INLIER_TOLERANCE)
                .map(|(_, support)| support)
                .sum::<f32>();
            (candidate, support)
        })
        .collect::<Vec<_>>();
    modes.sort_by(|left, right| {
        right
            .1
            .total_cmp(&left.1)
            .then_with(|| left.0.abs().cmp(&right.0.abs()))
            .then_with(|| left.0.cmp(&right.0))
    });

    let mut selected = Vec::with_capacity(MAX_CANDIDATES);
    selected.push(0);
    for (candidate, _) in modes {
        if selected.len() >= MAX_CANDIDATES {
            break;
        }
        if selected
            .iter()
            .all(|selected| (selected - candidate).abs() > INLIER_TOLERANCE)
        {
            selected.push(candidate);
        }
    }
    selected
}

fn average_similarity(map: &SimilarityMap, observation: &MatchObservation) -> f32 {
    let reference = map
        .at_point(observation.reference_x, observation.reference_y)
        .unwrap_or(0.5);
    let incoming = map
        .at_point(observation.incoming_x, observation.incoming_y)
        .unwrap_or(reference);
    0.5 * (reference + incoming)
}

fn alignment_errors(
    offset: i32,
    direct: &SimilarityMap,
    zero_alignment: &SimilarityMap,
    compensated: &SimilarityMap,
) -> (f32, f32) {
    let mut samples = Vec::new();
    for tile in 0..direct.layout().len() {
        let (Some(direct_similarity), Some(zero_similarity), Some(candidate_similarity)) = (
            direct.at_tile(tile),
            zero_alignment.at_tile(tile),
            compensated.at_tile(tile),
        ) else {
            continue;
        };
        let pair_evidence = if offset == 0 {
            direct_similarity
        } else {
            1.0 - direct_similarity
        };
        let texture = direct.texture_at(tile).max(compensated.texture_at(tile));
        if texture < 0.05 {
            continue;
        }
        let weight = (0.1 + 0.9 * pair_evidence) * (0.1 + 0.9 * texture);
        samples.push((1.0 - candidate_similarity, 1.0 - zero_similarity, weight));
    }
    if samples.is_empty() {
        return (1.0, 1.0);
    }
    samples.sort_by(|left, right| left.0.total_cmp(&right.0));
    let retained_weight = samples.iter().map(|sample| sample.2).sum::<f32>() * 0.75;
    let mut used_weight = 0.0;
    let mut zero_error = 0.0;
    let mut candidate_error = 0.0;
    for (candidate_sample, zero_sample, weight) in samples {
        let used = weight.min((retained_weight - used_weight).max(0.0));
        if used <= 0.0 {
            break;
        }
        candidate_error += candidate_sample * used;
        zero_error += zero_sample * used;
        used_weight += used;
    }
    if used_weight > 0.0 {
        (zero_error / used_weight, candidate_error / used_weight)
    } else {
        (1.0, 1.0)
    }
}

fn score_candidate(
    offset: i32,
    axis: StitchAxis,
    observations: &[MatchObservation],
    direct: &SimilarityMap,
    zero_alignment: &SimilarityMap,
    compensated: &SimilarityMap,
) -> CandidateDiagnostics {
    let mut eligible_by_tile = vec![0.0_f32; direct.layout().len()];
    let mut inlier_by_tile = vec![0.0_f32; direct.layout().len()];
    let mut raw_inliers = 0_u32;

    for observation in observations {
        debug_assert!(
            axis.cross_delta(observation.dx, observation.dy).abs() <= MAX_CROSS_AXIS_DELTA
        );
        let direct_similarity = average_similarity(direct, observation);
        let pair_evidence = if offset == 0 {
            direct_similarity
        } else {
            1.0 - direct_similarity
        };
        let descriptor_quality =
            (1.0 - observation.distance / MAX_HAMMING_DISTANCE).clamp(0.05, 1.0);
        let weight = descriptor_quality * (0.1 + 0.9 * pair_evidence);
        eligible_by_tile[observation.reference_tile] += weight;
        if (axis.primary_delta(observation.dx, observation.dy) - offset).abs() <= INLIER_TOLERANCE {
            inlier_by_tile[observation.reference_tile] += weight;
            raw_inliers = raw_inliers.saturating_add(1);
        }
    }

    let eligible_support = eligible_by_tile
        .iter()
        .map(|weight| weight.min(1.0))
        .sum::<f32>();
    let weighted_support = inlier_by_tile
        .iter()
        .map(|weight| weight.min(1.0))
        .sum::<f32>();
    let weighted_inlier_share = if eligible_support > 0.0 {
        (weighted_support / eligible_support).clamp(0.0, 1.0)
    } else {
        0.0
    };
    let inlier_tiles = inlier_by_tile
        .iter()
        .filter(|weight| **weight >= 0.05)
        .count() as u32;
    let spatial_coverage = (inlier_tiles as f32 / 12.0).clamp(0.0, 1.0);
    let (zero_error, alignment_error) =
        alignment_errors(offset, direct, zero_alignment, compensated);
    let residual_gain = if offset == 0 || zero_error <= f32::EPSILON {
        0.0
    } else {
        ((zero_error - alignment_error) / zero_error).clamp(0.0, 1.0)
    };
    let score = 0.6 * weighted_inlier_share + 0.25 * residual_gain + 0.15 * spatial_coverage;

    CandidateDiagnostics {
        offset,
        raw_inliers,
        inlier_tiles,
        weighted_support,
        weighted_inlier_share,
        spatial_coverage,
        alignment_error,
        precise_alignment_error: None,
        overlap_blocks: None,
        residual_gain,
        score,
    }
}

fn has_motion_support(candidate: &CandidateDiagnostics) -> bool {
    candidate.raw_inliers >= MIN_INLIER_MATCHES
        && candidate.inlier_tiles >= MIN_INLIER_TILES
        && candidate.residual_gain >= MIN_RESIDUAL_GAIN
}

fn luminance_at(frame: &Frame, x: u32, y: u32) -> u8 {
    let channels = frame.pixel_format().channels() as usize;
    let offset = ((y as usize) * (frame.width() as usize) + x as usize) * channels;
    match frame.pixel_format() {
        PixelFormat::Gray8 => frame.pixels()[offset],
        PixelFormat::Rgb8 | PixelFormat::Rgba8 => {
            let red = u32::from(frame.pixels()[offset]);
            let green = u32::from(frame.pixels()[offset + 1]);
            let blue = u32::from(frame.pixels()[offset + 2]);
            ((77 * red + 150 * green + 29 * blue + 128) >> 8) as u8
        }
    }
}

fn luminance_gradient(frame: &Frame, x: u32, y: u32) -> u16 {
    let horizontal = luminance_at(frame, x + 1, y).abs_diff(luminance_at(frame, x - 1, y));
    let vertical = luminance_at(frame, x, y + 1).abs_diff(luminance_at(frame, x, y - 1));
    u16::from(horizontal) + u16::from(vertical)
}

const REFINEMENT_OFFSETS: usize = (2 * INLIER_TOLERANCE + 1) as usize;
const REFINEMENT_PATCH_RADIUS: i32 = 4;
const MAX_REFINEMENT_PATCHES_PER_TILE: usize = 2;

fn refinement_tile_errors(
    reference: &Frame,
    incoming: &Frame,
    axis: StitchAxis,
    offsets: &[i32],
    observations: &[&MatchObservation],
) -> Option<[f32; REFINEMENT_OFFSETS]> {
    let width = incoming.width() as i32;
    let height = incoming.height() as i32;
    let first = *offsets.first()?;
    let last = *offsets.last()?;
    let mut sums = [0_u64; REFINEMENT_OFFSETS];
    let mut samples = 0_u32;
    for observation in observations.iter().take(MAX_REFINEMENT_PATCHES_PER_TILE) {
        let x = round_coordinate(observation.incoming_x);
        let y = round_coordinate(observation.incoming_y);
        let (left, top, right, bottom) = match axis {
            StitchAxis::Vertical => (x, y - last, x, y - first),
            StitchAxis::Horizontal => (x - last, y, x - first, y),
        };
        let border = REFINEMENT_PATCH_RADIUS + 1;
        // Every candidate must use the same full patch, including gradient neighbors.
        if x < border
            || y < border
            || x >= width - border
            || y >= height - border
            || left < border
            || top < border
            || right >= width - border
            || bottom >= height - border
        {
            continue;
        }
        for dy in (-REFINEMENT_PATCH_RADIUS..=REFINEMENT_PATCH_RADIUS).step_by(2) {
            for dx in (-REFINEMENT_PATCH_RADIUS..=REFINEMENT_PATCH_RADIUS).step_by(2) {
                let x = (x + dx) as u32;
                let y = (y + dy) as u32;
                let incoming_gradient = luminance_gradient(incoming, x, y);
                // Texture eligibility must not change with the candidate offset.
                if incoming_gradient < 16 {
                    continue;
                }
                let incoming_value = luminance_at(incoming, x, y);
                for (index, &offset) in offsets.iter().enumerate() {
                    let (rx, ry) = match axis {
                        StitchAxis::Vertical => (x, (y as i32 - offset) as u32),
                        StitchAxis::Horizontal => ((x as i32 - offset) as u32, y),
                    };
                    let value = luminance_at(reference, rx, ry);
                    let gradient = luminance_gradient(reference, rx, ry);
                    let error = u16::from(value.abs_diff(incoming_value))
                        + gradient.abs_diff(incoming_gradient) / 2;
                    sums[index] += u64::from(error.min(255));
                }
                samples += 1;
            }
        }
    }
    // A tile needs multiple textured points, not a single accidental agreement.
    (samples >= 4).then(|| sums.map(|sum| sum as f32 / (samples as f32 * 255.0)))
}

fn refine_motion_offset(
    reference: &Frame,
    incoming: &Frame,
    axis: StitchAxis,
    strongest_mode: i32,
    maximum_shift: i32,
    observations: &[MatchObservation],
) -> (i32, f32) {
    let offsets = (strongest_mode - INLIER_TOLERANCE..=strongest_mode + INLIER_TOLERANCE)
        .filter(|offset| offset.abs() <= maximum_shift)
        .collect::<Vec<_>>();
    let mut inliers = observations
        .iter()
        .filter(|observation| {
            (axis.primary_delta(observation.dx, observation.dy) - strongest_mode).abs()
                <= INLIER_TOLERANCE
        })
        .collect::<Vec<_>>();
    // Preserve the motion hypothesis's spatial support. Fixed UI with zero motion
    // must not vote on the precision of a scrolling offset. Choose the best two
    // descriptors per tile, so a dense patch cannot outweigh the rest of the image.
    inliers.sort_by(|left, right| {
        left.reference_tile
            .cmp(&right.reference_tile)
            .then_with(|| left.distance.total_cmp(&right.distance))
    });
    let groups = inliers
        .chunk_by(|left, right| left.reference_tile == right.reference_tile)
        .collect::<Vec<_>>();
    let tile_errors = groups
        .par_iter()
        .with_min_len(8)
        .filter_map(|group| refinement_tile_errors(reference, incoming, axis, &offsets, group))
        .collect::<Vec<_>>();
    if tile_errors.len() < MIN_INLIER_TILES as usize {
        // Preserve the coarse estimate when there is no spatially distributed
        // precision evidence. 1.0 also requests the existing full-resolution retry.
        return (strongest_mode, 1.0);
    }
    let retained = (tile_errors.len() * 3).div_ceil(4);
    offsets
        .into_iter()
        .enumerate()
        .map(|(index, offset)| {
            let mut errors = tile_errors
                .iter()
                .map(|errors| errors[index])
                .collect::<Vec<_>>();
            errors.sort_by(f32::total_cmp);
            // As in coarse alignment, trim the worst quarter to tolerate dynamic
            // patches; normalize each tile first to retain equal spatial influence.
            let error = errors[..retained].iter().sum::<f32>() / retained as f32;
            (offset, error)
        })
        .min_by(|left, right| {
            left.1
                .total_cmp(&right.1)
                .then_with(|| {
                    (left.0 - strongest_mode)
                        .abs()
                        .cmp(&(right.0 - strongest_mode).abs())
                })
                .then_with(|| left.0.cmp(&right.0))
        })
        .unwrap_or((strongest_mode, 1.0))
}

fn options_error(message: impl Into<String>) -> StitchError {
    StitchError::InvalidOptions {
        message: message.into(),
    }
}

pub(crate) fn validate_estimator_options(
    options: MotionEstimatorOptions,
) -> Result<(), StitchError> {
    if !(8..=256).contains(&options.tile_size) {
        return Err(options_error("tile_size must be in [8, 256]"));
    }
    if !(64..=20_000).contains(&options.max_features) {
        return Err(options_error("max_features must be in [64, 20000]"));
    }
    if !options.max_motion_ratio.is_finite() || !(0.0..=0.9).contains(&options.max_motion_ratio) {
        return Err(options_error(
            "max_motion_ratio must be finite and in [0, 0.9]",
        ));
    }
    if !options.min_confidence.is_finite() || !(0.0..=1.0).contains(&options.min_confidence) {
        return Err(options_error("min_confidence must be finite and in [0, 1]"));
    }
    Ok(())
}

/// Prepared data belonging to one stitch accumulator. A duplicate leaves it intact;
/// every estimated frame replaces `previous_raw`, and accepted motion invalidates
/// the reference before its pixels or canvas-window position can change.
/// Only selected descriptors and grayscale planes downsampled 4x on each axis
/// are retained, not ORB pyramids. Equal references share their grayscale planes.
#[derive(Default)]
pub(crate) struct EstimationCache {
    previous_raw: Option<Arc<GrayImage>>,
    previous_features: Option<Vec<BinaryFeature>>,
    previous_full_features: Option<Vec<BinaryFeature>>,
    reference_gray: Option<Arc<GrayImage>>,
    reference_features: Option<Vec<BinaryFeature>>,
    full_reference_features: Option<Vec<BinaryFeature>>,
}

impl EstimationCache {
    pub(crate) fn invalidate_reference(&mut self) {
        self.reference_gray = None;
        self.reference_features = None;
        self.full_reference_features = None;
    }
}

pub struct VerticalMotionEstimator {
    geometry: Geometry,
    axis: StitchAxis,
    options: MotionEstimatorOptions,
    layout: TileLayout,
    scene_cut_streak: u8,
    parallel_work: bool,
    pyramid_plan: crate::orb::PyramidPlan,
    sampling: SamplingPlan,
    full_pyramid_plan: OnceLock<crate::orb::PyramidPlan>,
}

impl std::fmt::Debug for VerticalMotionEstimator {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("VerticalMotionEstimator")
            .field("geometry", &self.geometry)
            .field("axis", &self.axis)
            .field("options", &self.options)
            .field("scene_cut_streak", &self.scene_cut_streak)
            .field("parallel_work", &self.parallel_work)
            .finish()
    }
}

impl VerticalMotionEstimator {
    pub fn new(geometry: Geometry, options: MotionEstimatorOptions) -> Result<Self, StitchError> {
        Self::new_for_axis(geometry, StitchAxis::Vertical, options)
    }

    pub fn new_for_axis(
        geometry: Geometry,
        axis: StitchAxis,
        options: MotionEstimatorOptions,
    ) -> Result<Self, StitchError> {
        validate_estimator_options(options)?;
        let layout = TileLayout::new(geometry.width, geometry.height, options.tile_size);
        let sampling = SamplingPlan::new(geometry, axis);
        let (width, height) = sampling.dimensions(geometry);
        let pyramid_plan = crate::orb::PyramidPlan::new(width, height);
        Ok(Self {
            geometry,
            axis,
            options,
            layout,
            scene_cut_streak: 0,
            parallel_work: std::thread::available_parallelism()
                .is_ok_and(|parallelism| parallelism.get() > 1),
            pyramid_plan,
            sampling,
            full_pyramid_plan: OnceLock::new(),
        })
    }

    fn rejected_estimate(
        &mut self,
        scene_cut: bool,
        confidence: f32,
        mut diagnostics: MotionDiagnostics,
    ) -> MotionEstimate {
        if scene_cut {
            self.scene_cut_streak = self.scene_cut_streak.saturating_add(1);
        } else {
            self.scene_cut_streak = 0;
        }
        if self.scene_cut_streak >= 3 {
            self.scene_cut_streak = 0;
            diagnostics.stage = MotionStage::SceneCut;
        }
        MotionEstimate::indeterminate(confidence, diagnostics)
    }

    pub fn estimate(
        &mut self,
        motion_reference: &Frame,
        previous_raw: &Frame,
        incoming: &Frame,
    ) -> Result<MotionEstimate, StitchError> {
        // Direct callers may supply unrelated frames on every call. Only the
        // stitch accumulator has the lifecycle needed to reuse prepared data.
        self.estimate_cached(
            motion_reference,
            previous_raw,
            incoming,
            &mut EstimationCache::default(),
        )
    }

    pub(crate) fn estimate_cached(
        &mut self,
        motion_reference: &Frame,
        previous_raw: &Frame,
        incoming: &Frame,
        cache: &mut EstimationCache,
    ) -> Result<MotionEstimate, StitchError> {
        // Taking this before early returns also invalidates it when the incoming
        // frame takes the identical-interior path without preparing grayscale.
        let previous_gray = cache.previous_raw.take();
        let previous_features = cache.previous_features.take();
        let previous_full_features = cache.previous_full_features.take();
        for (name, frame) in [
            ("motion reference", motion_reference),
            ("previous raw frame", previous_raw),
            ("incoming frame", incoming),
        ] {
            if frame.geometry() != self.geometry {
                return Err(StitchError::InvalidFrame {
                    message: format!(
                        "{name} has geometry {}; estimator expects {}",
                        frame.geometry(),
                        self.geometry
                    ),
                });
            }
        }
        if self.geometry.width < 5 || self.geometry.height < 5 {
            return Ok(MotionEstimate::no_motion(
                1.0,
                MotionDiagnostics::at(MotionStage::InputTooSmall),
            ));
        }

        if motion_reference.visible_interior_pixels_equal(incoming) {
            self.scene_cut_streak = 0;
            let mut diagnostics = MotionDiagnostics::at(MotionStage::IdenticalInterior);
            diagnostics.direct_similarity = 1.0;
            return Ok(MotionEstimate::no_motion(1.0, diagnostics));
        }

        // Composition may reconstruct precisely the last input. In that case
        // its prepared data is also valid for the new reference. Verify all RGB
        // pixels: fixed UI, animation, and repainted bands can break this equality.
        if cache.reference_gray.is_none()
            && let Some(gray) = &previous_gray
            && motion_reference.visible_pixels_equal(previous_raw)
        {
            cache.reference_gray = Some(Arc::clone(gray));
            cache.reference_features = previous_features;
            cache.full_reference_features = previous_full_features;
        }

        let prepare_maps = || -> Result<_, StitchError> {
            let (previous_gray, incoming_gray) = {
                let _perf = crate::perf::Scope::new(crate::perf::Stage::Grayscale);
                (
                    match previous_gray {
                        Some(gray) => gray,
                        None => Arc::new(GrayImage::from_frame(previous_raw)?),
                    },
                    GrayImage::from_frame(incoming)?,
                )
            };
            let direct = {
                let _perf = crate::perf::Scope::new(crate::perf::Stage::SimilarityMaps);
                SimilarityMap::between(&previous_gray, &incoming_gray, self.layout, self.axis, 0)
            };
            // Release the previous raw planes once the direct map is complete.
            let same_reference = cache
                .reference_gray
                .as_ref()
                .is_some_and(|gray| Arc::ptr_eq(gray, &previous_gray));
            drop(previous_gray);
            if cache.reference_gray.is_none() {
                let _perf = crate::perf::Scope::new(crate::perf::Stage::Grayscale);
                cache.reference_gray = Some(Arc::new(GrayImage::from_frame(motion_reference)?));
            }
            let reference_gray = cache
                .reference_gray
                .as_ref()
                .expect("prepared reference grayscale");
            // Both maps compare with the same incoming planes at offset zero.
            let separate_zero = if same_reference {
                None
            } else {
                let _perf = crate::perf::Scope::new(crate::perf::Stage::SimilarityMaps);
                Some(SimilarityMap::between(
                    reference_gray,
                    &incoming_gray,
                    self.layout,
                    self.axis,
                    0,
                ))
            };
            Ok((incoming_gray, direct, separate_zero))
        };
        let max_features = self.options.max_features as usize;
        let mut detect = || {
            pure_rust_feature_evidence(
                motion_reference,
                incoming,
                self.layout,
                max_features,
                self.axis,
                self.parallel_work,
                &self.pyramid_plan,
                Some(&self.sampling),
                &mut cache.reference_features,
            )
        };
        let (maps, (evidence, incoming_features)) = if self.parallel_work && self.sampling.reduced()
        {
            crate::perf::join(prepare_maps, detect)
        } else {
            (prepare_maps(), detect())
        };
        let (incoming_gray, direct, separate_zero) = maps?;
        let zero_alignment = separate_zero.as_ref().unwrap_or(&direct);
        let reference_gray = cache
            .reference_gray
            .as_ref()
            .expect("prepared reference grayscale");
        cache.previous_features = Some(incoming_features);
        let primary_extent = self
            .axis
            .primary_extent(self.geometry.width, self.geometry.height);
        let maximum_shift = (primary_extent as f32 * self.options.max_motion_ratio).floor() as i32;
        let images = EvaluationImages {
            reference: motion_reference,
            previous_raw,
            incoming,
            reference_gray,
            incoming_gray: &incoming_gray,
            direct: &direct,
            zero_alignment,
        };
        let mut search = None;
        let mut evaluated = self.evaluate(&images, evidence, maximum_shift, &mut search);
        if self.sampling.reduced() && evaluated.needs_full_resolution() {
            let pyramid = self.full_pyramid_plan.get_or_init(|| {
                crate::orb::PyramidPlan::new(
                    self.geometry.width as usize,
                    self.geometry.height as usize,
                )
            });
            let (evidence, incoming_features) = pure_rust_feature_evidence(
                motion_reference,
                incoming,
                self.layout,
                max_features,
                self.axis,
                self.parallel_work,
                pyramid,
                None,
                &mut cache.full_reference_features,
            );
            cache.previous_full_features = Some(incoming_features);
            evaluated = self.evaluate(&images, evidence, maximum_shift, &mut search);
        }
        cache.previous_raw = Some(Arc::new(incoming_gray));
        Ok(self.commit_evaluation(evaluated))
    }

    fn commit_evaluation(&mut self, evaluated: EvaluatedMotion) -> MotionEstimate {
        let EvaluatedMotion {
            estimate,
            scene_cut,
            ..
        } = evaluated;
        if matches!(estimate.outcome, crate::MotionOutcome::Indeterminate) {
            self.rejected_estimate(scene_cut, estimate.confidence, estimate.diagnostics)
        } else {
            self.scene_cut_streak = 0;
            estimate
        }
    }

    fn evaluate<'a>(
        &self,
        images: &EvaluationImages<'a>,
        evidence: FeatureEvidence,
        maximum_shift: i32,
        search: &mut Option<RegistrationSearch<'a>>,
    ) -> EvaluatedMotion {
        let EvaluationImages {
            reference: motion_reference,
            previous_raw,
            incoming,
            reference_gray,
            incoming_gray,
            direct,
            zero_alignment,
        } = *images;
        let direct_similarity = direct.mean();
        let mut diagnostics = MotionDiagnostics::at(MotionStage::EmptyDescriptors);
        diagnostics.reference_keypoints = evidence.reference_features;
        diagnostics.incoming_keypoints = evidence.incoming_features;
        diagnostics.direct_similarity = direct_similarity;
        let observations = evidence.observations;
        diagnostics.mutual_matches = observations.len() as u32;
        let candidates = candidate_offsets(&observations, self.axis, maximum_shift);
        let scoring_perf = crate::perf::Scope::new(crate::perf::Stage::CandidateScoring);
        let registration = if maximum_shift > 0 {
            search
                .get_or_insert_with(|| {
                    RegistrationSearch::new(
                        motion_reference,
                        previous_raw,
                        incoming,
                        self.axis,
                        maximum_shift,
                        self.options.min_confidence,
                    )
                })
                .register(&candidates)
        } else {
            Registration::Unavailable
        };
        if let Registration::Ambiguous(aliases) = registration {
            diagnostics.stage = MotionStage::LowConfidence;
            diagnostics.ambiguous_offsets = aliases;
            return EvaluatedMotion::rejected(false, 0.0, diagnostics);
        }
        if observations.is_empty() && matches!(registration, Registration::Unavailable) {
            return EvaluatedMotion::rejected(direct_similarity < 0.6, 0.0, diagnostics);
        }
        let score = |offset| {
            let compensated = if offset == 0 {
                None
            } else {
                Some(SimilarityMap::between(
                    reference_gray,
                    incoming_gray,
                    self.layout,
                    self.axis,
                    offset,
                ))
            };
            score_candidate(
                offset,
                self.axis,
                &observations,
                direct,
                zero_alignment,
                compensated.as_ref().unwrap_or(zero_alignment),
            )
        };
        let mut scored: Vec<_> = if self.parallel_work {
            candidates.into_par_iter().map(score).collect()
        } else {
            candidates.into_iter().map(score).collect()
        };
        scored.sort_by(|left, right| {
            right
                .score
                .total_cmp(&left.score)
                .then_with(|| left.offset.abs().cmp(&right.offset.abs()))
                .then_with(|| left.offset.cmp(&right.offset))
        });

        scoring_perf.finish();
        if let Registration::Verified {
            offset,
            confidence,
            error,
            blocks,
        } = registration
        {
            let index = scored.iter().position(|c| c.offset == offset);
            let mut verified = index.map_or_else(|| score(offset), |i| scored[i].clone());
            if let Some(i) = index {
                scored.remove(i);
            }
            verified.overlap_blocks = Some(blocks);
            verified.precise_alignment_error = Some(error);
            scored.insert(0, verified);
            diagnostics.stage = MotionStage::Selected;
            diagnostics.selected_offset = Some(offset);
            diagnostics.candidates = scored;
            return EvaluatedMotion {
                estimate: MotionEstimate::motion(offset, confidence, diagnostics),
                scene_cut: false,
                margin: confidence,
            };
        }
        let strongest_mode = scored[0].offset;
        if strongest_mode != 0 {
            let _perf = crate::perf::Scope::new(crate::perf::Stage::Refinement);
            let (refined_offset, precise_error) = refine_motion_offset(
                motion_reference,
                incoming,
                self.axis,
                strongest_mode,
                maximum_shift,
                &observations,
            );
            let mut candidate = scored
                .iter()
                .position(|c| c.offset == refined_offset)
                .map_or_else(|| score(refined_offset), |i| scored.remove(i));
            candidate.precise_alignment_error = Some(precise_error);
            scored.insert(0, candidate);
        }

        let best_score = scored[0].score;
        let second_score = scored
            .iter()
            .skip(1)
            .find(|candidate| (candidate.offset - scored[0].offset).abs() > INLIER_TOLERANCE)
            .map(|candidate| candidate.score)
            .unwrap_or(0.0);
        let margin = if best_score > f32::EPSILON {
            ((best_score - second_score) / best_score).clamp(0.0, 1.0)
        } else {
            0.0
        };
        let best = &scored[0];
        let confidence = (0.40 * best.weighted_inlier_share
            + 0.25 * best.spatial_coverage
            + 0.20 * best.residual_gain
            + 0.15 * margin)
            .clamp(0.0, 1.0);
        diagnostics.selected_offset = Some(best.offset);
        diagnostics.candidates = scored.clone();

        if best.offset == 0 {
            diagnostics.stage = MotionStage::SelectedNoMotion;
            return EvaluatedMotion {
                estimate: MotionEstimate::no_motion(confidence, diagnostics),
                scene_cut: false,
                margin,
            };
        }

        let accepted = has_motion_support(best) && confidence >= self.options.min_confidence;
        if accepted {
            let selected_offset = best.offset;
            diagnostics.stage = MotionStage::Selected;
            return EvaluatedMotion {
                estimate: MotionEstimate::motion(selected_offset, confidence, diagnostics),
                scene_cut: false,
                margin,
            };
        }

        let scene_cut = direct_similarity < 0.5
            && scored
                .iter()
                .all(|candidate| candidate.alignment_error > 0.6);
        diagnostics.stage = MotionStage::LowConfidence;
        EvaluatedMotion::rejected(scene_cut, confidence, diagnostics)
    }
}

#[cfg(test)]
#[path = "estimator_optimization_tests.rs"]
mod optimization_tests;

#[cfg(test)]
#[path = "estimator_refinement_tests.rs"]
mod refinement_tests;

#[cfg(test)]
mod tests {
    use super::*;

    fn full_resolution_estimator(frame: &Frame, axis: StitchAxis) -> VerticalMotionEstimator {
        let mut estimator = VerticalMotionEstimator::new_for_axis(
            frame.geometry(),
            axis,
            MotionEstimatorOptions::default(),
        )
        .unwrap();
        estimator.sampling = SamplingPlan::full_resolution(frame.geometry(), axis);
        estimator.pyramid_plan =
            crate::orb::PyramidPlan::new(frame.width() as usize, frame.height() as usize);
        estimator
    }

    fn directional_fixture(
        axis: StitchAxis,
        cross: u32,
        primary: u32,
        scroll: u32,
        sparse: bool,
    ) -> Frame {
        let (width, height) = match axis {
            StitchAxis::Vertical => (cross, primary),
            StitchAxis::Horizontal => (primary, cross),
        };
        let pixels = (0..height)
            .flat_map(|y| {
                (0..width).map(move |x| {
                    let (cross, along) = match axis {
                        StitchAxis::Vertical => (x, y + scroll),
                        StitchAxis::Horizontal => (y, x + scroll),
                    };
                    if sparse && !(cross > 400 && cross < 560) {
                        return 255;
                    }
                    let mut hash =
                        cross.wrapping_mul(0xc2b2_ae35) ^ along.wrapping_mul(0x27d4_eb2d);
                    hash ^= hash >> 16;
                    hash = hash.wrapping_mul(0x7feb_352d);
                    hash ^= hash >> 15;
                    (hash >> 24) as u8
                })
            })
            .collect();
        Frame::new(width, height, PixelFormat::Gray8, pixels).unwrap()
    }

    #[test]
    fn directional_sampling_preserves_offsets_and_exact_stitched_pixels() {
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            for cross in [320, 1024, 2048] {
                for shift in [1, 17] {
                    let first = directional_fixture(axis, cross, 256, 0, false);
                    let second = directional_fixture(axis, cross, 256, shift, false);
                    for reverse in [false, true] {
                        let (reference, incoming) = if reverse {
                            (&second, &first)
                        } else {
                            (&first, &second)
                        };
                        let mut estimator = VerticalMotionEstimator::new_for_axis(
                            reference.geometry(),
                            axis,
                            MotionEstimatorOptions::default(),
                        )
                        .unwrap();
                        let estimated = estimator.estimate(reference, reference, incoming).unwrap();
                        if shift == 1 {
                            let baseline = full_resolution_estimator(reference, axis)
                                .estimate(reference, reference, incoming)
                                .unwrap();
                            assert_eq!(
                                estimated.outcome, baseline.outcome,
                                "one-pixel compatibility: {axis:?}, cross={cross}"
                            );
                            continue;
                        }
                        let expected = if reverse {
                            shift as i32
                        } else {
                            -(shift as i32)
                        };
                        assert_eq!(
                            estimated.offset(),
                            Some(expected),
                            "{axis:?}, cross={cross}, shift={expected}: {estimated:#?}"
                        );
                        let stitched = crate::stitch(
                            &[reference.clone(), incoming.clone()],
                            crate::StitchOptions {
                                axis,
                                ..crate::StitchOptions::default()
                            },
                        )
                        .unwrap();
                        let document = directional_fixture(axis, cross, 256 + shift, 0, false);
                        assert_eq!(
                            stitched.image, document,
                            "{axis:?}, cross={cross}, shift={expected}"
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn narrow_content_retains_known_motion() {
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            let first = directional_fixture(axis, 2048, 320, 0, true);
            let second = directional_fixture(axis, 2048, 320, 17, true);
            let mut estimator = VerticalMotionEstimator::new_for_axis(
                first.geometry(),
                axis,
                MotionEstimatorOptions::default(),
            )
            .unwrap();
            let result = estimator.estimate(&first, &first, &second).unwrap();
            assert_eq!(result.offset(), Some(-17), "{axis:?}: {result:#?}");
        }
    }

    #[test]
    fn large_fixed_overlays_and_repeated_rules_preserve_motion() {
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            let make = |scroll| {
                let frame = directional_fixture(axis, 1024, 320, scroll, false);
                let fixed = directional_fixture(axis, 1024, 320, 0, false);
                let mut pixels = frame.pixels().to_vec();
                for y in 0..frame.height() {
                    for x in 0..frame.width() {
                        let (cross, primary) = match axis {
                            StitchAxis::Vertical => (x, y),
                            StitchAxis::Horizontal => (y, x),
                        };
                        let index = (y * frame.width() + x) as usize;
                        if cross < 128 || primary < 32 {
                            pixels[index] = fixed.pixels()[index];
                        } else if (primary + scroll) % 24 == 0 {
                            pixels[index] = 0;
                        }
                    }
                }
                Frame::new(frame.width(), frame.height(), PixelFormat::Gray8, pixels).unwrap()
            };
            let mut previous = make(0);
            let mut estimator = VerticalMotionEstimator::new_for_axis(
                previous.geometry(),
                axis,
                MotionEstimatorOptions::default(),
            )
            .unwrap();
            for step in 1..=4 {
                let incoming = make(step * 17);
                let estimate = estimator.estimate(&previous, &previous, &incoming).unwrap();
                assert_eq!(estimate.offset(), Some(-17), "{axis:?}: {estimate:#?}");
                previous = incoming;
            }
        }
    }

    #[test]
    fn narrow_texture_in_a_mostly_unchanged_tile_can_refine_motion() {
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            let make = |scroll| {
                let frame = directional_fixture(axis, 2048, 320, scroll, false);
                let mut pixels = frame.pixels().to_vec();
                for y in 0..frame.height() {
                    for x in 0..frame.width() {
                        let cross = match axis {
                            StitchAxis::Vertical => x,
                            StitchAxis::Horizontal => y,
                        };
                        if !(420..500).contains(&cross) {
                            pixels[(y * frame.width() + x) as usize] = 255;
                        }
                    }
                }
                Frame::new(frame.width(), frame.height(), PixelFormat::Gray8, pixels).unwrap()
            };
            let reference = make(0);
            let incoming = make(17);
            let mut estimator = VerticalMotionEstimator::new_for_axis(
                reference.geometry(),
                axis,
                MotionEstimatorOptions::default(),
            )
            .unwrap();
            let actual = estimator
                .estimate(&reference, &reference, &incoming)
                .unwrap();
            assert_eq!(actual.offset(), Some(-17), "{axis:?}: {actual:#?}");
            assert_eq!(
                actual.diagnostics.candidates[0].precise_alignment_error,
                Some(0.0)
            );
            // The textured strip used to be excluded by the tile's high direct
            // similarity, forcing an unnecessary full-resolution retry.
            assert!(estimator.full_pyramid_plan.get().is_none());
        }
    }

    #[test]
    fn rejected_reduced_pass_retries_without_double_scene_cut_updates() {
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            let first = directional_fixture(axis, 1024, 224, 0, false);
            let mut estimator = VerticalMotionEstimator::new_for_axis(
                first.geometry(),
                axis,
                MotionEstimatorOptions::default(),
            )
            .unwrap();
            let mut full = full_resolution_estimator(&first, axis);
            let mut previous = first;
            for seed in 1..=3 {
                // Flat, unrelated frames guarantee empty descriptors on both attempts.
                let incoming = Frame::new(
                    previous.width(),
                    previous.height(),
                    PixelFormat::Gray8,
                    vec![if seed % 2 == 0 { 255 } else { 0 }; previous.pixels().len()],
                )
                .unwrap();
                #[cfg(feature = "perf-instrumentation")]
                crate::perf::reset();
                let actual = estimator.estimate(&previous, &previous, &incoming).unwrap();
                #[cfg(feature = "perf-instrumentation")]
                assert_eq!(
                    crate::perf::snapshot().calls[crate::perf::Stage::FeatureExtraction as usize],
                    2
                );
                let expected = full.estimate(&previous, &previous, &incoming).unwrap();
                assert_eq!(actual, expected);
                assert_eq!(estimator.scene_cut_streak, full.scene_cut_streak);
                assert!(estimator.full_pyramid_plan.get().is_some());
                previous = incoming;
            }
        }
    }

    #[test]
    fn duplicates_bypass_sampling_and_no_motion_retries_once() {
        let first = directional_fixture(StitchAxis::Vertical, 1024, 256, 0, false);
        let mut estimator =
            VerticalMotionEstimator::new(first.geometry(), MotionEstimatorOptions::default())
                .unwrap();
        assert_eq!(
            estimator
                .estimate(&first, &first, &first)
                .unwrap()
                .diagnostics
                .stage,
            MotionStage::IdenticalInterior
        );
        assert!(estimator.full_pyramid_plan.get().is_none());
        let mut pixels = first.pixels().to_vec();
        for y in 80..100 {
            pixels[y * 1024 + 80..y * 1024 + 100].fill(0);
        }
        let incoming = Frame::new(1024, 256, PixelFormat::Gray8, pixels).unwrap();
        #[cfg(feature = "perf-instrumentation")]
        crate::perf::reset();
        let actual = estimator.estimate(&first, &first, &incoming).unwrap();
        #[cfg(feature = "perf-instrumentation")]
        assert_eq!(
            crate::perf::snapshot().calls[crate::perf::Stage::FeatureExtraction as usize],
            2
        );
        let expected = full_resolution_estimator(&first, StitchAxis::Vertical)
            .estimate(&first, &first, &incoming)
            .unwrap();
        assert_eq!(actual, expected);
        assert_eq!(actual.outcome, crate::MotionOutcome::NoMotion);
        assert!(estimator.full_pyramid_plan.get().is_some());
    }

    #[test]
    fn retry_requires_accepted_motion_with_positive_margin() {
        let mut evaluated = EvaluatedMotion {
            estimate: MotionEstimate::motion(
                -17,
                1.0,
                MotionDiagnostics::at(MotionStage::Selected),
            ),
            scene_cut: false,
            margin: 0.0,
        };
        assert!(evaluated.needs_full_resolution());
        evaluated.margin = 0.1;
        assert!(evaluated.needs_full_resolution());
        evaluated
            .estimate
            .diagnostics
            .candidates
            .push(CandidateDiagnostics {
                offset: -17,
                raw_inliers: 20,
                inlier_tiles: 8,
                weighted_support: 8.0,
                weighted_inlier_share: 1.0,
                spatial_coverage: 1.0,
                alignment_error: 0.0,
                precise_alignment_error: Some(1.0),
                overlap_blocks: None,
                residual_gain: 1.0,
                score: 1.0,
            });
        assert!(evaluated.needs_full_resolution());
        evaluated.estimate.diagnostics.candidates[0].precise_alignment_error = Some(0.0);
        assert!(!evaluated.needs_full_resolution());
        evaluated.estimate.outcome = crate::MotionOutcome::Indeterminate;
        assert!(evaluated.needs_full_resolution());
    }

    #[cfg(feature = "perf-instrumentation")]
    #[test]
    #[ignore = "Release-only matcher benchmark using the local scrolling image fixture"]
    fn directional_sampling_release_benchmark() {
        if cfg!(debug_assertions) {
            panic!("use the windows-msvc-performance Release environment");
        }
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../../snow_shot/test-imgs/scrollscreenshot-test.png");
        let source = Frame::decode(path).unwrap();
        let frames: Vec<_> = (0..=12)
            .map(|step| source.crop(0, step * 25, source.width(), 1600).unwrap())
            .collect();
        let mut changed_pixels = frames[0].pixels().to_vec();
        let channels = frames[0].pixel_format().channels() as usize;
        for y in 80..100 {
            let start = (y * frames[0].width() as usize + 80) * channels;
            changed_pixels[start..start + 20 * channels].fill(0);
        }
        let changed = Frame::new(
            frames[0].width(),
            frames[0].height(),
            frames[0].pixel_format(),
            changed_pixels,
        )
        .unwrap();
        let mut reports = Vec::new();
        // Alternate execution order, with an unreported warmup for each mode.
        for round in 0..4 {
            for adaptive in if round % 2 == 0 {
                [false, true]
            } else {
                [true, false]
            } {
                let mut estimator = if adaptive {
                    VerticalMotionEstimator::new(
                        frames[0].geometry(),
                        MotionEstimatorOptions::default(),
                    )
                    .unwrap()
                } else {
                    full_resolution_estimator(&frames[0], StitchAxis::Vertical)
                };
                let mut samples = Vec::new();
                for pair in frames.windows(2) {
                    crate::perf::reset();
                    let started = std::time::Instant::now();
                    let result = estimator.estimate(&pair[0], &pair[0], &pair[1]).unwrap();
                    let elapsed = started.elapsed().as_nanos() as u64;
                    let perf = crate::perf::snapshot();
                    assert_eq!(
                        result.offset(),
                        Some(-25),
                        "adaptive={adaptive}, round={round}: {result:#?}"
                    );
                    samples.push(serde_json::json!({
                        "estimate_ns": elapsed,
                        "feature_ns": perf.elapsed_ns[crate::perf::Stage::FeatureExtraction as usize],
                        "attempts": perf.calls[crate::perf::Stage::FeatureExtraction as usize],
                        "offset": result.offset(),
                    }));
                }
                if round > 0 {
                    reports.push(serde_json::json!({"workload": "scroll", "round": round, "adaptive": adaptive, "samples": samples}));
                }
                let mut fallback_samples = Vec::new();
                for _ in 0..4 {
                    crate::perf::reset();
                    let started = std::time::Instant::now();
                    let result = estimator
                        .estimate(&frames[0], &frames[0], &changed)
                        .unwrap();
                    let elapsed = started.elapsed().as_nanos() as u64;
                    let perf = crate::perf::snapshot();
                    assert_eq!(result.outcome, crate::MotionOutcome::NoMotion);
                    assert_eq!(
                        perf.calls[crate::perf::Stage::FeatureExtraction as usize],
                        if adaptive { 2 } else { 1 }
                    );
                    fallback_samples.push(serde_json::json!({
                        "estimate_ns": elapsed,
                        "feature_ns": perf.elapsed_ns[crate::perf::Stage::FeatureExtraction as usize],
                        "attempts": perf.calls[crate::perf::Stage::FeatureExtraction as usize],
                    }));
                }
                if round > 0 {
                    reports.push(serde_json::json!({"workload": "no-motion", "round": round, "adaptive": adaptive, "samples": fallback_samples}));
                }
            }
        }
        let output = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../../build/windows-msvc-performance/directional-matcher-results.json");
        std::fs::write(&output, serde_json::to_vec_pretty(&reports).unwrap()).unwrap();
        eprintln!("matcher benchmark report: {}", output.display());
    }

    fn observation(tile: usize, distance: f32, dx: i32, dy: i32) -> MatchObservation {
        MatchObservation {
            distance,
            reference_x: tile as f32 * 32.0 + 8.0,
            reference_y: 8.0,
            incoming_x: tile as f32 * 32.0 + 8.0,
            incoming_y: 8.0 + dy as f32,
            reference_tile: tile,
            dx,
            dy,
        }
    }

    #[test]
    fn candidate_generation_clusters_neighboring_offsets() {
        let observations = vec![
            observation(0, 10.0, 0, -20),
            observation(1, 10.0, 0, -21),
            observation(2, 10.0, 0, -19),
            observation(3, 10.0, 0, 0),
        ];
        let candidates = candidate_offsets(&observations, StitchAxis::Vertical, 100);
        assert_eq!(candidates[0], 0);
        assert!(
            candidates
                .iter()
                .any(|candidate| (-22..=-18).contains(candidate))
        );
        assert_eq!(
            candidates
                .iter()
                .filter(|candidate| (-22..=-18).contains(*candidate))
                .count(),
            1
        );
    }

    #[test]
    fn invalid_options_are_rejected() {
        for options in [
            MotionEstimatorOptions {
                tile_size: 0,
                ..MotionEstimatorOptions::default()
            },
            MotionEstimatorOptions {
                max_features: 10,
                ..MotionEstimatorOptions::default()
            },
            MotionEstimatorOptions {
                min_confidence: f32::NAN,
                ..MotionEstimatorOptions::default()
            },
        ] {
            assert!(validate_estimator_options(options).is_err());
        }
    }

    #[test]
    fn coordinate_rounding_happens_before_subtraction() {
        assert_eq!(round_coordinate(1.49) - round_coordinate(0.51), 0);
        assert_eq!(round_coordinate(1.5) - round_coordinate(0.49), 2);
    }

    #[test]
    fn exact_lookup_preserves_duplicates_unmatched_queries_and_ratio_ties() {
        let feature = |i: u8| BinaryFeature {
            x: f32::from(i),
            y: 5.0,
            descriptor: std::array::from_fn(|j| {
                i.wrapping_mul(73).wrapping_add((j as u8).wrapping_mul(7))
            }),
        };
        let mut train: Vec<_> = (0..67).map(feature).collect();
        train[61] = train[3];
        let mut queries = train.clone();
        for q in queries.iter_mut().skip(10).step_by(2) {
            q.descriptor[0] ^= 9;
        }
        queries[57] = feature(200);
        assert_eq!(
            match_with_exact_lookup(&queries, &train),
            match_descriptors_portable(&queries, &train)
        );
    }

    #[test]
    fn mutual_matches_agree_with_two_exhaustive_ratio_searches() {
        let features = (0..32)
            .map(|i| BinaryFeature {
                x: (i * 7) as f32,
                y: (i * 5) as f32,
                descriptor: std::array::from_fn(|byte| ((i * 73 + byte * 19) ^ (i + byte)) as u8),
            })
            .collect::<Vec<_>>();
        let mut incoming = features.clone();
        for (i, feature) in incoming.iter_mut().enumerate() {
            if i % 2 == 1 {
                feature.descriptor[0] ^= 1;
            }
            if i % 3 == 0 {
                feature.x += 5.0;
            }
        }
        incoming[3].descriptor = incoming[2].descriptor;
        let mut reference = features;
        reference[5].descriptor = reference[6].descriptor;
        let layout = TileLayout::new(256, 180, 32);
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            for count in [0, 1, 2, reference.len()] {
                let reference = &reference[..count];
                let reverse = match_descriptors_portable(&incoming, reference);
                let expected = match_descriptors_portable(reference, &incoming)
                    .into_iter()
                    .enumerate()
                    .filter_map(|(i, m)| {
                        let m = m?;
                        let a = &reference[i];
                        let b = &incoming[m.train_index];
                        let dx = round_coordinate(b.x) - round_coordinate(a.x);
                        let dy = round_coordinate(b.y) - round_coordinate(a.y);
                        (reverse[m.train_index]?.train_index == i
                            && axis.cross_delta(dx, dy).abs() <= MAX_CROSS_AXIS_DELTA)
                            .then_some((a.x, b.x, m.distance))
                    })
                    .collect::<Vec<_>>();
                let actual = mutual_observations_rust(reference, &incoming, layout, axis);
                assert_eq!(
                    actual
                        .into_iter()
                        .map(|m| (m.reference_x, m.incoming_x, m.distance as u32))
                        .collect::<Vec<_>>(),
                    expected
                );
            }
        }
    }

    #[test]
    fn repeated_forward_matches_leave_at_most_one_reverse_candidate() {
        let feature = |descriptor, y| BinaryFeature {
            x: 16.0,
            y,
            descriptor,
        };
        let incoming = [feature([0; 32], 12.0), feature([255; 32], 12.0)];
        let mut repeated = [0; 32];
        repeated[..25].fill(1);
        let mut reference = vec![feature([0xAA; 32], 36.0); 512];
        reference.resize(1024, feature(repeated, 36.0));
        let layout = TileLayout::new(96, 96, 16);

        // An equal distance and a distance exactly at the ratio threshold both fail.
        for (distance, expected) in [(25, None), (20, None), (19, Some(19))] {
            let mut descriptor = repeated;
            descriptor[distance..].fill(0);
            reference[1023] = feature(descriptor, 64.0);
            let mut forward = match_descriptors(&reference, &incoming);
            assert_eq!(forward.iter().flatten().count(), 512);
            prune_reverse_candidates(&mut forward, incoming.len());
            let count = usize::from(expected.is_some());
            assert_eq!(forward.iter().flatten().count(), count);
            assert_eq!(forward[1023].map(|matched| matched.distance), expected);

            let observations =
                mutual_observations_rust(&reference, &incoming, layout, StitchAxis::Vertical);
            assert_eq!(observations.len(), count);
            assert_eq!(
                observations
                    .first()
                    .map(|matched| (matched.dy, matched.distance)),
                expected.map(|distance| (-52, distance as f32))
            );
        }
    }

    #[test]
    fn reverse_ratio_still_compares_references_that_chose_another_feature() {
        let zero = BinaryFeature {
            x: 16.0,
            y: 12.0,
            descriptor: [0; 32],
        };
        let mut other = zero;
        other.descriptor[..3].fill(255);
        let incoming = [zero, other];

        let mut first = zero;
        first.y = 24.0;
        first.descriptor[3..6].copy_from_slice(&[255, 255, 15]);
        other.y = 36.0;
        let reference = [first, other];
        let mut forward = match_descriptors(&reference, &incoming);
        prune_reverse_candidates(&mut forward, incoming.len());
        assert_eq!(forward[0].unwrap().train_index, 0);
        assert_eq!(forward[1].unwrap().train_index, 1);

        // The first reference is 20 bits from zero. The other reference is 24
        // bits away, close enough to reject the first under the reverse ratio.
        let layout = TileLayout::new(96, 96, 16);
        let observations =
            mutual_observations_rust(&reference, &incoming, layout, StitchAxis::Vertical);
        assert_eq!(observations.len(), 1);
        assert_eq!(observations[0].reference_y, other.y);
    }

    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    #[test]
    fn popcnt_matcher_matches_portable_results() {
        if !std::arch::is_x86_feature_detected!("popcnt") {
            return;
        }
        let feature = |seed: u8| BinaryFeature {
            x: 0.0,
            y: 0.0,
            descriptor: std::array::from_fn(|index| {
                seed.wrapping_mul(37)
                    .wrapping_add((index as u8).wrapping_mul(13))
            }),
        };
        let queries = [feature(1), feature(7), feature(19)];
        let train = [feature(2), feature(6), feature(8), feature(18), feature(20)];
        let portable = match_descriptors_portable(&queries, &train);
        let accelerated = unsafe { match_descriptors_popcnt(&queries, &train) };
        assert_eq!(accelerated, portable);
    }

    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    #[test]
    fn avx2_matcher_matches_portable_results() {
        if !std::arch::is_x86_feature_detected!("avx2") {
            return;
        }
        let feature = |seed: u8| BinaryFeature {
            x: 0.0,
            y: 0.0,
            descriptor: std::array::from_fn(|index| {
                seed.wrapping_mul(37)
                    .wrapping_add((index as u8).wrapping_mul(13))
            }),
        };
        let queries = [feature(1), feature(7), feature(19)];
        let train = [feature(2), feature(6), feature(8), feature(18), feature(20)];
        let portable = match_descriptors_portable(&queries, &train);
        let accelerated = unsafe { match_descriptors_avx2(&queries, &train) };
        assert_eq!(accelerated, portable);
    }

    #[cfg(target_arch = "aarch64")]
    #[test]
    fn neon_matcher_executes_and_preserves_nearest_ties_and_ratio_rejection() {
        assert!(std::arch::is_aarch64_feature_detected!("neon"));
        let before = NEON_MATCHES.load(std::sync::atomic::Ordering::Relaxed);
        let feature = |seed: u8| BinaryFeature {
            x: 0.0,
            y: 0.0,
            descriptor: std::array::from_fn(|i| {
                seed.wrapping_mul(73)
                    .wrapping_add((i as u8).wrapping_mul(7))
            }),
        };
        let queries = [feature(0), feature(1), feature(127), feature(255)];
        let mut train = vec![feature(0), feature(0), feature(1), feature(2), feature(255)];
        train.push(BinaryFeature {
            descriptor: [255; 32],
            ..feature(0)
        });
        for len in 0..=train.len() {
            for query in &queries {
                assert_eq!(
                    unsafe { nearest_two_neon(query, &train[..len]) },
                    nearest_two(query, &train[..len])
                );
            }
            assert_eq!(
                unsafe { match_descriptors_neon(&queries, &train[..len]) },
                match_descriptors_portable(&queries, &train[..len])
            );
            assert_eq!(
                match_descriptors(&queries, &train[..len]),
                match_descriptors_portable(&queries, &train[..len])
            );
        }
        let zero = BinaryFeature {
            descriptor: [0; 32],
            ..feature(0)
        };
        let full = BinaryFeature {
            descriptor: [255; 32],
            ..feature(0)
        };
        assert_eq!(
            unsafe { nearest_two_neon(&zero, &[full, zero]) }.unwrap()[1].distance,
            256
        );
        // Exercise the widening reduction at distance 256 and equal best matches
        // on opposite sides of a four-descriptor block boundary.
        for candidates in [[full; 8], [full, full, full, zero, zero, full, full, full]] {
            assert_eq!(
                unsafe { nearest_two_neon(&zero, &candidates) },
                nearest_two(&zero, &candidates)
            );
        }
        assert!(NEON_MATCHES.load(std::sync::atomic::Ordering::Relaxed) > before);
    }

    #[cfg(target_arch = "aarch64")]
    #[test]
    #[ignore = "paired Release-only ARM64 kernel measurement"]
    fn neon_release_benchmark_matching() {
        use std::hint::black_box;
        let feature = |seed: u32| BinaryFeature {
            x: 0.0,
            y: 0.0,
            descriptor: std::array::from_fn(|i| {
                seed.wrapping_mul(0x9e3779b9)
                    .wrapping_add(i as u32 * 0x10203)
                    .rotate_left(i as u32) as u8
            }),
        };
        let queries: Vec<_> = (0..128).map(feature).collect();
        let train: Vec<_> = (0..512).map(feature).collect();
        crate::neon_benchmark::paired(
            "stitch_descriptor_matching",
            8,
            || match_descriptors_portable(black_box(&queries), black_box(&train)),
            || unsafe { match_descriptors_neon(black_box(&queries), black_box(&train)) },
        );
    }

    fn scrolling_fixture(scroll: u32, animation: u8) -> Frame {
        const WIDTH: u32 = 320;
        const HEIGHT: u32 = 224;
        const TOP: u32 = 40;
        const BOTTOM: u32 = 32;
        const SIDEBAR: u32 = 64;
        let mut pixels = Vec::with_capacity((WIDTH * HEIGHT) as usize);
        for y in 0..HEIGHT {
            for x in 0..WIDTH {
                let fixed = !(TOP..HEIGHT - BOTTOM).contains(&y) || x < SIDEBAR;
                let source_y = if fixed { y } else { y - TOP + scroll };
                let mut hash = if fixed {
                    x.wrapping_mul(0x9e37_79b9) ^ y.wrapping_mul(0x85eb_ca6b)
                } else {
                    x.wrapping_mul(0xc2b2_ae35) ^ source_y.wrapping_mul(0x27d4_eb2d)
                };
                hash ^= hash >> 16;
                hash = hash.wrapping_mul(0x7feb_352d);
                hash ^= hash >> 15;
                let mut value = (hash >> 24) as u8;
                if (192..320).contains(&x) && (64..160).contains(&y) {
                    let mut dynamic_hash = x.wrapping_mul(0x1656_67b1)
                        ^ y.wrapping_mul(0xd3a2_646c)
                        ^ u32::from(animation).wrapping_mul(0xfd70_46c5);
                    dynamic_hash ^= dynamic_hash >> 16;
                    dynamic_hash = dynamic_hash.wrapping_mul(0x7feb_352d);
                    value = (dynamic_hash >> 24) as u8;
                }
                pixels.push(value);
            }
        }
        Frame::new(WIDTH, HEIGHT, PixelFormat::Gray8, pixels).unwrap()
    }

    fn horizontal_scrolling_fixture(scroll: u32) -> Frame {
        const WIDTH: u32 = 224;
        const HEIGHT: u32 = 192;
        const LEFT: u32 = 32;
        const RIGHT: u32 = 24;
        const TOP: u32 = 32;
        let mut pixels = Vec::with_capacity((WIDTH * HEIGHT) as usize);
        for y in 0..HEIGHT {
            for x in 0..WIDTH {
                let fixed = !(LEFT..WIDTH - RIGHT).contains(&x) || y < TOP;
                let source_x = if fixed { x } else { x - LEFT + scroll };
                let mut hash = if fixed {
                    x.wrapping_mul(0x9e37_79b9) ^ y.wrapping_mul(0x85eb_ca6b)
                } else {
                    source_x.wrapping_mul(0xc2b2_ae35) ^ y.wrapping_mul(0x27d4_eb2d)
                };
                hash ^= hash >> 16;
                hash = hash.wrapping_mul(0x7feb_352d);
                hash ^= hash >> 15;
                pixels.push((hash >> 24) as u8);
            }
        }
        Frame::new(WIDTH, HEIGHT, PixelFormat::Gray8, pixels).unwrap()
    }

    #[test]
    fn robust_estimator_recovers_scroll_among_fixed_and_dynamic_regions() {
        let reference = scrolling_fixture(0, 0);
        let incoming = scrolling_fixture(24, 1);
        let mut estimator =
            VerticalMotionEstimator::new(reference.geometry(), MotionEstimatorOptions::default())
                .unwrap();
        let estimate = estimator
            .estimate(&reference, &reference, &incoming)
            .unwrap();
        assert_eq!(
            estimate.outcome,
            crate::MotionOutcome::Motion { offset: -24 },
            "{estimate:#?}"
        );
        assert!(estimate.confidence >= MotionEstimatorOptions::default().min_confidence);
        assert!(
            estimate
                .diagnostics
                .candidates
                .iter()
                .find(|candidate| candidate.offset == -24)
                .is_some_and(|candidate| candidate.inlier_tiles >= MIN_INLIER_TILES)
        );
    }

    #[test]
    fn robust_estimator_recovers_reverse_scroll() {
        let reference = scrolling_fixture(24, 0);
        let incoming = scrolling_fixture(0, 1);
        let mut estimator =
            VerticalMotionEstimator::new(reference.geometry(), MotionEstimatorOptions::default())
                .unwrap();
        let estimate = estimator
            .estimate(&reference, &reference, &incoming)
            .unwrap();
        assert_eq!(
            estimate.outcome,
            crate::MotionOutcome::Motion { offset: 24 },
            "{estimate:#?}"
        );
    }

    #[test]
    fn horizontal_estimator_recovers_both_scroll_directions() {
        let left = horizontal_scrolling_fixture(0);
        let right = horizontal_scrolling_fixture(24);
        let mut estimator = VerticalMotionEstimator::new_for_axis(
            left.geometry(),
            StitchAxis::Horizontal,
            MotionEstimatorOptions::default(),
        )
        .unwrap();
        assert_eq!(
            estimator.estimate(&left, &left, &right).unwrap().outcome,
            crate::MotionOutcome::Motion { offset: -24 }
        );

        let mut estimator = VerticalMotionEstimator::new_for_axis(
            left.geometry(),
            StitchAxis::Horizontal,
            MotionEstimatorOptions::default(),
        )
        .unwrap();
        assert_eq!(
            estimator.estimate(&right, &right, &left).unwrap().outcome,
            crate::MotionOutcome::Motion { offset: 24 }
        );
    }

    #[test]
    fn animation_without_scroll_never_creates_false_motion() {
        let reference = scrolling_fixture(0, 0);
        let incoming = scrolling_fixture(0, 1);
        let mut estimator =
            VerticalMotionEstimator::new(reference.geometry(), MotionEstimatorOptions::default())
                .unwrap();
        let estimate = estimator
            .estimate(&reference, &reference, &incoming)
            .unwrap();
        assert!(!matches!(
            estimate.outcome,
            crate::MotionOutcome::Motion { .. }
        ));
    }

    #[test]
    fn earlier_content_does_not_change_matching_for_the_current_frame_pair() {
        let mut previous = scrolling_fixture(0, 0);
        let mut estimator =
            VerticalMotionEstimator::new(previous.geometry(), MotionEstimatorOptions::default())
                .unwrap();
        for step in 1..=4 {
            let incoming = scrolling_fixture(step * 16, step as u8);
            let estimate = estimator.estimate(&previous, &previous, &incoming).unwrap();
            assert_eq!(estimate.offset(), Some(-16), "{estimate:#?}");
            previous = incoming;
        }

        let reference = directional_fixture(StitchAxis::Vertical, 320, 224, 0, false);
        let incoming = directional_fixture(StitchAxis::Vertical, 320, 224, 24, false);
        let actual = estimator
            .estimate(&reference, &reference, &incoming)
            .unwrap();
        let expected =
            VerticalMotionEstimator::new(reference.geometry(), MotionEstimatorOptions::default())
                .unwrap()
                .estimate(&reference, &reference, &incoming)
                .unwrap();
        assert_eq!(expected.offset(), Some(-24), "{expected:#?}");
        assert_eq!(actual, expected);
    }

    #[test]
    fn blank_to_text_transitions_match_a_fresh_estimator_in_both_directions() {
        for axis in [StitchAxis::Vertical, StitchAxis::Horizontal] {
            let make = |scroll| {
                let frame = directional_fixture(axis, 320, 320, scroll, false);
                let mut pixels = frame.pixels().to_vec();
                for y in 0..frame.height() {
                    for x in 0..frame.width() {
                        let primary = match axis {
                            StitchAxis::Vertical => y,
                            StitchAxis::Horizontal => x,
                        };
                        if primary + scroll < 192 {
                            pixels[(y * frame.width() + x) as usize] = 255;
                        }
                    }
                }
                Frame::new(frame.width(), frame.height(), PixelFormat::Gray8, pixels).unwrap()
            };
            let options = MotionEstimatorOptions {
                max_features: 128,
                ..MotionEstimatorOptions::default()
            };
            let mut previous = make(0);
            let mut estimator =
                VerticalMotionEstimator::new_for_axis(previous.geometry(), axis, options).unwrap();
            let mut previous_scroll = 0;
            for scroll in (1..=8).chain((0..8).rev()).map(|step| step * 32) {
                let incoming = make(scroll);
                let actual = estimator.estimate(&previous, &previous, &incoming).unwrap();
                let expected =
                    VerticalMotionEstimator::new_for_axis(previous.geometry(), axis, options)
                        .unwrap()
                        .estimate(&previous, &previous, &incoming)
                        .unwrap();
                assert_eq!(
                    expected.offset(),
                    Some(previous_scroll as i32 - scroll as i32),
                    "{axis:?} scroll={scroll}: {expected:#?}"
                );
                assert_eq!(actual, expected, "{axis:?} scroll={scroll}");
                previous = incoming;
                previous_scroll = scroll;
            }
        }
    }

    #[test]
    fn repeated_scrolls_keep_recovering_the_offset() {
        let mut previous = scrolling_fixture(0, 0);
        let mut estimator =
            VerticalMotionEstimator::new(previous.geometry(), MotionEstimatorOptions::default())
                .unwrap();
        for step in 1..=4 {
            let incoming = scrolling_fixture(step * 16, step as u8);
            let estimate = estimator.estimate(&previous, &previous, &incoming).unwrap();
            assert_eq!(
                estimate.outcome,
                crate::MotionOutcome::Motion { offset: -16 },
                "{estimate:#?}"
            );
            previous = incoming;
        }
    }

    fn unrelated_fixture(seed: u32) -> Frame {
        let mut pixels = Vec::with_capacity(320 * 224);
        for y in 0_u32..224 {
            for x in 0_u32..320 {
                let mut hash = x.wrapping_mul(0x9e37_79b9)
                    ^ y.wrapping_mul(0x85eb_ca6b)
                    ^ seed.wrapping_mul(0xc2b2_ae35);
                hash ^= hash >> 16;
                hash = hash.wrapping_mul(0x7feb_352d);
                pixels.push((hash >> 24) as u8);
            }
        }
        Frame::new(320, 224, PixelFormat::Gray8, pixels).unwrap()
    }

    #[test]
    fn repeated_scene_cuts_are_reported() {
        let mut previous = unrelated_fixture(0);
        let mut estimator =
            VerticalMotionEstimator::new(previous.geometry(), MotionEstimatorOptions::default())
                .unwrap();
        let mut last = None;
        for seed in 1..=3 {
            let incoming = unrelated_fixture(seed);
            let estimate = estimator.estimate(&previous, &previous, &incoming).unwrap();
            assert!(!matches!(
                estimate.outcome,
                crate::MotionOutcome::Motion { .. }
            ));
            previous = incoming;
            last = Some(estimate);
        }
        let last = last.unwrap();
        assert_eq!(last.diagnostics.stage, MotionStage::SceneCut, "{last:#?}");
    }
}
