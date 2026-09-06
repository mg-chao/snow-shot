//! Best-effort reversal of the Windows full-screen Magnifier color matrix.

use nalgebra::{Matrix3, Vector3};

/// Policy for capture paths that contain a full-screen Magnifier effect.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum ColorCorrection {
    #[default]
    Disabled,
    /// Sample the effect immediately before each capture operation.
    CurrentMagnifier,
    /// Share one sampled effect across all workers in a desktop snapshot.
    Snapshot(Option<ScreenColorTransform>),
}

impl ColorCorrection {
    pub fn snapshot_current() -> Self {
        Self::Snapshot(current_transform())
    }

    pub(crate) fn resolve(self) -> Option<ScreenColorTransform> {
        match self {
            Self::Disabled => None,
            Self::CurrentMagnifier => current_transform(),
            Self::Snapshot(transform) => transform,
        }
    }
}

/// Validated inverse RGB affine transform. Alpha is never modified.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ScreenColorTransform {
    pub(crate) rows: [[f32; 4]; 3],
    pub(crate) inverted: bool,
}

impl ScreenColorTransform {
    /// Accepts the row-major MAGCOLOREFFECT matrix (row-vector convention).
    /// Identity, unsupported alpha effects, and unstable inverses return None.
    pub fn from_magnifier_matrix(matrix: &[f32; 25]) -> Option<Self> {
        if matrix.iter().any(|value| !value.is_finite()) {
            return None;
        }
        for row in 0..5 {
            for column in 0..5 {
                if row == 3 || column >= 3 {
                    let expected = if row == column { 1.0 } else { 0.0 };
                    if matrix[row * 5 + column] != expected {
                        return None;
                    }
                }
            }
        }
        let linear = Matrix3::<f64>::from_fn(|r, c| f64::from(matrix[c * 5 + r]));
        let offset = Vector3::new(matrix[20] as f64, matrix[21] as f64, matrix[22] as f64);
        if linear == Matrix3::identity() && offset == Vector3::zeros() {
            return None;
        }
        let inverse = linear.try_inverse()?;
        let norm = |m: &Matrix3<f64>| (0..3).map(|r| m.row(r).abs().sum()).fold(0.0, f64::max);
        if norm(&linear) * norm(&inverse) > 10_000.0
            || norm(&(linear * inverse - Matrix3::identity())) > 1e-6
        {
            return None;
        }
        let translation = -(inverse * offset);
        let rows = std::array::from_fn(|r| {
            [
                inverse[(r, 0)] as f32,
                inverse[(r, 1)] as f32,
                inverse[(r, 2)] as f32,
                (translation[r] * 255.0) as f32,
            ]
        });
        if rows.iter().flatten().any(|v| !v.is_finite()) {
            return None;
        }
        Some(Self {
            inverted: rows
                == [
                    [-1., 0., 0., 255.],
                    [0., -1., 0., 255.],
                    [0., 0., -1., 255.],
                ],
            rows,
        })
    }
}

#[cfg(windows)]
fn current_transform() -> Option<ScreenColorTransform> {
    use std::cell::RefCell;
    use std::sync::Mutex;
    use windows::Win32::UI::Magnification::{
        MAGCOLOREFFECT, MagGetFullscreenColorEffect, MagInitialize, MagUninitialize,
    };

    static USERS: Mutex<usize> = Mutex::new(0);
    struct Reader {
        initialized: bool,
        matrix: Option<[f32; 25]>,
        transform: Option<ScreenColorTransform>,
    }
    impl Reader {
        fn read(&mut self) -> Option<ScreenColorTransform> {
            if !self.initialized {
                let mut users = USERS.lock().ok()?;
                if *users == 0 && !unsafe { MagInitialize() }.as_bool() {
                    return None;
                }
                *users += 1;
                self.initialized = true;
            }
            let mut effect = MAGCOLOREFFECT::default();
            if !unsafe { MagGetFullscreenColorEffect(&mut effect) }.as_bool() {
                self.matrix = None;
                self.transform = None;
                return None;
            }
            if self.matrix != Some(effect.transform) {
                self.transform = ScreenColorTransform::from_magnifier_matrix(&effect.transform);
                self.matrix = Some(effect.transform);
            }
            self.transform
        }
    }
    impl Drop for Reader {
        fn drop(&mut self) {
            if self.initialized
                && let Ok(mut users) = USERS.lock()
            {
                *users -= 1;
                if *users == 0 {
                    unsafe {
                        let _ = MagUninitialize();
                    }
                }
            }
        }
    }
    thread_local! {
        static READER: RefCell<Reader> = const { RefCell::new(Reader {
            initialized: false, matrix: None, transform: None,
        }) };
    }
    READER.with(|reader| reader.borrow_mut().read())
}

#[cfg(not(windows))]
fn current_transform() -> Option<ScreenColorTransform> {
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    fn identity() -> [f32; 25] {
        std::array::from_fn(|i| if i / 5 == i % 5 { 1.0 } else { 0.0 })
    }
    #[test]
    fn rejects_identity_singular_unstable_and_nonfinite() {
        assert!(ScreenColorTransform::from_magnifier_matrix(&identity()).is_none());
        for value in [0.0, 1e-8, f32::NAN, f32::INFINITY] {
            let mut m = identity();
            m[0] = value;
            assert!(ScreenColorTransform::from_magnifier_matrix(&m).is_none());
        }
        let mut m = identity();
        m[18] = 0.5;
        assert!(ScreenColorTransform::from_magnifier_matrix(&m).is_none());
    }
    #[test]
    fn prepares_exact_inversion() {
        let mut m = identity();
        for c in 0..3 {
            m[c * 6] = -1.0;
            m[20 + c] = 1.0;
        }
        assert!(
            ScreenColorTransform::from_magnifier_matrix(&m)
                .unwrap()
                .inverted
        );
    }
    #[test]
    fn reverses_channel_mixing_and_translation() {
        let mut m = identity();
        m[0] = 0.8;
        m[5] = 0.1;
        m[20] = 0.05;
        let inverse = ScreenColorTransform::from_magnifier_matrix(&m).unwrap();
        let original = [40., 80., 120.];
        let filtered = [40. * 0.8 + 80. * 0.1 + 0.05 * 255., 80., 120.];
        for (row, expected) in inverse.rows.iter().zip(original) {
            let actual =
                row[0] * filtered[0] + row[1] * filtered[1] + row[2] * filtered[2] + row[3];
            assert!((actual - expected).abs() < 0.001);
        }
    }
}
