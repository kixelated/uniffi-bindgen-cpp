mod uniffi_fixtures {
    arithmetical::uniffi_reexport_scaffolding!();
    uniffi_callbacks::uniffi_reexport_scaffolding!();
    custom_types::uniffi_reexport_scaffolding!();
    uniffi_geometry::uniffi_reexport_scaffolding!();
    uniffi_rondpoint::uniffi_reexport_scaffolding!();
    uniffi_sprites::uniffi_reexport_scaffolding!();
    uniffi_todolist::uniffi_reexport_scaffolding!();
    uniffi_traits::uniffi_reexport_scaffolding!();
    uniffi_chronological::uniffi_reexport_scaffolding!();
    uniffi_trait_methods::uniffi_reexport_scaffolding!();
    uniffi_coverall::uniffi_reexport_scaffolding!();
    uniffi_fixture_docstring::uniffi_reexport_scaffolding!();
    uniffi_fixture_callbacks::uniffi_reexport_scaffolding!();
    uniffi_cpp_error_types_builtin::uniffi_reexport_scaffolding!();

    uniffi_cpp_custom_types_builtin::uniffi_reexport_scaffolding!();
    uniffi_enum_style_test::uniffi_reexport_scaffolding!();
    uniffi_empty_type::uniffi_reexport_scaffolding!();
    uniffi_reserved_field_name::uniffi_reexport_scaffolding!();
    uniffi_type_flattening::uniffi_reexport_scaffolding!();
    uniffi_fixture_async_calls::uniffi_reexport_scaffolding!();
    uniffi_c_api::uniffi_reexport_scaffolding!();

    uniffi_ext_types_export::uniffi_reexport_scaffolding!();
    uniffi_ext_types_import::uniffi_reexport_scaffolding!();
}
use std::sync::{
    atomic::{AtomicU64, Ordering},
    Arc,
};

static ASYNC_PENDING_COUNT: AtomicU64 = AtomicU64::new(0);

struct PendingGuard;

impl Drop for PendingGuard {
    fn drop(&mut self) {
        ASYNC_PENDING_COUNT.fetch_sub(1, Ordering::SeqCst);
    }
}

#[derive(Debug, uniffi::Error)]
pub enum AsyncTestError {
    Failed,
}

impl std::fmt::Display for AsyncTestError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "async test failed")
    }
}

impl std::error::Error for AsyncTestError {}

#[uniffi::export]
pub async fn async_fallible(fail: bool) -> Result<String, AsyncTestError> {
    if fail {
        Err(AsyncTestError::Failed)
    } else {
        Ok("async success".to_owned())
    }
}

#[uniffi::export]
pub async fn async_pending() {
    ASYNC_PENDING_COUNT.fetch_add(1, Ordering::SeqCst);
    let _guard = PendingGuard;
    std::future::pending::<()>().await;
}

#[uniffi::export]
pub fn async_pending_count() -> u64 {
    ASYNC_PENDING_COUNT.load(Ordering::SeqCst)
}

#[derive(Debug, uniffi::Error)]
pub enum AsyncParserError {
    InvalidInteger,
    Unexpected,
}

impl std::fmt::Display for AsyncParserError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidInteger => write!(f, "invalid integer"),
            Self::Unexpected => write!(f, "unexpected callback error"),
        }
    }
}

impl std::error::Error for AsyncParserError {}

impl From<uniffi::UnexpectedUniFFICallbackError> for AsyncParserError {
    fn from(_: uniffi::UnexpectedUniFFICallbackError) -> Self {
        Self::Unexpected
    }
}

#[uniffi::export(with_foreign)]
#[async_trait::async_trait]
pub trait AsyncParser: Send + Sync {
    async fn stringify(&self, value: i32) -> String;
    async fn parse(&self, value: String) -> Result<i32, AsyncParserError>;
    async fn wait(&self);
}

#[uniffi::export]
pub async fn stringify_using_parser(parser: Arc<dyn AsyncParser>, value: i32) -> String {
    parser.stringify(value).await
}

#[uniffi::export]
pub async fn parse_using_parser(
    parser: Arc<dyn AsyncParser>,
    value: String,
) -> Result<i32, AsyncParserError> {
    parser.parse(value).await
}

#[uniffi::export]
pub async fn wait_using_parser(parser: Arc<dyn AsyncParser>) {
    parser.wait().await
}

uniffi::setup_scaffolding!();
