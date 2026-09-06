use std::sync::mpsc;
use std::thread;

use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::w;

pub struct NativeFixture {
    hwnd: isize,
    worker: Option<thread::JoinHandle<()>>,
}

impl NativeFixture {
    pub fn start() -> Self {
        let (sender, receiver) = mpsc::sync_channel(1);
        let worker = thread::spawn(move || unsafe {
            let instance = GetModuleHandleW(None).unwrap();
            let class = WNDCLASSW {
                lpfnWndProc: Some(window_proc),
                hInstance: instance.into(),
                lpszClassName: w!("SnowUiaBenchmarkFixture"),
                ..Default::default()
            };
            assert_ne!(RegisterClassW(&class), 0);
            let hwnd = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                class.lpszClassName,
                w!("UIA benchmark fixture"),
                WS_POPUP | WS_VISIBLE,
                60,
                60,
                500,
                320,
                None,
                None,
                Some(instance.into()),
                None,
            )
            .unwrap();
            for (left, text) in [(20, w!("Left control")), (260, w!("Right control"))] {
                CreateWindowExW(
                    WINDOW_EX_STYLE::default(),
                    w!("BUTTON"),
                    text,
                    WS_CHILD | WS_VISIBLE,
                    left,
                    40,
                    200,
                    60,
                    Some(hwnd),
                    None,
                    Some(instance.into()),
                    None,
                )
                .unwrap();
            }
            sender.send(hwnd.0 as isize).unwrap();
            let mut message = MSG::default();
            while GetMessageW(&mut message, None, 0, 0).as_bool() {
                let _ = TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        });
        Self {
            hwnd: receiver.recv().unwrap(),
            worker: Some(worker),
        }
    }
}

impl Drop for NativeFixture {
    fn drop(&mut self) {
        unsafe {
            let _ = PostMessageW(
                Some(HWND(self.hwnd as *mut _)),
                WM_CLOSE,
                WPARAM(0),
                LPARAM(0),
            );
        }
        if let Some(worker) = self.worker.take() {
            worker.join().unwrap();
        }
    }
}

unsafe extern "system" fn window_proc(
    hwnd: HWND,
    message: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    if message == WM_DESTROY {
        unsafe {
            PostQuitMessage(0);
        }
        return LRESULT(0);
    }
    unsafe { DefWindowProcW(hwnd, message, wparam, lparam) }
}
