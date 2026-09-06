//! Allocation-free panic context shared by the GUI FFI and the OCR process.
use std::fmt::{self, Write};
use std::sync::Once;

pub type PanicCallback = extern "C" fn(*const u8, usize);
static INSTALL: Once = Once::new();

struct LocationBuffer {
    bytes: [u8; 1024],
    len: usize,
}

impl Write for LocationBuffer {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let mut length = text.len().min(self.bytes.len() - self.len);
        while !text.is_char_boundary(length) {
            length -= 1;
        }
        self.bytes[self.len..self.len + length].copy_from_slice(&text.as_bytes()[..length]);
        self.len += length;
        Ok(())
    }
}

/// Installs once, before worker threads start. Payloads are intentionally omitted:
/// panic messages can contain OCR content, credentials, or arbitrary user input.
pub fn install_panic_hook(callback: PanicCallback) {
    INSTALL.call_once(|| {
        std::panic::set_hook(Box::new(move |info| {
            let mut context = LocationBuffer {
                bytes: [0; 1024],
                len: 0,
            };
            if let Some(location) = info.location() {
                let file = location
                    .file()
                    .rsplit(['/', '\\'])
                    .next()
                    .unwrap_or("unknown");
                let _ = write!(
                    &mut context,
                    "rust.panic {file}:{}:{}",
                    location.line(),
                    location.column()
                );
            } else {
                let _ = context.write_str("rust.panic unknown location");
            }
            callback(context.bytes.as_ptr(), context.len);
        }));
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bounded_location_remains_utf8() {
        let mut buffer = LocationBuffer {
            bytes: [0; 1024],
            len: 0,
        };
        buffer.write_str(&"x".repeat(1023)).unwrap();
        buffer.write_str("\u{4e2d}").unwrap();
        assert_eq!(buffer.len, 1023);
        assert!(std::str::from_utf8(&buffer.bytes[..buffer.len]).is_ok());
    }
}
