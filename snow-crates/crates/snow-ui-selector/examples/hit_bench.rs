use std::thread;
use std::time::{Duration, Instant};

use snow_ui_selector::{AccessibilityBackend, ElementRegionService, HitTestMode};
use windows::Win32::Foundation::POINT;
use windows::Win32::UI::WindowsAndMessaging::GetCursorPos;
use windows::core::Result;

#[derive(Debug)]
struct Options {
    backend: AccessibilityBackend,
    mode: HitTestMode,
    samples: usize,
    interval_ms: u64,
    csv: bool,
    include_refresh: bool,
    point: Option<POINT>,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            backend: AccessibilityBackend::Msaa,
            mode: HitTestMode::UiElement,
            samples: 1,
            interval_ms: 250,
            csv: false,
            include_refresh: false,
            point: None,
        }
    }
}

fn main() -> Result<()> {
    let options = parse_options();
    let mut selector = ElementRegionService::with_backend(options.backend)?;

    if !options.include_refresh {
        selector.refresh()?;
    }

    if options.csv {
        println!(
            "sample,backend,mode,include_refresh,ok,elapsed_ms,mouse_x,mouse_y,rect_x,rect_y,rect_w,rect_h,rect_count,detail"
        );
    }

    for sample in 1..=options.samples {
        let point = match options.point {
            Some(point) => point,
            None => cursor_pos()?,
        };

        let started = Instant::now();
        let hit = if options.include_refresh {
            selector.refresh()?;
            selector.hit_test_point(point, options.mode)?
        } else {
            selector.hit_test_point(point, options.mode)?
        };
        let elapsed_ms = started.elapsed().as_secs_f64() * 1000.0;

        let rect_count = hit.as_ref().map_or(0, Vec::len);
        let first = hit.as_ref().and_then(|rects| rects.first());

        if options.csv {
            if let Some(rect) = first {
                println!(
                    "{sample},{},{},{},{},{elapsed_ms:.3},{},{},{},{},{},{},{},hit",
                    backend_name(options.backend),
                    mode_name(options.mode),
                    options.include_refresh as u8,
                    1,
                    point.x,
                    point.y,
                    rect.left(),
                    rect.top(),
                    rect.width(),
                    rect.height(),
                    rect_count
                );
            } else {
                println!(
                    "{sample},{},{},{},{},{elapsed_ms:.3},{},{},{},{},{},{},{},no-hit",
                    backend_name(options.backend),
                    mode_name(options.mode),
                    options.include_refresh as u8,
                    0,
                    point.x,
                    point.y,
                    0,
                    0,
                    0,
                    0,
                    rect_count
                );
            }
        } else if let Some(rect) = first {
            println!(
                "sample={sample} backend={} mode={} include_refresh={} ok=yes time={elapsed_ms:.3} ms",
                backend_name(options.backend),
                mode_name(options.mode),
                options.include_refresh
            );
            println!(
                "  mouse=({}, {}) rect=({}, {}, {}, {}) rect_count={rect_count}",
                point.x,
                point.y,
                rect.left(),
                rect.top(),
                rect.width(),
                rect.height()
            );
        } else {
            println!(
                "sample={sample} backend={} mode={} include_refresh={} ok=no time={elapsed_ms:.3} ms",
                backend_name(options.backend),
                mode_name(options.mode),
                options.include_refresh
            );
            println!("  mouse=({}, {}) no hit", point.x, point.y);
        }

        if sample != options.samples && options.interval_ms > 0 {
            thread::sleep(Duration::from_millis(options.interval_ms));
        }
    }

    Ok(())
}

fn cursor_pos() -> Result<POINT> {
    let mut point = POINT::default();
    unsafe {
        GetCursorPos(&mut point)?;
    }
    Ok(point)
}

fn backend_name(backend: AccessibilityBackend) -> &'static str {
    match backend {
        AccessibilityBackend::Uia => "uia",
        AccessibilityBackend::Msaa => "msaa",
    }
}

fn mode_name(mode: HitTestMode) -> &'static str {
    match mode {
        HitTestMode::UiElement => "element",
        HitTestMode::Window => "window",
    }
}

fn parse_options() -> Options {
    let mut options = Options::default();
    let mut args = std::env::args().skip(1);

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--backend" | "-b" => {
                let value = args
                    .next()
                    .unwrap_or_else(|| usage_exit("missing --backend value"));
                if value.eq_ignore_ascii_case("msaa") {
                    options.backend = AccessibilityBackend::Msaa;
                } else if value.eq_ignore_ascii_case("uia") {
                    options.backend = AccessibilityBackend::Uia;
                } else {
                    usage_exit("invalid --backend value");
                }
            }
            "--mode" | "-m" => {
                let value = args
                    .next()
                    .unwrap_or_else(|| usage_exit("missing --mode value"));
                if value.eq_ignore_ascii_case("element") {
                    options.mode = HitTestMode::UiElement;
                } else if value.eq_ignore_ascii_case("window") {
                    options.mode = HitTestMode::Window;
                } else {
                    usage_exit("invalid --mode value");
                }
            }
            "--samples" | "-n" => {
                let value = args
                    .next()
                    .unwrap_or_else(|| usage_exit("missing --samples value"));
                options.samples = parse_positive_usize(&value, "--samples");
            }
            "--interval-ms" => {
                let value = args
                    .next()
                    .unwrap_or_else(|| usage_exit("missing --interval-ms value"));
                options.interval_ms = value
                    .parse::<u64>()
                    .unwrap_or_else(|_| usage_exit("invalid --interval-ms value"));
            }
            "--point" => {
                let x = args
                    .next()
                    .unwrap_or_else(|| usage_exit("missing --point x value"));
                let y = args
                    .next()
                    .unwrap_or_else(|| usage_exit("missing --point y value"));
                options.point = Some(POINT {
                    x: x.parse::<i32>()
                        .unwrap_or_else(|_| usage_exit("invalid --point x value")),
                    y: y.parse::<i32>()
                        .unwrap_or_else(|_| usage_exit("invalid --point y value")),
                });
            }
            "--include-refresh" => options.include_refresh = true,
            "--csv" => options.csv = true,
            "--help" | "-h" => {
                print_usage();
                std::process::exit(0);
            }
            _ => usage_exit("unknown argument"),
        }
    }

    options
}

fn parse_positive_usize(value: &str, name: &str) -> usize {
    let parsed = value
        .parse::<usize>()
        .unwrap_or_else(|_| usage_exit(&format!("invalid {name} value")));
    if parsed == 0 {
        usage_exit(&format!("{name} must be greater than zero"));
    }
    parsed
}

fn usage_exit(message: &str) -> ! {
    eprintln!("{message}");
    print_usage();
    std::process::exit(2);
}

fn print_usage() {
    eprintln!(
        "Usage: hit_bench [--backend msaa|uia] [--mode element|window] [--samples N] [--interval-ms N] [--point X Y] [--include-refresh] [--csv]"
    );
}
