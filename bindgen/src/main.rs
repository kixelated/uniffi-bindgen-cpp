mod bindings;

use anyhow::Context;
use camino::Utf8PathBuf;
use clap::{Parser, ValueEnum};
use uniffi_bindgen::BindingGenerator;

use bindings::{c::CBindingGenerator, cpp::CppBindingGenerator};

/// The language of the generated bindings.
#[derive(Clone, Copy, Debug, ValueEnum)]
enum Lang {
    Cpp,
    /// A C header and its C++ implementation, built on the C++ bindings.
    C,
}

#[derive(Parser)]
struct Args {
    #[clap(long, short)]
    config: Option<Utf8PathBuf>,
    #[clap(long, short)]
    out_dir: Option<Utf8PathBuf>,
    #[clap(long)]
    lib_file: Option<Utf8PathBuf>,
    #[clap(long = "library", conflicts_with = "lib_file", requires = "out_dir")]
    library_mode: bool,
    #[clap(long = "scaffolding", conflicts_with = "lang")]
    scaffolding_mode: bool,
    #[clap(long = "crate")]
    crate_name: Option<String>,
    #[clap(long, value_enum, default_value_t = Lang::Cpp)]
    lang: Lang,
    source: Utf8PathBuf,
}

fn main() {
    let args = Args::parse();

    match args.lang {
        Lang::Cpp => generate(
            &CppBindingGenerator {
                scaffolding_mode: args.scaffolding_mode,
            },
            args,
        ),
        Lang::C => generate(&CBindingGenerator, args),
    }
}

fn generate(generator: &impl BindingGenerator, args: Args) {
    if args.library_mode {
        let config_supplier =
            uniffi_bindgen::cargo_metadata::CrateConfigSupplier::from_cargo_metadata_command(false)
                .unwrap();

        uniffi_bindgen::library_mode::generate_bindings(
            &args.source,
            args.crate_name,
            generator,
            &config_supplier,
            args.config.as_deref(),
            &args.out_dir.unwrap(),
            false,
        )
        .context("Failed to generate bindings using library mode")
        .unwrap();
    } else {
        uniffi_bindgen::generate_external_bindings(
            generator,
            args.source,
            args.config.as_deref(),
            args.out_dir,
            args.lib_file,
            args.crate_name.as_deref(),
            false,
        )
        .context("Failed to generate external bindings")
        .unwrap();
    }
}
