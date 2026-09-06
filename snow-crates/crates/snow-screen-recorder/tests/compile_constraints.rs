//! Integration tests for the facade crate's public surface.

use snow_screen_recorder::{
    EditingSession, ExportRequest, IntermediateRecordingProfile, RecordingArtifact,
    RecordingAudioTrackConfig, RecordingAudioTrackSource, RecordingConfig, RecordingSession,
    VideoEncodeConfig,
};

#[test]
fn facade_reexports_recording_runtime_types() {
    fn _assert_config(_: &RecordingConfig) {}
    fn _assert_session(_: &RecordingSession) {}
}

#[test]
fn facade_reexports_recording_model_types() {
    fn _assert_artifact(_: &RecordingArtifact) {}
    let _ = IntermediateRecordingProfile::EditFast;
    let _ = VideoEncodeConfig::default();
}

#[test]
fn facade_reexports_export_types() {
    fn _assert_editing(_: &EditingSession) {}
    fn _assert_request(_: &ExportRequest) {}
}

#[test]
fn facade_reexports_runtime_selectors() {
    let _ = RecordingAudioTrackConfig::microphone_default("microphone");
    let _ = RecordingAudioTrackSource::MicrophoneDefault;
}
