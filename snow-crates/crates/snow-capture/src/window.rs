#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub(crate) struct WindowKey {
    pub(crate) handle: isize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct WindowId {
    handle: isize,
}

impl WindowId {
    pub const fn from_raw_handle(raw_handle: isize) -> Self {
        Self { handle: raw_handle }
    }

    pub const fn raw_handle(&self) -> isize {
        self.handle
    }

    pub fn stable_id(&self) -> String {
        format!("{:016x}", self.handle as usize as u64)
    }

    pub(crate) const fn key(&self) -> WindowKey {
        WindowKey {
            handle: self.handle,
        }
    }
}
