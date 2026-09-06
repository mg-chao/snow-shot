use serde::Serialize;

use crate::{Quad, error::Result};

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct OcrJsonItem {
    #[serde(rename = "box", skip_serializing_if = "Option::is_none")]
    pub box_: Option<[[f64; 2]; 4]>,
    pub txt: String,
    pub score: f64,
}

pub fn to_json_items(
    boxes: Option<&[Quad]>,
    txts: &[String],
    scores: &[f32],
) -> Result<Vec<OcrJsonItem>> {
    if txts.len() != scores.len() {
        return Err(crate::error::RapidOcrError::InvalidInput(format!(
            "json output length mismatch: txts={}, scores={}",
            txts.len(),
            scores.len()
        )));
    }

    if let Some(boxes) = boxes
        && boxes.len() != txts.len()
    {
        return Err(crate::error::RapidOcrError::InvalidInput(format!(
            "json output length mismatch: boxes={}, txts={}",
            boxes.len(),
            txts.len()
        )));
    }

    let mut out = Vec::with_capacity(txts.len());
    for i in 0..txts.len() {
        let out_box = boxes.map(|all| {
            let src_box = all[i];
            let mut out_box = [[0.0_f64; 2]; 4];
            for (dst, src) in out_box.iter_mut().zip(src_box.iter()) {
                dst[0] = src[0] as f64;
                dst[1] = src[1] as f64;
            }
            out_box
        });
        out.push(OcrJsonItem {
            box_: out_box,
            txt: txts[i].clone(),
            score: scores[i] as f64,
        });
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::to_json_items;

    #[test]
    fn json_items_none_for_empty_inputs() {
        assert_eq!(
            to_json_items(Some(&[]), &[], &[]).expect("empty should be valid"),
            Vec::new()
        );
    }
}
