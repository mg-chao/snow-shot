use crate::error::{AudioError, AudioResult};

/// Maximum number of audio channels supported by this crate.
pub const MAX_CHANNELS: u16 = 32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AudioFormat {
    pub sample_rate: u32,
    pub channels: u16,
}

impl AudioFormat {
    pub const fn new(sample_rate: u32, channels: u16) -> Self {
        Self {
            sample_rate,
            channels,
        }
    }

    pub fn validate(&self) -> AudioResult<()> {
        if self.sample_rate == 0 {
            return Err(AudioError::InvalidConfig(
                "sample rate must be greater than zero".into(),
            ));
        }
        if self.channels == 0 {
            return Err(AudioError::InvalidConfig(
                "channel count must be greater than zero".into(),
            ));
        }
        if self.channels > MAX_CHANNELS {
            return Err(AudioError::InvalidConfig(format!(
                "channel count above {MAX_CHANNELS} is not supported"
            )));
        }
        Ok(())
    }

    pub fn samples_for_frames(&self, frames: u32) -> AudioResult<usize> {
        self.validate()?;
        usize::from(self.channels)
            .checked_mul(frames as usize)
            .ok_or(AudioError::BufferOverflow)
    }

    pub fn bytes_per_frame(&self) -> AudioResult<usize> {
        usize::from(self.channels)
            .checked_mul(std::mem::size_of::<i16>())
            .ok_or(AudioError::BufferOverflow)
    }

    pub fn bytes_for_frames(&self, frames: u32) -> AudioResult<usize> {
        let frame_bytes = self.bytes_per_frame()?;
        frame_bytes
            .checked_mul(frames as usize)
            .ok_or(AudioError::BufferOverflow)
    }
}

impl Default for AudioFormat {
    fn default() -> Self {
        Self {
            sample_rate: 48_000,
            channels: 2,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validation_rejects_invalid_inputs() {
        assert!(AudioFormat::new(0, 2).validate().is_err());
        assert!(AudioFormat::new(48_000, 0).validate().is_err());
        assert!(AudioFormat::new(48_000, 64).validate().is_err());
    }

    #[test]
    fn sample_and_byte_size_computation_matches_expectation() {
        let fmt = AudioFormat::new(48_000, 2);
        assert_eq!(fmt.samples_for_frames(480).unwrap(), 960);
        assert_eq!(fmt.bytes_per_frame().unwrap(), 4);
        assert_eq!(fmt.bytes_for_frames(480).unwrap(), 1_920);
    }
}
