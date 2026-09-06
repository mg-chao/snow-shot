//! Test-only entry points, linked exclusively into isolated crash fixtures.
#[path = "../../snow-ocr-process/src/diagnostics.rs"]
mod ocr_diagnostics;

#[unsafe(no_mangle)]
pub extern "C" fn snow_test_rust_panic(callback: snow_diagnostics::PanicCallback) {
    snow_diagnostics::install_panic_hook(callback);
    panic!("private panic payload must not be logged");
}

#[unsafe(no_mangle)]
pub extern "C" fn snow_test_ocr_initialize() {
    ocr_diagnostics::initialize();
    ocr_diagnostics::operation_started(1);
}
