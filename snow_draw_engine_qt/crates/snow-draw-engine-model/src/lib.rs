mod model;
mod query;

pub use model::DocumentModel;
pub use query::{QueryStore, RelationIndex, SnapIndex, SpatialIndex};
pub use snow_draw_engine_core::{Point, SnapQuery, SnapResult, ViewportQuery};
pub use snow_draw_engine_document::{
    ApplyResult, BindableElementState, Document, DocumentDelta, DocumentRevision, ElementData,
    ElementId, ElementKind, ElementMeta, RectangleData, SerialNumberData, TextData, Transaction,
};
