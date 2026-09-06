use ndarray::ArrayView2;

use crate::error::{RapidOcrError, Result};

pub fn decode_view(
    preds: ArrayView2<'_, f32>,
    label_list: &[String],
) -> Result<Vec<(String, f32)>> {
    if preds.ncols() == 0 {
        return Err(RapidOcrError::Decode(
            "classifier output has zero columns".to_string(),
        ));
    }
    if label_list.is_empty() {
        return Err(RapidOcrError::Config(
            "classifier label_list cannot be empty".to_string(),
        ));
    }
    let mut out = Vec::with_capacity(preds.nrows());
    for row in preds.rows() {
        let mut best_idx = 0_usize;
        let mut best_score = f32::NEG_INFINITY;
        for (idx, score) in row.iter().enumerate() {
            if *score > best_score {
                best_score = *score;
                best_idx = idx;
            }
        }
        let label = label_list
            .get(best_idx)
            .cloned()
            .unwrap_or_else(|| best_idx.to_string());
        out.push((label, best_score));
    }
    Ok(out)
}
