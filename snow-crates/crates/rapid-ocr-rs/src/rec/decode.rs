use std::{
    fs::File,
    io::{BufRead, BufReader},
    path::Path,
};

#[cfg(test)]
use ndarray::Array3;
use ndarray::{ArrayView2, ArrayView3, Axis, s};

use crate::{
    error::{RapidOcrError, Result},
    types::{WordInfo, WordType},
};

#[derive(Debug, Clone)]
pub struct CtcLabelDecoder {
    character: Vec<String>,
}

pub type DecodedLine = (String, f32);
pub type DecodeOutput = (Vec<DecodedLine>, Vec<WordInfo>);

impl CtcLabelDecoder {
    pub fn new(character: Option<Vec<String>>, character_path: Option<&Path>) -> Result<Self> {
        let mut character_list = if let Some(character) = character {
            character
        } else if let Some(path) = character_path {
            read_character_file(path)?
        } else {
            return Err(RapidOcrError::Decode(
                "character and character_path are both None".to_string(),
            ));
        };

        if character_list.is_empty() {
            return Err(RapidOcrError::Decode("character list is empty".to_string()));
        }

        character_list.insert(0, "blank".to_string());
        character_list.push(" ".to_string());
        Ok(Self {
            character: character_list,
        })
    }

    pub fn new_from_text(character: Option<Vec<String>>, text: Option<&str>) -> Result<Self> {
        let character = match (character, text) {
            (Some(character), _) => Some(character),
            (None, Some(text)) => Some(parse_character_text(text)),
            (None, None) => None,
        };
        Self::new(character, None)
    }

    #[cfg(test)]
    pub fn decode(
        &self,
        preds: &Array3<f32>,
        return_word_box: bool,
        wh_ratio_list: &[f32],
        max_wh_ratio: f32,
    ) -> Result<DecodeOutput> {
        self.decode_view(preds.view(), return_word_box, wh_ratio_list, max_wh_ratio)
    }

    pub fn decode_view(
        &self,
        preds: ArrayView3<'_, f32>,
        return_word_box: bool,
        wh_ratio_list: &[f32],
        max_wh_ratio: f32,
    ) -> Result<DecodeOutput> {
        if preds.ndim() != 3 {
            return Err(RapidOcrError::Decode(format!(
                "preds must be rank 3, got rank {}",
                preds.ndim()
            )));
        }

        let (batch_size, _timesteps, class_num) = preds.dim();
        if class_num == 0 {
            return Err(RapidOcrError::Decode(
                "preds class dim cannot be zero".to_string(),
            ));
        }
        if return_word_box && wh_ratio_list.len() != batch_size {
            return Err(RapidOcrError::InvalidInput(format!(
                "wh_ratio_list length {} does not match batch size {} when return_word_box=true",
                wh_ratio_list.len(),
                batch_size
            )));
        }

        let mut line_results = Vec::with_capacity(batch_size);
        let mut word_results = Vec::with_capacity(batch_size);

        for batch_idx in 0..batch_size {
            let probs = preds.slice(s![batch_idx, .., ..]);
            let (token_indices, token_probs) = argmax_with_prob(probs);

            let mut selection = vec![true; token_indices.len()];
            if token_indices.len() >= 2 {
                for i in 1..token_indices.len() {
                    selection[i] = token_indices[i] != token_indices[i - 1];
                }
            }

            for (idx, token) in token_indices.iter().enumerate() {
                if *token == 0 {
                    selection[idx] = false;
                }
            }

            let mut conf_list: Vec<f32> = token_probs
                .iter()
                .zip(selection.iter())
                .filter_map(|(v, keep)| if *keep { Some(round5(*v)) } else { None })
                .collect();

            if conf_list.is_empty() {
                conf_list.push(0.0);
            }

            let mut text = String::new();
            for (token, keep) in token_indices.iter().zip(selection.iter()) {
                if !*keep {
                    continue;
                }
                let ch = self.character.get(*token).ok_or_else(|| {
                    RapidOcrError::Decode(format!(
                        "token id {} out of character range {}",
                        token,
                        self.character.len()
                    ))
                })?;
                text.push_str(ch);
            }

            // Keep parity near text_score threshold by aggregating in f64 before round-to-5.
            let sum_conf: f64 = conf_list.iter().map(|v| f64::from(*v)).sum();
            let mean_conf = round5_f64(sum_conf / conf_list.len() as f64);

            if return_word_box {
                let mut info = get_word_info(&text, &selection);
                let wh_ratio = *wh_ratio_list.get(batch_idx).unwrap_or(&1.0_f32);
                info.line_txt_len = token_indices.len() as f32 * wh_ratio / max_wh_ratio;
                info.confs = conf_list;
                word_results.push(info);
            }
            line_results.push((text, mean_conf));
        }

        Ok((line_results, word_results))
    }
}

fn argmax_with_prob(probs: ArrayView2<'_, f32>) -> (Vec<usize>, Vec<f32>) {
    let rows = probs.len_of(Axis(0));
    let cols = probs.len_of(Axis(1));
    let mut idxs = Vec::with_capacity(rows);
    let mut vals = Vec::with_capacity(rows);

    if let Some(slice) = probs.as_slice_memory_order() {
        for r in 0..rows {
            let row_start = r * cols;
            let row = &slice[row_start..row_start + cols];
            let mut max_idx = 0usize;
            let mut max_val = f32::NEG_INFINITY;
            for (idx, value) in row.iter().enumerate() {
                if *value > max_val {
                    max_val = *value;
                    max_idx = idx;
                }
            }
            idxs.push(max_idx);
            vals.push(max_val);
        }
        return (idxs, vals);
    }

    for row in probs.axis_iter(Axis(0)) {
        let mut max_idx = 0_usize;
        let mut max_val = f32::NEG_INFINITY;
        for (idx, value) in row.iter().enumerate() {
            if *value > max_val {
                max_val = *value;
                max_idx = idx;
            }
        }
        idxs.push(max_idx);
        vals.push(max_val);
    }
    (idxs, vals)
}

fn read_character_file(path: &Path) -> Result<Vec<String>> {
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    let mut out = Vec::new();
    for line in reader.lines() {
        let line = line?;
        let trimmed = line.trim();
        if !trimmed.is_empty() {
            out.push(trimmed.to_string());
        }
    }
    Ok(out)
}

fn parse_character_text(text: &str) -> Vec<String> {
    text.lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .map(str::to_string)
        .collect()
}

fn get_word_info(text: &str, selection: &[bool]) -> WordInfo {
    let mut word_list: Vec<Vec<String>> = Vec::new();
    let mut word_col_list: Vec<Vec<usize>> = Vec::new();
    let mut state_list: Vec<WordType> = Vec::new();

    let mut word_content: Vec<String> = Vec::new();
    let mut word_col_content: Vec<usize> = Vec::new();

    let valid_col: Vec<usize> = selection
        .iter()
        .enumerate()
        .filter_map(|(idx, keep)| if *keep { Some(idx) } else { None })
        .collect();

    if valid_col.is_empty() {
        return WordInfo::default();
    }

    let chars: Vec<char> = text.chars().collect();
    if chars.is_empty() {
        return WordInfo::default();
    }

    let mut col_width = vec![0_usize; valid_col.len()];
    for i in 1..valid_col.len() {
        col_width[i] = valid_col[i] - valid_col[i - 1];
    }
    let first_width = if has_chinese_char(chars[0]) { 3 } else { 2 };
    col_width[0] = first_width.min(valid_col[0]);

    let mut state: Option<WordType> = None;
    for (c_i, ch) in chars.iter().enumerate() {
        if ch.is_whitespace() {
            if !word_content.is_empty() {
                word_list.push(word_content.clone());
                word_col_list.push(word_col_content.clone());
                state_list.push(state.unwrap_or(WordType::EnNum));
                word_content.clear();
                word_col_content.clear();
            }
            continue;
        }

        let c_state = if has_chinese_char(*ch) {
            WordType::Cn
        } else {
            WordType::EnNum
        };

        if state.is_none() {
            state = Some(c_state);
        }

        if state != Some(c_state) || col_width.get(c_i).copied().unwrap_or(0) > 5 {
            if !word_content.is_empty() {
                word_list.push(word_content.clone());
                word_col_list.push(word_col_content.clone());
                state_list.push(state.unwrap_or(WordType::EnNum));
                word_content.clear();
                word_col_content.clear();
            }
            state = Some(c_state);
        }

        word_content.push(ch.to_string());
        if let Some(col) = valid_col.get(c_i) {
            word_col_content.push(*col);
        }
    }

    if !word_content.is_empty() {
        word_list.push(word_content);
        word_col_list.push(word_col_content);
        state_list.push(state.unwrap_or(WordType::EnNum));
    }

    WordInfo {
        words: word_list,
        word_cols: word_col_list,
        word_types: state_list,
        ..WordInfo::default()
    }
}

fn has_chinese_char(ch: char) -> bool {
    ('\u{4e00}'..='\u{9fff}').contains(&ch)
        || ('\u{3000}'..='\u{303f}').contains(&ch)
        || ('\u{ff00}'..='\u{ffef}').contains(&ch)
}

fn round5(v: f32) -> f32 {
    (v * 100_000.0).round_ties_even() / 100_000.0
}

fn round5_f64(v: f64) -> f32 {
    ((v * 100_000.0).round_ties_even() / 100_000.0) as f32
}

#[cfg(test)]
mod tests {
    use ndarray::Array3;

    use super::CtcLabelDecoder;

    #[test]
    fn decode_basic_ctc() {
        let decoder = CtcLabelDecoder::new(Some(vec!["a".into(), "b".into()]), None)
            .expect("decoder init should pass");
        // character list after init: [blank, a, b, " "]
        let preds = Array3::from_shape_vec(
            (1, 4, 4),
            vec![
                0.9, 0.1, 0.0, 0.0, // blank
                0.1, 0.8, 0.1, 0.0, // a
                0.1, 0.8, 0.1, 0.0, // a duplicate (removed)
                0.1, 0.1, 0.8, 0.0, // b
            ],
        )
        .expect("shape should match");

        let (lines, words) = decoder
            .decode(&preds, true, &[1.0], 1.0)
            .expect("decode should pass");
        assert_eq!(lines[0].0, "ab");
        assert_eq!(words.len(), 1);
    }

    #[test]
    fn decode_rejects_wh_ratio_len_mismatch_when_word_box_enabled() {
        let decoder = CtcLabelDecoder::new(Some(vec!["a".into(), "b".into()]), None)
            .expect("decoder init should pass");
        let preds = Array3::from_shape_vec(
            (1, 2, 4),
            vec![
                0.9, 0.1, 0.0, 0.0, //
                0.1, 0.8, 0.1, 0.0, //
            ],
        )
        .expect("shape should match");

        let err = decoder
            .decode(&preds, true, &[], 1.0)
            .expect_err("must reject mismatch");
        assert!(
            err.to_string()
                .contains("wh_ratio_list length 0 does not match batch size 1")
        );
    }
}
