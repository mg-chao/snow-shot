use unicode_bidi::BidiInfo;

pub fn reorder_bidi_for_display(text: &str) -> String {
    let bidi_info = BidiInfo::new(text, None);
    let mut out = String::new();

    for para in &bidi_info.paragraphs {
        let line = para.range.clone();
        let display = bidi_info.reorder_line(para, line);
        out.push_str(&display);
    }

    out
}

#[cfg(test)]
mod tests {
    use super::reorder_bidi_for_display;

    #[test]
    fn english_text_is_stable() {
        let text = "hello world";
        assert_eq!(reorder_bidi_for_display(text), text);
    }

    #[test]
    fn arabic_text_roundtrip_has_same_char_count() {
        let text = "امروز هوا خوبه";
        let out = reorder_bidi_for_display(text);
        assert!(!out.is_empty());
        assert_eq!(out.chars().count(), text.chars().count());
    }
}
