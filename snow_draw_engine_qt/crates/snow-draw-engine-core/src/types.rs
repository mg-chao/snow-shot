use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ColorRgba8 {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct CornerRadii {
    pub top_left: f64,
    pub top_right: f64,
    pub bottom_right: f64,
    pub bottom_left: f64,
}

impl CornerRadii {
    pub const fn splat(radius: f64) -> Self {
        Self {
            top_left: radius,
            top_right: radius,
            bottom_right: radius,
            bottom_left: radius,
        }
    }

    pub fn scaled(self, scale: f64) -> Self {
        Self {
            top_left: self.top_left * scale,
            top_right: self.top_right * scale,
            bottom_right: self.bottom_right * scale,
            bottom_left: self.bottom_left * scale,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ErrorCode {
    #[default]
    InvalidArgument,
    NotFound,
    InvalidState,
    BufferTooSmall,
    StaleRevision,
    Unsupported,
    Internal,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub enum ZoomFocus {
    #[default]
    Pointer,
    Center,
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct SnapConfig {
    pub enabled: bool,
    pub distance: f64,
    pub enable_point_snaps: bool,
    pub enable_gap_snaps: bool,
    pub show_guides: bool,
    pub show_gap_size: bool,
    pub line_color: ColorRgba8,
    pub line_width: f64,
    pub marker_size: f64,
    pub gap_dash_length: f64,
    pub gap_dash_gap: f64,
}

impl SnapConfig {
    pub const DEFAULT_ENABLED: bool = false;
    pub const DEFAULT_DISTANCE: f64 = 8.0;
    pub const DEFAULT_ENABLE_POINT_SNAPS: bool = true;
    pub const DEFAULT_ENABLE_GAP_SNAPS: bool = true;
    pub const DEFAULT_SHOW_GUIDES: bool = true;
    pub const DEFAULT_SHOW_GAP_SIZE: bool = false;
    pub const DEFAULT_LINE_COLOR: ColorRgba8 = ColorRgba8 {
        r: 255,
        g: 107,
        b: 107,
        a: 255,
    };
    pub const DEFAULT_LINE_WIDTH: f64 = 1.0;
    pub const DEFAULT_MARKER_SIZE: f64 = 8.0;
    pub const DEFAULT_GAP_DASH_LENGTH: f64 = 4.0;
    pub const DEFAULT_GAP_DASH_GAP: f64 = 4.0;
}

impl Default for SnapConfig {
    fn default() -> Self {
        Self {
            enabled: Self::DEFAULT_ENABLED,
            distance: Self::DEFAULT_DISTANCE,
            enable_point_snaps: Self::DEFAULT_ENABLE_POINT_SNAPS,
            enable_gap_snaps: Self::DEFAULT_ENABLE_GAP_SNAPS,
            show_guides: Self::DEFAULT_SHOW_GUIDES,
            show_gap_size: Self::DEFAULT_SHOW_GAP_SIZE,
            line_color: Self::DEFAULT_LINE_COLOR,
            line_width: Self::DEFAULT_LINE_WIDTH,
            marker_size: Self::DEFAULT_MARKER_SIZE,
            gap_dash_length: Self::DEFAULT_GAP_DASH_LENGTH,
            gap_dash_gap: Self::DEFAULT_GAP_DASH_GAP,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct GridConfig {
    pub enabled: bool,
    pub size: f64,
}

impl GridConfig {
    pub const DEFAULT_ENABLED: bool = false;
    pub const DEFAULT_SIZE: f64 = 20.0;
    pub const MIN_SIZE: f64 = 5.0;
    pub const MAX_SIZE: f64 = 100.0;
}

impl Default for GridConfig {
    fn default() -> Self {
        Self {
            enabled: Self::DEFAULT_ENABLED,
            size: Self::DEFAULT_SIZE,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct EngineConfig {
    pub min_zoom: f64,
    pub max_zoom: f64,
    pub zoom_focus: ZoomFocus,
    pub wheel_zoom_sensitivity: f64,
    pub clear_color: ColorRgba8,
    pub snap: SnapConfig,
    pub grid: GridConfig,
    pub enable_pointer_capture: bool,
}

impl Default for EngineConfig {
    fn default() -> Self {
        Self {
            min_zoom: 0.1,
            max_zoom: 8.0,
            zoom_focus: ZoomFocus::Pointer,
            wheel_zoom_sensitivity: 0.001,
            clear_color: ColorRgba8 {
                r: 255,
                g: 255,
                b: 255,
                a: 255,
            },
            snap: SnapConfig::default(),
            grid: GridConfig::default(),
            enable_pointer_capture: true,
        }
    }
}

pub fn validate_config(config: &EngineConfig) -> Result<(), ErrorCode> {
    let valid_zoom_range = config.min_zoom.is_finite()
        && config.max_zoom.is_finite()
        && config.min_zoom > 0.0
        && config.max_zoom >= config.min_zoom;
    if !valid_zoom_range {
        return Err(ErrorCode::InvalidArgument);
    }

    if !config.wheel_zoom_sensitivity.is_finite() || config.wheel_zoom_sensitivity < 0.0 {
        return Err(ErrorCode::InvalidArgument);
    }

    if !config.snap.distance.is_finite() || config.snap.distance < 0.0 {
        return Err(ErrorCode::InvalidArgument);
    }

    if !config.snap.line_width.is_finite() || config.snap.line_width <= 0.0 {
        return Err(ErrorCode::InvalidArgument);
    }

    if !config.snap.marker_size.is_finite() || config.snap.marker_size < 0.0 {
        return Err(ErrorCode::InvalidArgument);
    }

    if !config.snap.gap_dash_length.is_finite() || config.snap.gap_dash_length < 0.0 {
        return Err(ErrorCode::InvalidArgument);
    }

    if !config.snap.gap_dash_gap.is_finite() || config.snap.gap_dash_gap < 0.0 {
        return Err(ErrorCode::InvalidArgument);
    }

    let valid_grid_size = config.grid.size.is_finite()
        && config.grid.size >= GridConfig::MIN_SIZE
        && config.grid.size <= GridConfig::MAX_SIZE;
    if !valid_grid_size {
        return Err(ErrorCode::InvalidArgument);
    }

    Ok(())
}
