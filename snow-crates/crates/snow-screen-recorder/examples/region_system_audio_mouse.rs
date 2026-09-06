use std::thread;
use std::time::Duration;

use snow_screen_recorder::{
    EditingSession, ExportFormat, MouseEditConfig, RecordingAudioConfig, RecordingAudioTrackConfig,
    RecordingConfig, RecordingRegion, RecordingSession, RecordingTarget, VideoEncodeConfig,
    VideoEncodingSpeed,
};

const REGION_X: i32 = 0;
const REGION_Y: i32 = 0;
const REGION_WIDTH: u32 = 2000;
const REGION_HEIGHT: u32 = 2000;
const TARGET_FPS: u32 = 60;
const RECORD_SECONDS: u64 = 3;
const PAUSE_SECONDS: u64 = 3;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let output_dir = std::env::current_dir()?.join("recordings");
    let export_path = output_dir.join("region_0_0_2000_2000_fps24.mp4");

    let region = RecordingRegion::new(REGION_X, REGION_Y, REGION_WIDTH, REGION_HEIGHT);
    let recording_config = RecordingConfig {
        target: RecordingTarget::Region(region),
        output_dir: output_dir.clone(),
        fps: TARGET_FPS,
        video: VideoEncodeConfig {
            quality: 100,
            speed: VideoEncodingSpeed::UltraFast,
        },
        audio: RecordingAudioConfig {
            tracks: vec![RecordingAudioTrackConfig::system_default("system")],
            ..RecordingAudioConfig::default()
        },
        keep_temp_files: true,
        ..RecordingConfig::default()
    };

    let mut recording = RecordingSession::create(recording_config)?;
    recording.start()?;

    println!(
        "Recording region ({}, {})-({}, {}) at {} FPS: record {}s -> pause {}s -> record {}s...",
        REGION_X,
        REGION_Y,
        REGION_X + REGION_WIDTH as i32,
        REGION_Y + REGION_HEIGHT as i32,
        TARGET_FPS,
        RECORD_SECONDS,
        PAUSE_SECONDS,
        RECORD_SECONDS
    );
    thread::sleep(Duration::from_secs(RECORD_SECONDS));
    println!("Pausing recording for {} seconds...", PAUSE_SECONDS);
    recording.pause()?;
    thread::sleep(Duration::from_secs(PAUSE_SECONDS));
    println!("Resuming recording for {} seconds...", RECORD_SECONDS);
    recording.resume()?;
    thread::sleep(Duration::from_secs(RECORD_SECONDS));

    let stop_duration_start = std::time::Instant::now();
    let artifact = recording.stop()?;
    println!(
        "Recording stopped in {} ms",
        stop_duration_start.elapsed().as_millis()
    );

    let editing = EditingSession::open(artifact)?;
    let mut request = editing.export_request();
    if let Some(track) = request
        .audio_tracks
        .iter_mut()
        .find(|track| track.track_id == "system")
    {
        track.enabled = true;
    }
    request.mouse = MouseEditConfig {
        visible: true,
        trail_enabled: true,
        click_enabled: true,
        ..MouseEditConfig::default()
    };
    request.format = ExportFormat::Mp4;
    request.output_path = export_path.clone();
    request.video = VideoEncodeConfig {
        quality: 100,
        speed: VideoEncodingSpeed::UltraFast,
    };

    let start_ts = std::time::Instant::now();
    let result = editing.export(request)?;
    println!(
        "Export completed in {} seconds.",
        start_ts.elapsed().as_secs_f64()
    );

    println!(
        "Export finished: {} (duration: {} ms)",
        result.output_path.display(),
        result.duration_ms
    );

    Ok(())
}
