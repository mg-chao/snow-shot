use std::{cell::OnceCell, collections::BTreeMap};

use rayon::prelude::*;

use super::{
    Fit, FitGrid, Frame, MIN_INLIER_TILES, MIN_RESIDUAL_GAIN, MIN_SUPPORTED_BANDS,
    OverlapResiduals, Pair, Registration, StitchAxis, View, candidate_shifts, exposure,
    full_overlap::ChangesByBias, pixel_proposals,
};

struct OffsetFit {
    offset: i32,
    bias: [i16; 3],
    proposal: Fit,
    gradient: OnceCell<Fit>,
    full: OnceCell<OverlapResiduals>,
    verification: OnceCell<Option<Verification>>,
}

#[derive(Clone, Copy)]
struct Verification {
    confidence: f32,
    error: f32,
    blocks: u32,
}

impl OffsetFit {
    fn new(pair: &Pair, offset: i32) -> Option<Self> {
        let bias = exposure(&pair.reference, &pair.incoming, offset);
        let proposal = pair.fit::<false>(offset, bias, FitGrid::Proposal);
        if proposal.blocks < MIN_INLIER_TILES || 1.0 - proposal.overlap.loss < MIN_RESIDUAL_GAIN {
            return None;
        }
        Some(Self {
            offset,
            bias,
            proposal,
            gradient: OnceCell::new(),
            full: OnceCell::new(),
            verification: OnceCell::new(),
        })
    }

    fn gradient(&self, pair: &Pair) -> &Fit {
        self.gradient
            .get_or_init(|| pair.fit::<true>(self.offset, [0; 3], FitGrid::Proposal))
    }

    fn full(&self, pair: &Pair, changes: &mut ChangesByBias) -> &OverlapResiduals {
        self.full
            .get_or_init(|| pair.full_overlap(self.offset, self.bias, changes))
    }

    fn compare(
        &self,
        other: &Self,
        pair: &Pair,
        changes: &mut ChangesByBias,
        threshold: f32,
    ) -> f32 {
        // A short intersection can lie entirely inside a repaint. First rule out a
        // hypothesis contradicted across its own overlap, even after dropping the worst band.
        let absolute = self
            .proposal
            .overlap
            .separate_preference(&other.proposal.overlap);
        if absolute.abs() >= threshold {
            return absolute;
        }
        let a = self.gradient(pair);
        let b = other.gradient(pair);
        if a.blocks >= MIN_INLIER_TILES && b.blocks >= MIN_INLIER_TILES {
            // Axial gradients also work across exposure changes in a composed reference.
            let gradient = a.overlap.separate_preference(&b.overlap);
            if gradient.abs() >= threshold {
                return gradient;
            }
        }
        let shared = self.proposal.overlap.preference(&other.proposal.overlap);
        if shared.abs() >= threshold {
            return shared;
        }
        let native = self
            .full(pair, changes)
            .compare(other.full(pair, changes), threshold);
        if native.abs() >= threshold {
            native
        } else {
            a.overlap.preference(&b.overlap)
        }
    }

    fn verify(&self, pair: &Pair, threshold: f32) -> Option<Verification> {
        *self.verification.get_or_init(|| {
            // The winner must also explain a wider, independent grid along the scroll
            // direction. Several adjacent tiles may all belong to one moving widget.
            let fit = pair.fit::<false>(self.offset, self.bias, FitGrid::Verification);
            let confidence = 1.0 - fit.overlap.loss;
            if fit.blocks < MIN_INLIER_TILES
                || fit.supported_bands < MIN_SUPPORTED_BANDS
                || confidence < threshold
                || !(fit.dominant
                    || pair
                        .fit::<true>(self.offset, [0; 3], FitGrid::Verification)
                        .dominant)
            {
                return None;
            }
            Some(Verification {
                confidence,
                error: fit.error,
                blocks: fit.blocks,
            })
        })
    }
}

// Feature extraction can be retried at a different resolution. Pixels, exposure and
// the shift limit belong to this frame pair, while eligibility is evaluated per pass.
pub(crate) struct RegistrationSearch<'a> {
    pair: Pair<'a>,
    limit: i32,
    threshold: f32,
    pixel_offsets: Vec<i32>,
    offsets: BTreeMap<i32, Option<OffsetFit>>,
    changes: ChangesByBias,
    repeats: BTreeMap<usize, bool>,
}

impl<'a> RegistrationSearch<'a> {
    pub(crate) fn new(
        reference: &'a Frame,
        previous: &'a Frame,
        incoming: &'a Frame,
        axis: StitchAxis,
        maximum_shift: i32,
        min_confidence: f32,
    ) -> Self {
        let pair = Pair {
            reference: View::new(reference, axis),
            raw: View::new(previous, axis),
            incoming: View::new(incoming, axis),
        };
        let limit = maximum_shift.min(pair.incoming.height.saturating_sub(1) as i32);
        let pixel_offsets = pixel_proposals(&pair.reference, &pair.incoming, limit);
        Self {
            pair,
            limit,
            threshold: min_confidence.max(MIN_RESIDUAL_GAIN),
            pixel_offsets,
            offsets: BTreeMap::new(),
            changes: ChangesByBias::new(),
            repeats: BTreeMap::new(),
        }
    }

    pub(crate) fn register(&mut self, features: &[i32]) -> Registration {
        let pair = &self.pair;
        let threshold = self.threshold;
        let active = candidate_shifts(&self.pixel_offsets, features, self.limit);
        let missing: Vec<_> = active
            .iter()
            .copied()
            .filter(|d| !self.offsets.contains_key(d))
            .collect();
        self.offsets.extend(
            missing
                .into_par_iter()
                .with_min_len(2)
                .map(|d| (d, OffsetFit::new(pair, d)))
                .collect::<Vec<_>>(),
        );

        // A feature-only candidate from the first pass may disappear on retry.
        // Keep its cached evidence out of both the election and the ambiguity checks.
        let mut fits: Vec<_> = active
            .iter()
            .filter_map(|d| self.offsets[d].as_ref())
            .collect();
        let eligible = |fit: &OffsetFit| 1.0 - fit.proposal.overlap.loss >= threshold;
        if !fits.iter().any(|fit| eligible(fit)) {
            return Registration::Unavailable;
        }
        fits.sort_unstable_by(|a, b| {
            a.proposal
                .overlap
                .loss
                .total_cmp(&b.proposal.overlap.loss)
                .then(a.offset.cmp(&b.offset))
        });
        let mut best = fits[0];
        for &fit in fits.iter().skip(1).filter(|fit| eligible(fit)) {
            if fit.compare(best, pair, &mut self.changes, threshold) >= threshold {
                best = fit;
            }
        }
        if !best.proposal.dominant && !best.gradient(pair).dominant {
            return Registration::Unavailable;
        }
        let mut confidence = 1.0 - best.proposal.overlap.loss;
        let mut ambiguous = Vec::new();
        for fit in fits.into_iter().filter(|fit| fit.offset != best.offset) {
            let delta = best.offset.abs_diff(fit.offset) as usize;
            // Even a weaker proposal can give identical predictions on a repeating input.
            if *self.repeats.entry(delta).or_insert_with(|| {
                pair.reference.repeats_at(delta) || pair.incoming.repeats_at(delta)
            }) {
                ambiguous.push(fit.offset);
            } else if eligible(fit) {
                let separation = best.compare(fit, pair, &mut self.changes, threshold);
                confidence = confidence.min(separation);
                if separation < threshold {
                    ambiguous.push(fit.offset);
                }
            }
        }
        if !ambiguous.is_empty() {
            ambiguous.push(best.offset);
            return Registration::Ambiguous(ambiguous);
        }
        let Some(verified) = best.verify(pair, threshold) else {
            return Registration::Unavailable;
        };
        Registration::Verified {
            offset: best.offset,
            confidence: confidence.min(verified.confidence),
            error: verified.error,
            blocks: verified.blocks,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::PixelFormat;

    #[test]
    fn retry_uses_only_current_feature_proposals() {
        // The alternating one-level repaint breaks gradient signatures while preserving
        // a strong pixel fit at the true offset. Only a feature proposal can introduce it.
        let frame = |shift: u32| {
            let (width, height) = (96_u32, 128_u32);
            let pixels = (0..height)
                .flat_map(|y| {
                    (0..width).map(move |x| {
                        let seed =
                            (y + shift).wrapping_mul(0x27d4_eb2d) ^ x.wrapping_mul(0x85eb_ca6b_u32);
                        let value =
                            ((seed ^ (seed >> 15)).wrapping_mul(0xc2b2_ae35) >> 24) as u8 & 0xfe;
                        value + u8::from(shift != 0 && y % 2 == 0)
                    })
                })
                .collect();
            Frame::new(width, height, PixelFormat::Gray8, pixels).unwrap()
        };
        let reference = frame(0);
        let incoming = frame(17);
        let new_search = || {
            RegistrationSearch::new(
                &reference,
                &reference,
                &incoming,
                StitchAxis::Vertical,
                64,
                0.65,
            )
        };
        let mut retry = new_search();
        for features in [&[30][..], &[-17, 30][..], &[30][..], &[-17][..]] {
            let decision = retry.register(features);
            assert_eq!(decision, new_search().register(features));
            if features.contains(&-17) {
                assert!(matches!(
                    decision,
                    Registration::Verified { offset: -17, .. }
                ));
            } else {
                assert_eq!(decision, Registration::Unavailable);
            }
        }
    }
}
