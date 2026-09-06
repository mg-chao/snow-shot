use crate::{Quad, error::Result};

#[derive(Debug, Clone, Copy)]
struct BoxProps {
    top: f32,
    bottom: f32,
    left: f32,
    height: f32,
    center_y: f32,
}

pub fn to_markdown(boxes: &[Quad], txts: &[String]) -> Result<String> {
    if boxes.len() != txts.len() {
        return Err(crate::error::RapidOcrError::InvalidInput(format!(
            "markdown output length mismatch: boxes={}, txts={}",
            boxes.len(),
            txts.len()
        )));
    }
    if boxes.is_empty() {
        return Ok("No text detected.".to_string());
    }

    let mut combined: Vec<(Quad, String)> = (0..boxes.len())
        .map(|i| (boxes[i], txts[i].clone()))
        .collect();
    combined.sort_by(|(a_box, _), (b_box, _)| {
        let a = get_box_properties(a_box);
        let b = get_box_properties(b_box);
        a.top
            .partial_cmp(&b.top)
            .unwrap_or(std::cmp::Ordering::Equal)
            .then(
                a.left
                    .partial_cmp(&b.left)
                    .unwrap_or(std::cmp::Ordering::Equal),
            )
    });

    let mut output_lines: Vec<String> = Vec::new();
    let mut current_line_parts: Vec<String> = vec![combined[0].1.clone()];
    let mut prev_props = get_box_properties(&combined[0].0);

    for (box_, text) in combined.iter().skip(1) {
        let current_props = get_box_properties(box_);

        let min_height = current_props.height.min(prev_props.height);
        let centers_are_close =
            (current_props.center_y - prev_props.center_y).abs() < (min_height * 0.5);

        let overlap_top = prev_props.top.max(current_props.top);
        let overlap_bottom = prev_props.bottom.min(current_props.bottom);
        let has_vertical_overlap = overlap_bottom > overlap_top;

        if centers_are_close || has_vertical_overlap {
            current_line_parts.push("   ".to_string());
            current_line_parts.push(text.clone());
        } else {
            output_lines.push(current_line_parts.join(""));

            let vertical_gap = current_props.top - prev_props.bottom;
            if vertical_gap > prev_props.height * 0.7 {
                output_lines.push(String::new());
            }

            current_line_parts = vec![text.clone()];
        }

        prev_props = current_props;
    }

    output_lines.push(current_line_parts.join(""));
    Ok(output_lines.join("\n"))
}

pub fn to_markdown_texts(txts: &[String]) -> String {
    if txts.is_empty() {
        return "No text detected.".to_string();
    }
    txts.join("\n")
}

fn get_box_properties(box_: &Quad) -> BoxProps {
    let mut top = f32::INFINITY;
    let mut bottom = f32::NEG_INFINITY;
    let mut left = f32::INFINITY;
    for point in box_ {
        top = top.min(point[1]);
        bottom = bottom.max(point[1]);
        left = left.min(point[0]);
    }
    let height = bottom - top;
    BoxProps {
        top,
        bottom,
        left,
        height,
        center_y: top + height / 2.0,
    }
}

#[cfg(test)]
mod tests {
    use super::to_markdown;

    #[test]
    fn markdown_empty_message() {
        assert_eq!(
            to_markdown(&[], &[]).expect("empty should be valid"),
            "No text detected."
        );
    }
}
