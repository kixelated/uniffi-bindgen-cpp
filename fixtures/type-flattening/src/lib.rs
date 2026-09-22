use std::sync::Arc;

#[derive(uniffi::Object)]
pub struct Object {
    pub value: i32,
}

#[uniffi::export]
impl Object {
    pub fn get_value(&self) -> i32 {
        self.value
    }
}

#[derive(uniffi::Record)]
pub struct Structure {
    #[uniffi(default = None)]
    pub optional_arc: Option<Arc<Object>>,
}

#[uniffi::export]
impl Structure {
    pub fn has_object(&self) -> bool {
        self.optional_arc.is_some()
    }

    pub fn adjusted_value(&self, adjustment: i32) -> Result<i32, AdjustmentError> {
        if adjustment < 0 {
            return Err(AdjustmentError::NegativeAdjustment { adjustment });
        }

        Ok(self
            .optional_arc
            .as_ref()
            .map_or(adjustment, |object| object.value + adjustment))
    }
}

#[derive(uniffi::Enum, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[uniffi::export(Eq, Ord, Hash)]
pub enum FlatValue {
    First,
    Second,
}

#[uniffi::export]
impl FlatValue {
    pub fn next(&self) -> Self {
        match self {
            Self::First => Self::Second,
            Self::Second => Self::First,
        }
    }
}

#[derive(uniffi::Enum)]
pub enum RichValue {
    Text(String),
    Number(i32),
}

#[uniffi::export]
impl RichValue {
    pub fn describe(&self) -> String {
        match self {
            Self::Text(value) => format!("text:{value}"),
            Self::Number(value) => format!("number:{value}"),
        }
    }
}

#[derive(Debug, uniffi::Error)]
pub enum AdjustmentError {
    NegativeAdjustment { adjustment: i32 },
}

impl std::fmt::Display for AdjustmentError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::NegativeAdjustment { adjustment } => {
                write!(formatter, "negative adjustment: {adjustment}")
            }
        }
    }
}

impl std::error::Error for AdjustmentError {}

#[uniffi::export]
impl AdjustmentError {
    pub fn is_negative(&self) -> bool {
        matches!(self, Self::NegativeAdjustment { .. })
    }
}

#[uniffi::export]
pub fn get_struct(value: i32) -> Structure {
    Structure {
        optional_arc: Some(Arc::new(Object { value })),
    }
}

#[uniffi::export]
pub fn struct_roundtrip(structure: Structure) -> Structure {
    structure
}

uniffi::include_scaffolding!("type_flattening");
