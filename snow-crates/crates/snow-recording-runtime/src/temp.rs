use std::fs;
use std::path::PathBuf;

use crate::config::RecordingConfig;
use crate::error::Result;

#[derive(Clone, Debug)]
pub struct TempLayout {
    pub output_dir: PathBuf,
    pub session_dir: PathBuf,
    pub bundle_path: PathBuf,
    pub video_temp_path: PathBuf,
    pub video_index_path: PathBuf,
    pub mouse_path: PathBuf,
}

impl TempLayout {
    pub fn create(config: &RecordingConfig, session_id: &str) -> Result<Self> {
        fs::create_dir_all(&config.output_dir)?;
        let session_dir = config.output_dir.join(format!(".snowtmp-{session_id}"));
        fs::create_dir_all(&session_dir)?;
        let bundle_path = config.output_dir.join(format!("{session_id}.snowrec"));

        Ok(Self {
            output_dir: config.output_dir.clone(),
            session_dir: session_dir.clone(),
            bundle_path: bundle_path.clone(),
            video_temp_path: bundle_path,
            video_index_path: session_dir.join("video_index.bin"),
            mouse_path: session_dir.join("mouse.bin"),
        })
    }
}
