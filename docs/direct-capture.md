# Direct capture contract

Focused-window and current-monitor commands belong to the application-owned
DirectCaptureController, independently of ScreenshotController. Priorities are
correct target pixels > isolated state ownership > testability > latency.

Each command snapshots its target identity and output settings at invocation.
The FIFO processes every request, including capture, optional file output,
clipboard publication, and optional history publication. Pixels are acquired
when a request reaches the head of the queue. Target disappearance fails that
request, without selecting a replacement. Failures advance the queue.

The native target is the foreground root HWND for focused-window capture and
the native monitor device name under the cursor for current-monitor capture.
Qt's friendly screen name is not a native device identity.

Window capture uses WGC, DXGI, then GDI. Monitor capture uses DXGI, WGC, then GDI.
The native library controls eligible fallback and one-shot resource release.
Only the requested target is acquired. The normal-screenshot API preference and
Snow Shot window visibility/exclusion policies are not consulted.

Images are unstyled and retain physical resolution. Image copy continues when
automatic saving fails; file copy requires a saved file. History follows a
successful clipboard publication. Each request finishes its outputs before the
next request starts. Clipboard writes retain the shared publication service's
retry/supersession behavior for competing non-direct clipboard operations.

The controller owns a dedicated worker thread for acquisition, file writes,
clipboard image preparation, and history publication. Clipboard commits and
workflow transitions stay on the application thread. Shutdown clears pending
requests, rejects late completions, cancels the clipboard commit, and joins
the worker. A native operation already in progress is allowed to return before
the thread exits; shutdown does not forcibly terminate native capture.

Image history uses the existing version-1 storage layout with an optional
content_kind=image discriminator. Absent discriminators mean screenshot-session
history. The history adapter supplies a single raster, full-image selection,
and canonical empty drawing history without reading an active editor. Existing
records need no migration. New image history restores pixels and selection at
the current canvas origin; normal history keeps display matching behavior.

The regular screenshot Silent mode, focused-window compound request fields in
C++, automatic selection code, and selection-export source override are removed.
Existing exported compound C functions remain compatible for external callers.

Verification uses deterministic direct-workflow and history tests, related
regular capture/export tests, and native target/lease tests. The standalone
workflow test target links only Qt Gui, so it cannot depend on selection,
overlay, selector, or drawing implementations. Runtime verification should
exercise WGC window capture, DXGI monitor capture, mixed-DPI monitor layouts,
and capture during an active editor session. No rollout flag or data migration
is needed; reverting the application routing and implementation restores the
previous behavior without deleting history.

## Behavior inventory

| Classification | Contract |
| --- | --- |
| Preserve | Existing image/file clipboard modes, automatic file naming and save settings, history source labels, and shutter notification. |
| Preserve | Normal screenshot selection, styling, overlays, and export behavior; existing history files and exported compound C APIs remain readable/usable. |
| Change | Acquire only the requested target, independent of the regular screenshot controller, API preference, selector, overlays, and active editor/recording state. |
| Change | Publish unstyled physical-resolution pixels with isolated image history, without consulting the active drawing document. |
| Change | Process every request in invocation order, snapshot targets/settings, and acquire pixels only at the FIFO head. |
| Unexercised | Full interactive coexistence with editing/recording, mixed-DPI display combinations, elevated/protected windows, and display hot-plug during real acquisition. |

The retained regular capture/export tests constrain preserved behavior. Tests
that required Silent mode, compound C++ focused-window fields, or an export
source-image override were intentionally removed with those obsolete paths.
The new workflow tests instead constrain observable request/output ordering,
raw pixels, settings snapshots, save/clipboard/history failures, rejected
operations, burst requests, late/duplicate callbacks, and receiver destruction.
A shutdown-during-notification test first reproduced an empty-queue access;
the completion handlers now recheck lifecycle state after notifications.

## Verification record

Verified on Windows with the `windows-msvc-debug` preset:

- `snow_shot` and all affected test targets build successfully with strict compiler warnings.
- Eight targeted CTest executables pass: direct-capture-workflow, direct-capture-frame,
  capture-workflow, capture-history-repository, screenshot-history,
  screenshot-export-service, selection-export-workflow, and system-tray-controller.
- History tests cover raw-image persistence across restart, canvas-origin restoration,
  publication during asynchronous navigation, and restoration of the live editor endpoint.
- `cargo test -p snow-capture-c --lib`: 34 passed.
- `cargo test -p snow-capture --lib platform::windows::tests`: 5 passed, covering
  backend priorities, eligible fallback, terminal errors, and resource release.
- `cargo test -p snow-capture --lib one_shot_capture`: 3 passed, covering owned
  frames, reusable prepared state, and access release after success/failure.
- `cargo clippy -p snow-capture-c --lib --tests -- -D warnings` and the touched
  Rust file's rustfmt check pass. New C++ files pass clang-format; existing files
  were formatted only in changed regions. `git diff --check` passes.
- Translation extraction was rerun and all three Snow Shot catalogs compiled
  with zero unfinished translations. Preexisting unfinished palette/API-mode
  entries were completed because catalog compilation requires that invariant.

The opt-in `snow-shot-direct-capture-native-smoke` executable, run with
`--run-native --require-preferred-backends`, captured a 544x407 window via WGC
and its 3840x2160 monitor via DXGI. Both images contained the fixture pixels
after their native sessions had been destroyed. An earlier run acquired the
monitor through WGC fallback; the normal smoke mode permits valid fallback and
reports the concrete backends, while the optional strict flag requires the
preferred pair. Backend ordering is also covered deterministically in Rust.

No full CTest suite, Cargo workspace suite, or performance benchmark was run.
The build emitted existing Cargo incremental-cache access and FFmpeg DLL-copy
warnings, but the affected builds, tests, and Clippy invocation completed.
