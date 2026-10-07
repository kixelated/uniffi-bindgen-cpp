pub(crate) mod gen_cpp;

use std::{fmt::Debug, fs};

use anyhow::Result;
use camino::Utf8Path;
use uniffi_bindgen::{
    interface::Literal, BindingGenerator, Component, ComponentInterface, GenerationSettings,
};

use self::gen_cpp::{generate_cpp_bindings, Bindings};

pub(crate) struct CppBindingGenerator {
    pub scaffolding_mode: bool,
}

/// A Trait to help render types in a language specific format.
pub trait CodeType: Debug {
    /// The language specific label used to reference this type. This will be used in
    /// method signatures and property declarations.
    fn type_label(&self, ci: &ComponentInterface) -> String;

    /// A representation of this type label that can be used as part of another
    /// identifier. e.g. `read_foo()`, or `FooInternals`.
    ///
    /// This is especially useful when creating specialized objects or methods to deal
    /// with this type only.
    fn canonical_name(&self) -> String;

    fn literal(&self, _literal: &Literal, ci: &ComponentInterface) -> String {
        unimplemented!("Unimplemented for {}", self.type_label(ci))
    }

    /// Name of the FfiConverter
    fn ffi_converter_name(&self) -> String {
        format!("FfiConverter{}", self.canonical_name())
    }

    /// A list of imports that are needed if this type is in use.
    /// Classes are imported exactly once.
    #[allow(dead_code)]
    fn imports(&self) -> Option<Vec<String>> {
        None
    }

    /// Function to run at startup
    fn initialization_fn(&self) -> Option<String> {
        None
    }
}

impl BindingGenerator for CppBindingGenerator {
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
        _components: &mut Vec<uniffi_bindgen::Component<Self::Config>>,
    ) -> Result<()> {
        return Ok(());
    }

    fn write_bindings(
        &self,
        settings: &GenerationSettings,
        components: &[uniffi_bindgen::Component<Self::Config>],
    ) -> Result<()> {
        for Component { ci, config, .. } in components {
            if self.scaffolding_mode {
                unimplemented!("Cpp scaffolding is not supported yet!");
            }
            write_cpp_bindings(&settings.out_dir, ci, config)?;
        }

        Ok(())
    }
}

/// Writes `<namespace>.hpp`, `<namespace>.cpp`, and `<namespace>_scaffolding.hpp`, plus
/// `uniffi_expected.hpp` under the expected error style.
pub(crate) fn write_cpp_bindings(
    out_dir: &Utf8Path,
    ci: &ComponentInterface,
    config: &gen_cpp::Config,
) -> Result<()> {
    let Bindings {
        scaffolding_header,
        header,
        source,
    } = generate_cpp_bindings(ci, config)?;
    let namespace = ci.namespace();

    // Askama drops each template's final newline, which trips `-Wnewline-eof`.
    fs::write(
        out_dir.join(format!("{namespace}_scaffolding.hpp")),
        scaffolding_header + "\n",
    )?;
    fs::write(out_dir.join(format!("{namespace}.hpp")), header + "\n")?;
    fs::write(out_dir.join(format!("{namespace}.cpp")), source + "\n")?;

    if config.expected() {
        let expected = include_str!("expected.hpp")
            .replace("{tl_expected}", include_str!("vendor/tl_expected.hpp"));
        fs::write(out_dir.join("uniffi_expected.hpp"), expected)?;
    }

    Ok(())
}
