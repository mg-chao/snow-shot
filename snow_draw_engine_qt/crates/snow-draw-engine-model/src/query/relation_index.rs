use snow_draw_engine_document::{Document, DocumentDelta, ElementId};

#[derive(Clone, Debug, Default)]
pub struct RelationIndex {
    bound_arrows_by_bindable: Vec<Vec<ElementId>>,
    bound_bindables_by_arrow: Vec<Vec<ElementId>>,
}

impl RelationIndex {
    pub fn rebuild(&mut self, document: &Document) {
        self.bound_arrows_by_bindable.clear();
        self.bound_bindables_by_arrow.clear();

        for arrow_id in document.paint_order().iter().copied() {
            let Ok(arrow) = document.arrow(arrow_id) else {
                continue;
            };

            for bindable_id in arrow.bound_element_ids() {
                if document.element(bindable_id).is_err() {
                    continue;
                }
                self.ensure_bindable_capacity(bindable_id.index as usize);
                self.ensure_arrow_capacity(arrow_id.index as usize);

                let bound_arrows = &mut self.bound_arrows_by_bindable[bindable_id.index as usize];
                if !bound_arrows.contains(&arrow_id) {
                    bound_arrows.push(arrow_id);
                }

                let bound_bindables = &mut self.bound_bindables_by_arrow[arrow_id.index as usize];
                if !bound_bindables.contains(&bindable_id) {
                    bound_bindables.push(bindable_id);
                }
            }
        }
    }

    pub fn refresh(&mut self, document: &Document, delta: &DocumentDelta) {
        if delta.relations_changed {
            self.rebuild(document);
        }
    }

    pub fn bound_arrow_ids(&self, bindable_id: ElementId) -> &[ElementId] {
        self.bound_arrows_by_bindable
            .get(bindable_id.index as usize)
            .map(Vec::as_slice)
            .unwrap_or(&[])
    }

    pub fn bound_bindable_ids(&self, arrow_id: ElementId) -> &[ElementId] {
        self.bound_bindables_by_arrow
            .get(arrow_id.index as usize)
            .map(Vec::as_slice)
            .unwrap_or(&[])
    }

    fn ensure_bindable_capacity(&mut self, index: usize) {
        if self.bound_arrows_by_bindable.len() <= index {
            self.bound_arrows_by_bindable
                .resize_with(index + 1, Vec::new);
        }
    }

    fn ensure_arrow_capacity(&mut self, index: usize) {
        if self.bound_bindables_by_arrow.len() <= index {
            self.bound_bindables_by_arrow
                .resize_with(index + 1, Vec::new);
        }
    }
}
