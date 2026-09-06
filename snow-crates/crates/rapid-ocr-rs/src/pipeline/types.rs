use crate::{
    Quad,
    config::RecImage,
    det::detector::DetTimingBreakdown,
    error::{RapidOcrError, Result},
    output::{
        OcrJsonItem, draw_ocr_result, draw_word_boxes, to_json_items, to_markdown,
        to_markdown_texts,
    },
    types::{LineResult, WordBox},
};
use serde_json::{Value, json};

#[derive(Debug, Clone, Default)]
pub struct OcrOutput {
    pub boxes: Option<Vec<Quad>>,
    pub det_scores: Option<Vec<f32>>,
    pub txts: Option<Vec<String>>,
    pub scores: Option<Vec<f32>>,
    pub word_boxes: Option<Vec<Vec<crate::types::WordBox>>>,
    pub cls_res: Option<Vec<(String, f32)>>,
    pub lines: Option<Vec<LineResult>>,
    pub elapsed_ms: [Option<f32>; 3], // [det, cls, rec]
    pub e2e_ms: Option<f32>,
    pub det_breakdown_ms: Option<DetTimingBreakdown>,
}

impl OcrOutput {
    pub fn len(&self) -> usize {
        self.txts.as_ref().map_or(0, Vec::len)
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn to_json(&self) -> Result<Vec<OcrJsonItem>> {
        let txts = self.txts.as_deref().unwrap_or(&[]);
        let scores = self.scores.as_deref().unwrap_or(&[]);
        let boxes = self.boxes.as_deref();
        to_json_items(boxes, txts, scores)
    }

    pub fn to_markdown(&self) -> Result<String> {
        match (self.boxes.as_deref(), self.txts.as_deref()) {
            (Some(boxes), Some(txts)) => to_markdown(boxes, txts),
            (None, Some(txts)) => Ok(to_markdown_texts(txts)),
            (Some(_), None) => Err(RapidOcrError::InvalidInput(
                "markdown output requires txts when boxes are present".to_string(),
            )),
            (None, None) => Ok("No text detected.".to_string()),
        }
    }

    pub fn visualize(&self, image: &RecImage, use_word_boxes: bool) -> Option<image::RgbImage> {
        if use_word_boxes && let Some(word_boxes) = &self.word_boxes {
            return Some(draw_word_boxes(image, word_boxes));
        }
        self.boxes
            .as_ref()
            .map(|boxes| draw_ocr_result(image, boxes))
    }
}

#[derive(Debug, Clone, Default)]
pub struct StageTimings {
    pub det_ms: Option<f32>,
    pub det_pre_ms: Option<f32>,
    pub det_infer_ms: Option<f32>,
    pub det_post_ms: Option<f32>,
    pub cls_ms: Option<f32>,
    pub rec_ms: Option<f32>,
    pub total_ms: f32,
    pub e2e_ms: Option<f32>,
}

impl StageTimings {
    pub fn from_elapsed_ms(
        elapsed_ms: [Option<f32>; 3],
        e2e_ms: Option<f32>,
        det_breakdown_ms: Option<DetTimingBreakdown>,
    ) -> Self {
        let total_ms = elapsed_ms.iter().flatten().copied().sum::<f32>();
        let (det_pre_ms, det_infer_ms, det_post_ms) = match det_breakdown_ms {
            Some(v) => (
                Some(v.preprocess_ms),
                Some(v.infer_ms),
                Some(v.postprocess_ms),
            ),
            None => (None, None, None),
        };
        Self {
            det_ms: elapsed_ms[0],
            det_pre_ms,
            det_infer_ms,
            det_post_ms,
            cls_ms: elapsed_ms[1],
            rec_ms: elapsed_ms[2],
            total_ms,
            e2e_ms,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct DetResult {
    pub boxes: Vec<Quad>,
    pub scores: Vec<f32>,
    pub timings: StageTimings,
}

#[derive(Debug, Clone, Default)]
pub struct ClsResult {
    pub cls_res: Vec<(String, f32)>,
    pub timings: StageTimings,
}

#[derive(Debug, Clone, Default)]
pub struct RecResult {
    pub lines: Vec<LineResult>,
    pub txts: Vec<String>,
    pub scores: Vec<f32>,
    pub word_boxes: Option<Vec<Vec<WordBox>>>,
    pub cls_res: Option<Vec<(String, f32)>>,
    pub timings: StageTimings,
}

#[derive(Debug, Clone, Default)]
pub struct FullResult {
    pub boxes: Vec<Quad>,
    pub det_scores: Vec<f32>,
    pub lines: Vec<LineResult>,
    pub txts: Vec<String>,
    pub scores: Vec<f32>,
    pub word_boxes: Option<Vec<Vec<WordBox>>>,
    pub cls_res: Option<Vec<(String, f32)>>,
    pub timings: StageTimings,
}

#[derive(Debug, Clone)]
pub enum OcrResult {
    Empty,
    Det(DetResult),
    Cls(ClsResult),
    Rec(RecResult),
    Full(FullResult),
}

impl OcrResult {
    pub fn len(&self) -> usize {
        match self {
            Self::Empty => 0,
            Self::Det(v) => v.boxes.len(),
            Self::Cls(v) => v.cls_res.len(),
            Self::Rec(v) => v.txts.len(),
            Self::Full(v) => v.txts.len(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn to_json_items(&self) -> Result<Vec<OcrJsonItem>> {
        match self {
            Self::Empty => Ok(Vec::new()),
            Self::Det(_) | Self::Cls(_) => Ok(Vec::new()),
            Self::Rec(v) => to_json_items(None, &v.txts, &v.scores),
            Self::Full(v) => to_json_items(Some(&v.boxes), &v.txts, &v.scores),
        }
    }

    pub fn to_json(&self) -> Result<Value> {
        match self {
            Self::Empty => Ok(json!({ "kind": "empty" })),
            Self::Det(v) => Ok(json!({
                "kind": "det",
                "boxes": v.boxes,
                "det_scores": v.scores,
            })),
            Self::Cls(v) => Ok(json!({
                "kind": "cls",
                "cls_res": v.cls_res,
            })),
            Self::Rec(v) => Ok(json!({
                "kind": "rec",
                "items": to_json_items(None, &v.txts, &v.scores)?,
                "word_boxes": v.word_boxes,
            })),
            Self::Full(v) => Ok(json!({
                "kind": "full",
                "items": to_json_items(Some(&v.boxes), &v.txts, &v.scores)?,
                "det_scores": v.det_scores,
                "word_boxes": v.word_boxes,
            })),
        }
    }

    pub fn to_markdown(&self) -> Result<String> {
        match self {
            Self::Empty => Ok("No text detected.".to_string()),
            Self::Det(_) => Ok("No text detected.".to_string()),
            Self::Cls(_) => Ok("No text detected.".to_string()),
            Self::Rec(v) => Ok(to_markdown_texts(&v.txts)),
            Self::Full(v) => to_markdown(&v.boxes, &v.txts),
        }
    }

    pub fn visualize(&self, image: &RecImage, use_word_boxes: bool) -> Option<image::RgbImage> {
        match self {
            Self::Full(v) => {
                if use_word_boxes && let Some(word_boxes) = &v.word_boxes {
                    return Some(draw_word_boxes(image, word_boxes));
                }
                Some(draw_ocr_result(image, &v.boxes))
            }
            Self::Det(v) => Some(draw_ocr_result(image, &v.boxes)),
            _ => None,
        }
    }
}

impl TryFrom<OcrOutput> for OcrResult {
    type Error = RapidOcrError;

    fn try_from(value: OcrOutput) -> Result<Self> {
        let OcrOutput {
            boxes,
            det_scores,
            txts,
            scores,
            word_boxes,
            cls_res,
            lines,
            elapsed_ms,
            e2e_ms,
            det_breakdown_ms,
        } = value;

        let timings = StageTimings::from_elapsed_ms(elapsed_ms, e2e_ms, det_breakdown_ms);

        let boxes = boxes.unwrap_or_default();
        let det_scores = det_scores.unwrap_or_default();
        let mut txts = txts.unwrap_or_default();
        let mut scores = scores.unwrap_or_default();
        let mut lines = lines.unwrap_or_default();

        if !det_scores.is_empty() && det_scores.len() != boxes.len() {
            return Err(RapidOcrError::InvalidInput(format!(
                "det_scores length mismatch: boxes={}, det_scores={}",
                boxes.len(),
                det_scores.len()
            )));
        }

        if lines.is_empty() && !txts.is_empty() {
            if txts.len() != scores.len() {
                return Err(RapidOcrError::InvalidInput(format!(
                    "text output length mismatch: txts={}, scores={}",
                    txts.len(),
                    scores.len()
                )));
            }
            lines = txts
                .iter()
                .cloned()
                .zip(scores.iter().copied())
                .map(|(text, score)| LineResult {
                    text,
                    score,
                    word_info: None,
                })
                .collect();
        }

        if txts.is_empty() && scores.is_empty() && !lines.is_empty() {
            txts = lines.iter().map(|v| v.text.clone()).collect();
            scores = lines.iter().map(|v| v.score).collect();
        }

        if txts.len() != scores.len() {
            return Err(RapidOcrError::InvalidInput(format!(
                "text output length mismatch: txts={}, scores={}",
                txts.len(),
                scores.len()
            )));
        }
        if !lines.is_empty() && lines.len() != txts.len() {
            return Err(RapidOcrError::InvalidInput(format!(
                "line output length mismatch: lines={}, txts={}",
                lines.len(),
                txts.len()
            )));
        }

        let has_boxes = !boxes.is_empty();
        let has_lines = !txts.is_empty();
        let has_cls = cls_res.as_ref().is_some_and(|v| !v.is_empty());

        if has_boxes && has_lines {
            if boxes.len() != txts.len() {
                return Err(RapidOcrError::InvalidInput(format!(
                    "full output length mismatch: boxes={}, txts={}",
                    boxes.len(),
                    txts.len()
                )));
            }
            return Ok(OcrResult::Full(FullResult {
                boxes,
                det_scores,
                lines,
                txts,
                scores,
                word_boxes,
                cls_res,
                timings,
            }));
        }

        if has_boxes {
            return Ok(OcrResult::Det(DetResult {
                boxes,
                scores: det_scores,
                timings,
            }));
        }

        if has_lines {
            return Ok(OcrResult::Rec(RecResult {
                lines,
                txts,
                scores,
                word_boxes,
                cls_res,
                timings,
            }));
        }

        if has_cls {
            return Ok(OcrResult::Cls(ClsResult {
                cls_res: cls_res.unwrap_or_default(),
                timings,
            }));
        }

        Ok(OcrResult::Empty)
    }
}

#[derive(Debug, Clone, Default)]
pub struct OcrCallOptions {
    pub use_det: Option<bool>,
    pub use_cls: Option<bool>,
    pub use_rec: Option<bool>,
    pub return_word_box: Option<bool>,
    pub return_single_char_box: Option<bool>,
    pub text_score: Option<f32>,
    pub box_thresh: Option<f32>,
    pub unclip_ratio: Option<f32>,
}

pub type RunOptions = OcrCallOptions;
