//! Explicit paragraph requests, independent of recognition and its session.
use crate::{Job, acceleration, protocol::Decoder, wire_boolean};
use simd_paddle_ocr_rs::{ExecutionConfig, ExecutionTarget, ParagraphPlan};
use std::{io, path::PathBuf, sync::atomic::Ordering};

pub struct Config {
    pub model: PathBuf,
    pub acceleration: bool,
    pub quads: Vec<[[f32; 2]; 4]>,
}

pub fn decode(payload: &[u8]) -> io::Result<Config> {
    let mut decoder = Decoder::new(payload);
    let model = PathBuf::from(decoder.string()?);
    let acceleration = wire_boolean(decoder.u8()?)?;
    let count = decoder.u32()? as usize;
    if count > 10000 || model.as_os_str().is_empty() {
        return Err(io::Error::other("invalid paragraph request"));
    }
    let mut quads = Vec::with_capacity(count);
    for _ in 0..count {
        let mut quad = [[0.; 2]; 4];
        for value in quad.iter_mut().flatten() {
            *value = decoder.f32()?;
            if !value.is_finite() {
                return Err(io::Error::other("nonfinite paragraph geometry"));
            }
        }
        quads.push(quad);
    }
    if !decoder.done() {
        return Err(io::Error::other("trailing paragraph request data"));
    }
    Ok(Config {
        model,
        acceleration,
        quads,
    })
}

pub struct Completion {
    pub id: u64,
    pub result: Result<Vec<Vec<usize>>, String>,
    pub cancelled: bool,
}

pub fn infer(job: Job, config: Config, acceleration: &acceleration::Acceleration) -> Completion {
    let result = (|| {
        if job.cancelled.load(Ordering::Acquire) {
            return Err("cancelled".to_owned());
        }
        let gpu = config
            .acceleration
            .then(|| acceleration.snapshot())
            .flatten();
        let execution = ExecutionConfig {
            target: if gpu.is_some() {
                ExecutionTarget::Gpu
            } else {
                ExecutionTarget::Cpu
            },
            #[cfg(feature = "gpu")]
            gpu,
            #[cfg(feature = "gpu")]
            gpu_options: Default::default(),
        };
        // No Auto target or CPU retry: a failed grouping leaves the original lines intact.
        let plan = ParagraphPlan::load_path(&config.model, execution).map_err(|e| e.to_string())?;
        let mut request = plan.request().map_err(|e| e.to_string())?;
        let image = job.input.view().map_err(|e| e.to_string())?;
        request
            .group(image, &config.quads)
            .map(|groups| {
                // Treat the result as membership only. Reading order belongs to the host.
                let mut groups = groups.to_vec();
                for group in &mut groups {
                    group.sort_unstable();
                }
                groups.sort_by_key(|group| group.first().copied());
                groups
            })
            .map_err(|e| e.to_string())
    })();
    Completion {
        id: job.id,
        result,
        cancelled: job.cancelled.load(Ordering::Acquire),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protocol::{put_f32, put_string, put_u8, put_u32};

    #[test]
    fn grouping_has_its_own_geometry_only_request() {
        let mut payload = Vec::new();
        put_string(&mut payload, "paragraph.onnx");
        put_u8(&mut payload, 0);
        put_u32(&mut payload, 1);
        for value in [0., 0., 30., 0., 30., 10., 0., 10.] {
            put_f32(&mut payload, value);
        }
        let request = decode(&payload).unwrap();
        assert_eq!(request.quads.len(), 1);
        assert!(!request.acceleration);
        payload.push(0);
        assert!(decode(&payload).is_err());
        payload.pop();
        let n = payload.len();
        payload[n - 4..].copy_from_slice(&f32::NAN.to_le_bytes());
        assert!(decode(&payload).is_err());
        assert!(decode(&payload[..n - 1]).is_err());
    }
}
