use crate::{StitchBranch, StitchError};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ViewportState {
    pub position: i64,
    pub max_position: i64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ViewportTransition {
    pub previous: ViewportState,
    pub candidate_position: i64,
    pub next: ViewportState,
    pub branch: StitchBranch,
    pub growth: u32,
}

impl ViewportState {
    pub fn transition(self, offset: i32) -> Result<ViewportTransition, StitchError> {
        let candidate =
            self.position
                .checked_sub(i64::from(offset))
                .ok_or(StitchError::Arithmetic {
                    operation: "calculating candidate viewport position",
                })?;

        let (next, branch, growth) = if candidate < 0 {
            let growth_i64 = candidate.checked_neg().ok_or(StitchError::Arithmetic {
                operation: "calculating prepend growth",
            })?;
            let max_position =
                self.max_position
                    .checked_add(growth_i64)
                    .ok_or(StitchError::Arithmetic {
                        operation: "rebasing viewport extent after prepend",
                    })?;
            (
                Self {
                    position: 0,
                    max_position,
                },
                StitchBranch::Prepend,
                u32::try_from(growth_i64).map_err(|_| StitchError::Arithmetic {
                    operation: "converting prepend growth",
                })?,
            )
        } else if candidate > self.max_position {
            let growth_i64 =
                candidate
                    .checked_sub(self.max_position)
                    .ok_or(StitchError::Arithmetic {
                        operation: "calculating append growth",
                    })?;
            (
                Self {
                    position: candidate,
                    max_position: candidate,
                },
                StitchBranch::Append,
                u32::try_from(growth_i64).map_err(|_| StitchError::Arithmetic {
                    operation: "converting append growth",
                })?,
            )
        } else {
            (
                Self {
                    position: candidate,
                    max_position: self.max_position,
                },
                StitchBranch::Contained,
                0,
            )
        };

        debug_assert!(next.position >= 0);
        debug_assert!(next.position <= next.max_position);
        Ok(ViewportTransition {
            previous: self,
            candidate_position: candidate,
            next,
            branch,
            growth,
        })
    }

    pub fn canvas_height(self, viewport_height: u32) -> Result<u32, StitchError> {
        let extent = u32::try_from(self.max_position).map_err(|_| StitchError::Arithmetic {
            operation: "converting viewport extent",
        })?;
        viewport_height
            .checked_add(extent)
            .ok_or(StitchError::Arithmetic {
                operation: "calculating canvas height",
            })
    }
}

#[cfg(test)]
mod tests {
    use proptest::prelude::*;

    use super::*;

    #[test]
    fn takes_each_normative_branch() {
        let initial = ViewportState::default();
        let appended = initial.transition(-4).unwrap();
        assert_eq!(appended.branch, StitchBranch::Append);
        assert_eq!(appended.growth, 4);
        assert_eq!(appended.next.position, 4);
        assert_eq!(appended.next.max_position, 4);

        let contained = appended.next.transition(2).unwrap();
        assert_eq!(contained.branch, StitchBranch::Contained);
        assert_eq!(contained.next.position, 2);
        assert_eq!(contained.next.max_position, 4);

        let prepended = contained.next.transition(5).unwrap();
        assert_eq!(prepended.branch, StitchBranch::Prepend);
        assert_eq!(prepended.growth, 3);
        assert_eq!(prepended.next.position, 0);
        assert_eq!(prepended.next.max_position, 7);
    }

    proptest! {
        #[test]
        fn legal_sequences_preserve_state_invariants(offsets in prop::collection::vec(-600_i32..=600, 0..200)) {
            let mut state = ViewportState::default();
            let mut old_height = 1000_u32;
            for offset in offsets.into_iter().filter(|offset| *offset != 0) {
                let transition = state.transition(offset).unwrap();
                state = transition.next;
                let height = state.canvas_height(1000).unwrap();
                prop_assert!(state.position >= 0);
                prop_assert!(state.position <= state.max_position);
                prop_assert!(height >= old_height);
                prop_assert_eq!(height, 1000 + u32::try_from(state.max_position).unwrap());
                old_height = height;
            }
        }
    }
}
