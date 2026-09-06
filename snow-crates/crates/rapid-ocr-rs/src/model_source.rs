use std::path::Path;

#[derive(Debug, Clone, Copy)]
pub enum ModelSource<'a> {
    File(&'a Path),
    Memory { name: &'a str, bytes: &'a [u8] },
}

impl ModelSource<'_> {
    pub(crate) fn name(self) -> String {
        match self {
            Self::File(path) => path.display().to_string(),
            Self::Memory { name, .. } => name.to_string(),
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub enum DictionarySource<'a> {
    File(&'a Path),
    Memory { name: &'a str, text: &'a str },
}

#[derive(Debug, Clone, Copy, Default)]
pub struct PipelineSources<'a> {
    pub det: Option<ModelSource<'a>>,
    pub cls: Option<ModelSource<'a>>,
    pub rec: Option<ModelSource<'a>>,
    pub rec_dictionary: Option<DictionarySource<'a>>,
}
