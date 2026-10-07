//! A plain C API over the generated C++ bindings.
//!
//! The C++ backend already lifts and lowers every type, drives Rust futures, and owns the
//! async dispatcher, so the C backend renders it in the expected error style and wraps it:
//! `<namespace>.h` is the C99 header, and `<namespace>_c.cpp` converts between its structs
//! and the C++ types. Consumers compile `<namespace>.cpp` and `<namespace>_c.cpp` as C++17.

mod gen_c;

use std::fs;

use anyhow::Result;
use uniffi_bindgen::{BindingGenerator, Component, GenerationSettings};

use super::cpp::{gen_cpp, write_cpp_bindings};

pub(crate) struct CBindingGenerator;

impl BindingGenerator for CBindingGenerator {
    // The C API inherits the C++ settings (enum style, custom types) its implementation uses.
    type Config = gen_cpp::Config;

    fn new_config(&self, root_toml: &toml::Value) -> Result<Self::Config> {
        Ok(match root_toml.get("bindings").and_then(|b| b.get("cpp")) {
            Some(v) => v.clone().try_into()?,
            None => Default::default(),
        })
    }

    fn update_component_configs(
        &self,
        _settings: &GenerationSettings,
        _components: &mut Vec<Component<Self::Config>>,
    ) -> Result<()> {
        Ok(())
    }

    fn write_bindings(
        &self,
        settings: &GenerationSettings,
        components: &[Component<Self::Config>],
    ) -> Result<()> {
        for Component { ci, config, .. } in components {
            // C has no exceptions to catch, so the implementation returns errors as values.
            let config = config.with_expected();
            gen_c::check_supported(ci, &config)?;
            write_cpp_bindings(&settings.out_dir, ci, &config)?;

            let gen_c::Bindings { header, source } = gen_c::generate(ci, &config)?;
            let namespace = ci.namespace();
            fs::write(settings.out_dir.join(format!("{namespace}.h")), header)?;
            fs::write(settings.out_dir.join(format!("{namespace}_c.cpp")), source)?;
        }

        Ok(())
    }
}
