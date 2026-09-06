use snow_draw_engine::ErrorCode;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SnowError {
    Ok = 0,
    InvalidArgument = 1,
    NotFound = 2,
    InvalidState = 3,
    BufferTooSmall = 4,
    StaleRevision = 5,
    Unsupported = 6,
    Internal = 7,
}

impl From<ErrorCode> for SnowError {
    fn from(value: ErrorCode) -> Self {
        match value {
            ErrorCode::InvalidArgument => SnowError::InvalidArgument,
            ErrorCode::NotFound => SnowError::NotFound,
            ErrorCode::InvalidState => SnowError::InvalidState,
            ErrorCode::BufferTooSmall => SnowError::BufferTooSmall,
            ErrorCode::StaleRevision => SnowError::StaleRevision,
            ErrorCode::Unsupported => SnowError::Unsupported,
            ErrorCode::Internal => SnowError::Internal,
        }
    }
}
