#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DeviceFlow {
    Render,
    Capture,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DeviceSelector {
    DefaultRender,
    DefaultCapture,
    Id(String),
}

impl DeviceSelector {
    pub fn is_default(&self) -> bool {
        matches!(self, Self::DefaultRender | Self::DefaultCapture)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AudioDeviceInfo {
    pub id: String,
    pub name: String,
    pub is_default: bool,
    pub is_active: bool,
    pub flow: DeviceFlow,
}
