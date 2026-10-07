//! Renders the C header and its C++ implementation.
//!
//! Every uniffi type maps to one C "field" type, used in struct members and lists, plus how it
//! is passed in (borrowed) and handed back (owned). Each type also gets three internal C++
//! converters named after its canonical name: `to_c_X` builds the owned C value from the C++
//! one, `from_c_X` builds the C++ value from a borrowed C one, and `free_c_X` releases what
//! `to_c_X` allocated.

use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

use anyhow::{bail, Context, Result};
use askama::Template;
use heck::{ToShoutySnakeCase, ToSnakeCase};
use uniffi_bindgen::{
    interface::{
        AsType, Callable, DefaultValue, Enum, Field, Literal, Object, Radix, Record, Type,
    },
    ComponentInterface,
};

use crate::bindings::cpp::gen_cpp::{
    filters::{self, CppCodeOracle},
    Config,
};

pub(crate) struct Bindings {
    pub(crate) header: String,
    pub(crate) source: String,
}

#[derive(Template)]
#[template(syntax = "cpp", escape = "none", path = "c_header.h")]
struct Header<'a> {
    ns: &'a str,
    guard: String,
    body: &'a str,
}

#[derive(Template)]
#[template(syntax = "cpp", escape = "none", path = "c_source.cpp")]
struct Source<'a> {
    ns: &'a str,
    namespace: &'a str,
    objects: &'a str,
    converters: &'a str,
    body: &'a str,
}

/// Refuses whatever the C backend cannot render correctly, before anything is written.
pub(crate) fn check_supported(ci: &ComponentInterface, config: &Config) -> Result<()> {
    Gen::new(ci, config).check_supported()
}

/// Renders the C API. Must run after the C++ bindings were rendered in the expected style
/// on this thread, because the C++ type labels it reuses depend on that style.
pub(crate) fn generate(ci: &ComponentInterface, config: &Config) -> Result<Bindings> {
    let mut gen = Gen::new(ci, config);

    let mut header = String::new();
    let mut source = String::new();
    gen.render(&mut header, &mut source)?;

    let mut objects = String::new();
    for obj in ci.object_definitions() {
        let c = c_name(obj.name());
        let class = CppCodeOracle.class_name(obj.name());
        writeln!(
            objects,
            "struct {c} {{\n    std::shared_ptr<::{}::{class}> inner;\n}};",
            ci.namespace()
        )?;
    }

    let mut converters = String::new();
    gen.render_converters(&mut converters)?;

    let ns = gen.ns.clone();
    let header = Header {
        ns: &ns,
        guard: format!("UNIFFI_{}_H", ns.to_shouty_snake_case()),
        body: header.trim_end(),
    }
    .render()
    .context("rendering the C header failed")?;
    let source = Source {
        ns: &ns,
        namespace: ci.namespace(),
        objects: objects.trim_end(),
        converters: converters.trim_end(),
        body: source.trim_end(),
    }
    .render()
    .context("rendering the C implementation failed")?;

    // Askama drops the final newline.
    Ok(Bindings {
        header: header + "\n",
        source: source + "\n",
    })
}

#[derive(Clone, Debug)]
enum Kind {
    Scalar,
    FlatEnum,
    String,
    Bytes,
    Record,
    DataEnum,
    Object,
    /// An optional string, object, or struct: a pointer that is NULL when absent.
    Optional(Box<CType>),
    /// An optional scalar or flat enum: a `{ has_value, value }` struct.
    OptionalValue(Box<CType>),
    List(Box<CType>),
    Map(Box<CType>, Box<CType>),
}

#[derive(Clone, Debug)]
struct CType {
    ty: Type,
    kind: Kind,
    /// The C type name: the scalar, or the struct or enum name.
    c: String,
    /// The C++ type the bindings use, relative to their namespace.
    cpp: String,
    /// Unique per type; names the converters.
    canon: String,
    /// Names the type inside a compound type's C name.
    slug: String,
}

impl CType {
    /// The owned representation, used for struct members and list items.
    fn field(&self) -> String {
        match &self.kind {
            Kind::Scalar | Kind::FlatEnum | Kind::OptionalValue(_) => self.c.clone(),
            Kind::String => "const char *".into(),
            Kind::Bytes | Kind::Record | Kind::DataEnum | Kind::List(_) | Kind::Map(..) => {
                self.c.clone()
            }
            Kind::Object => format!("{} *", self.c),
            Kind::Optional(inner) => match inner.kind {
                Kind::String | Kind::Object => inner.field(),
                _ => format!("{} *", inner.field()),
            },
        }
    }

    /// The borrowed representation the `from_c_X` converter takes.
    fn view(&self) -> String {
        match &self.kind {
            Kind::Scalar
            | Kind::FlatEnum
            | Kind::OptionalValue(_)
            | Kind::String
            | Kind::Object => self.field(),
            Kind::Bytes | Kind::Record | Kind::DataEnum | Kind::List(_) | Kind::Map(..) => {
                format!("const {} &", self.c)
            }
            Kind::Optional(inner) => match inner.kind {
                Kind::String | Kind::Object => inner.field(),
                _ => format!("const {} *", inner.field()),
            },
        }
    }

    /// A struct passed by pointer as an argument, so it must not be NULL.
    fn by_pointer(&self) -> bool {
        matches!(
            self.kind,
            Kind::Bytes | Kind::Record | Kind::DataEnum | Kind::List(_) | Kind::Map(..)
        )
    }

    /// How an argument of this type is declared.
    fn param(&self, name: &str) -> String {
        let ty = if self.by_pointer() {
            format!("const {} *", self.c)
        } else {
            self.view()
        };
        decl(&ty, name)
    }

    /// An argument the C API rejects when NULL.
    fn required_pointer(&self) -> bool {
        self.by_pointer() || matches!(self.kind, Kind::String | Kind::Object)
    }

    /// How a returned value is handed to the caller, who owns it.
    fn ret(&self) -> String {
        match &self.kind {
            Kind::Scalar | Kind::FlatEnum | Kind::OptionalValue(_) => self.c.clone(),
            Kind::String => "char *".into(),
            Kind::Optional(inner) => match inner.kind {
                Kind::String => "char *".into(),
                Kind::Object => inner.field(),
                _ => format!("{} *", inner.field()),
            },
            _ => format!("{} *", self.c),
        }
    }

    /// The function that frees a returned value, when the caller owns an allocation.
    fn free_fn(&self, ns: &str) -> Option<String> {
        match &self.kind {
            Kind::Scalar | Kind::FlatEnum | Kind::OptionalValue(_) => None,
            Kind::String => Some(format!("{ns}_string_free")),
            Kind::Optional(inner) => inner.free_fn(ns),
            _ => Some(format!("{}_free", self.c)),
        }
    }

    /// Converts a C++ value to what [`Self::ret`] declares.
    fn ret_expr(&self, ns: &str, expr: &str) -> String {
        let convert = format!("::{ns}::c_detail::to_c_{}({expr})", self.canon);
        match &self.kind {
            Kind::String => format!("const_cast<char *>({convert})"),
            Kind::Optional(inner) if matches!(inner.kind, Kind::String) => {
                format!("const_cast<char *>({convert})")
            }
            Kind::Bytes | Kind::Record | Kind::DataEnum | Kind::List(_) | Kind::Map(..) => {
                format!("::{ns}::c_detail::box({convert})")
            }
            _ => convert,
        }
    }
}

/// Joins a C type and a name, without a space after a pointer.
fn decl(ty: &str, name: &str) -> String {
    if ty.ends_with('*') || ty.ends_with('&') {
        format!("{ty}{name}")
    } else {
        format!("{ty} {name}")
    }
}

/// The C name of a uniffi type or function: its name in snake case.
fn c_name(name: &str) -> String {
    name.to_snake_case()
}

fn c_field_name(field: &Field, index: usize) -> String {
    if field.name().is_empty() {
        format!("v{index}")
    } else {
        filters::var_name(field.name()).unwrap()
    }
}

fn doc(docstring: Option<&str>, indent: i32) -> String {
    match docstring {
        Some(docstring) => format!("{}\n", filters::docstring(docstring, &indent).unwrap()),
        None => String::new(),
    }
}

/// A one-line comment for something the generator adds.
fn line_doc(text: &str) -> String {
    format!("/** {text} */\n")
}

struct Gen<'a> {
    ci: &'a ComponentInterface,
    config: &'a Config,
    /// The prefix of the runtime symbols: the namespace in snake case.
    ns: String,
    /// Every type a converter is emitted for, by canonical name.
    types: BTreeMap<String, CType>,
    /// Every global C identifier, to refuse a collision instead of emitting one.
    names: BTreeSet<String>,
}

impl<'a> Gen<'a> {
    fn new(ci: &'a ComponentInterface, config: &'a Config) -> Self {
        Self {
            ci,
            config,
            ns: ci.namespace().to_snake_case(),
            types: BTreeMap::new(),
            names: BTreeSet::new(),
        }
    }

    fn claim(&mut self, name: &str) -> Result<()> {
        if !self.names.insert(name.to_string()) {
            bail!("the C backend would emit `{name}` twice; rename one of the uniffi items");
        }
        Ok(())
    }

    /// Refuses whatever this backend cannot render correctly, before rendering anything.
    fn check_supported(&self) -> Result<()> {
        if let Some(cbi) = self.ci.callback_interface_definitions().first() {
            bail!(
                "the C backend does not support callback interfaces, but `{}` is one",
                cbi.name()
            );
        }
        if let Some(ty) = self.ci.iter_external_types().next() {
            bail!("the C backend does not support external types, but `{ty:?}` is one");
        }
        for obj in self.ci.object_definitions() {
            if obj.has_callback_interface() {
                bail!(
                    "the C backend does not support foreign-implemented traits, but `{}` is one",
                    obj.name()
                );
            }
            if let Some(t) = obj.uniffi_traits().first() {
                bail!(
                    "the C backend does not support exported traits on objects yet, but `{}` exports {t:?}",
                    obj.name()
                );
            }
            if self.ci.is_name_used_as_error(obj.name()) {
                bail!(
                    "the C backend does not support objects as errors, but `{}` is one",
                    obj.name()
                );
            }
        }
        for rec in self.ci.record_definitions() {
            if !rec.methods().is_empty() || !rec.constructors().is_empty() {
                bail!(
                    "the C backend does not support methods on records yet, but `{}` has some",
                    rec.name()
                );
            }
            if !rec.has_fields() {
                bail!(
                    "the C backend does not support empty records, but `{}` is one",
                    rec.name()
                );
            }
            let traits = rec.uniffi_trait_methods();
            if traits.display_fmt.is_some()
                || traits.debug_fmt.is_some()
                || traits.eq_eq.is_some()
                || traits.hash_hash.is_some()
                || traits.ord_cmp.is_some()
            {
                bail!(
                    "the C backend does not support exported traits on records yet, but `{}` exports some",
                    rec.name()
                );
            }
        }
        for e in self.ci.enum_definitions() {
            if !e.methods().is_empty() || !e.constructors().is_empty() {
                bail!(
                    "the C backend does not support methods on enums yet, but `{}` has some",
                    e.name()
                );
            }
            let traits = e.uniffi_trait_methods();
            // An error's Display output is its message; nothing else is rendered.
            let display_ok = self.ci.is_name_used_as_error(e.name());
            if (traits.display_fmt.is_some() && !display_ok)
                || traits.debug_fmt.is_some()
                || traits.eq_eq.is_some()
                || traits.hash_hash.is_some()
                || traits.ord_cmp.is_some()
            {
                bail!(
                    "the C backend does not support exported traits on enums yet, but `{}` exports some",
                    e.name()
                );
            }
        }
        Ok(())
    }

    /// Maps a uniffi type, registering it and everything it contains for converters.
    fn ctype(&mut self, ty: &Type) -> Result<CType> {
        let code = CppCodeOracle.find(ty);
        let canon = code.canonical_name();
        if let Some(ct) = self.types.get(&canon) {
            return Ok(ct.clone());
        }
        let cpp = code.type_label(self.ci);
        let ns = self.ns.clone();
        let named = |name: &str| {
            let c = c_name(name);
            let slug = c.strip_prefix(&format!("{ns}_")).unwrap_or(&c).to_string();
            (c, slug)
        };
        let scalar = |c: &str, slug: &str| (Kind::Scalar, c.to_string(), slug.to_string());

        let (kind, c, slug) = match ty {
            Type::UInt8 => scalar("uint8_t", "u8"),
            Type::Int8 => scalar("int8_t", "i8"),
            Type::UInt16 => scalar("uint16_t", "u16"),
            Type::Int16 => scalar("int16_t", "i16"),
            Type::UInt32 => scalar("uint32_t", "u32"),
            Type::Int32 => scalar("int32_t", "i32"),
            Type::UInt64 => scalar("uint64_t", "u64"),
            Type::Int64 => scalar("int64_t", "i64"),
            Type::Float32 => scalar("float", "f32"),
            Type::Float64 => scalar("double", "f64"),
            Type::Boolean => scalar("bool", "bool"),
            Type::String => (Kind::String, "const char *".into(), "string".into()),
            Type::Bytes => (Kind::Bytes, format!("{ns}_bytes"), "bytes".into()),
            Type::Record { name, .. } => {
                let (c, slug) = named(name);
                (Kind::Record, c, slug)
            }
            Type::Enum { name, .. } => {
                let e = self
                    .ci
                    .get_enum_definition(name)
                    .with_context(|| format!("enum `{name}` not found"))?;
                let kind = if filters::is_enum_struct(self.ci, name) {
                    Kind::DataEnum
                } else {
                    Kind::FlatEnum
                };
                if e.variants().is_empty() {
                    bail!("the C backend does not support empty enums, but `{name}` is one");
                }
                let (c, slug) = named(name);
                (kind, c, slug)
            }
            Type::Object { name, .. } => {
                let (c, slug) = named(name);
                (Kind::Object, c, slug)
            }
            Type::Optional { inner_type } => {
                let inner = self.ctype(inner_type)?;
                let slug = format!("optional_{}", inner.slug);
                match inner.kind {
                    Kind::Optional(_) | Kind::OptionalValue(_) => {
                        bail!("the C backend does not support nested optionals (`{cpp}`)")
                    }
                    Kind::Scalar | Kind::FlatEnum => (
                        Kind::OptionalValue(Box::new(inner)),
                        format!("{ns}_{slug}"),
                        slug,
                    ),
                    _ => (Kind::Optional(Box::new(inner)), String::new(), slug),
                }
            }
            Type::Sequence { inner_type } => {
                let inner = self.ctype(inner_type)?;
                let slug = format!("{}_list", inner.slug);
                (Kind::List(Box::new(inner)), format!("{ns}_{slug}"), slug)
            }
            Type::Map {
                key_type,
                value_type,
            } => {
                let key = self.ctype(key_type)?;
                let value = self.ctype(value_type)?;
                let slug = format!("{}_{}_map", key.slug, value.slug);
                (
                    Kind::Map(Box::new(key), Box::new(value)),
                    format!("{ns}_{slug}"),
                    slug,
                )
            }
            Type::Box { inner_type } => return self.ctype(inner_type),
            Type::Timestamp | Type::Duration | Type::Set { .. } | Type::Custom { .. } => {
                bail!("the C backend does not support `{cpp}` yet")
            }
            Type::CallbackInterface { name, .. } => {
                bail!("the C backend does not support callback interfaces, but `{name}` is one")
            }
        };

        let ct = CType {
            ty: ty.clone(),
            kind,
            c,
            cpp,
            canon: canon.clone(),
            slug,
        };
        self.types.insert(canon, ct.clone());
        Ok(ct)
    }

    fn render(&mut self, h: &mut String, s: &mut String) -> Result<()> {
        let ns = self.ns.clone();
        for name in [
            "task",
            "task_free",
            "work",
            "work_run",
            "dispatcher",
            "set_dispatcher",
            "shutdown_dispatcher",
            "string_free",
        ] {
            self.claim(&format!("{ns}_{name}"))?;
        }

        // Register every reachable type before rendering any of them.
        let local: Vec<Type> = self.ci.iter_local_types().cloned().collect();
        for ty in &local {
            self.ctype(ty)?;
        }

        let types: Vec<CType> = self.types.values().cloned().collect();

        // Flat enums first: C cannot forward-declare an enum.
        for ct in types.iter().filter(|ct| matches!(ct.kind, Kind::FlatEnum)) {
            self.render_flat_enum(h, ct)?;
        }

        // Then every struct name, so lists and pointers can refer to any of them.
        let mut forwards = String::new();
        for ct in &types {
            if matches!(
                ct.kind,
                Kind::Bytes
                    | Kind::Record
                    | Kind::DataEnum
                    | Kind::Object
                    | Kind::OptionalValue(_)
                    | Kind::List(_)
                    | Kind::Map(..)
            ) {
                self.claim(&ct.c)?;
                writeln!(forwards, "typedef struct {0} {0};", ct.c)?;
            }
        }
        if !forwards.is_empty() {
            writeln!(h, "{forwards}")?;
        }

        // Compound types hold only pointers, so they can precede the records they list.
        for ct in types.iter() {
            match &ct.kind {
                Kind::OptionalValue(inner) => {
                    h.push_str(&line_doc(
                        "An optional value: `value` is meaningful only when `has_value` is true.",
                    ));
                    writeln!(
                        h,
                        "struct {} {{\n    bool has_value;\n    {};\n}};\n",
                        ct.c,
                        decl(&inner.field(), "value")
                    )?;
                }
                Kind::Bytes => {
                    h.push_str(&line_doc(
                        "Bytes: `len` bytes at `data`, which is NULL when empty.",
                    ));
                    writeln!(
                        h,
                        "struct {} {{\n    const uint8_t *data;\n    size_t len;\n}};\n",
                        ct.c
                    )?;
                    self.render_free(h, s, ct, "these bytes")?;
                }
                Kind::List(inner) => {
                    h.push_str(&line_doc(
                        "A list of `len` items at `data`, which is NULL when empty.",
                    ));
                    writeln!(
                        h,
                        "struct {} {{\n    {}data;\n    size_t len;\n}};\n",
                        ct.c,
                        const_ptr(&inner.field())
                    )?;
                    self.render_free(h, s, ct, "this list and its items")?;
                }
                Kind::Map(key, value) => {
                    h.push_str(&line_doc(
                        "A map: `len` entries, the i-th key at `keys[i]` and its value at `values[i]`.",
                    ));
                    writeln!(
                        h,
                        "struct {} {{\n    {}keys;\n    {}values;\n    size_t len;\n}};\n",
                        ct.c,
                        const_ptr(&key.field()),
                        const_ptr(&value.field())
                    )?;
                    self.render_free(h, s, ct, "this map and its entries")?;
                }
                _ => {}
            }
        }

        // Records and data enums embed each other by value, so define dependencies first.
        for ct in self.sorted_structs(&types)? {
            match ct.kind {
                Kind::Record => self.render_record(h, s, &ct)?,
                Kind::DataEnum => self.render_data_enum(h, s, &ct)?,
                _ => unreachable!(),
            }
        }

        for obj in self.ci.object_definitions() {
            self.render_object(h, s, obj)?;
        }

        for func in self.ci.function_definitions() {
            let name = c_name(func.name());
            let call = format!(
                "::{}::{}",
                self.ci.namespace(),
                filters::fn_name(func.name()).unwrap()
            );
            self.render_callable(h, s, func, &name, None, &call)?;
        }

        // The header declared the types known up front; one found later would lack its struct.
        if self.types.len() != types.len() {
            bail!("the C backend found a type the interface does not list; this is a bug");
        }
        Ok(())
    }

    fn render_flat_enum(&mut self, h: &mut String, ct: &CType) -> Result<()> {
        let e = self.enum_def(&ct.ty)?;
        self.claim(&ct.c)?;
        h.push_str(&doc(e.docstring(), 0));
        writeln!(h, "typedef enum {} {{", ct.c)?;
        for (i, variant) in e.variants().iter().enumerate() {
            let constant = variant_constant(&ct.c, variant.name());
            self.claim(&constant)?;
            h.push_str(&doc(variant.docstring(), 4));
            let comma = if i + 1 < e.variants().len() { "," } else { "" };
            writeln!(h, "    {constant}{comma}")?;
        }
        writeln!(h, "}} {};\n", ct.c)?;
        Ok(())
    }

    fn enum_def(&self, ty: &Type) -> Result<&'a Enum> {
        match ty {
            Type::Enum { name, .. } => self
                .ci
                .get_enum_definition(name)
                .with_context(|| format!("enum `{name}` not found")),
            _ => bail!("not an enum: {ty:?}"),
        }
    }

    fn record_def(&self, ty: &Type) -> Result<&'a Record> {
        match ty {
            Type::Record { name, .. } => self
                .ci
                .get_record_definition(name)
                .with_context(|| format!("record `{name}` not found")),
            _ => bail!("not a record: {ty:?}"),
        }
    }

    /// Records and data enums ordered so each follows the ones it embeds by value.
    fn sorted_structs(&self, types: &[CType]) -> Result<Vec<CType>> {
        let structs: BTreeMap<String, CType> = types
            .iter()
            .filter(|ct| matches!(ct.kind, Kind::Record | Kind::DataEnum))
            .map(|ct| (ct.canon.clone(), ct.clone()))
            .collect();

        let mut sorted = Vec::new();
        let mut done = BTreeSet::new();
        let mut visiting = BTreeSet::new();
        for canon in structs.keys() {
            self.visit(canon, &structs, &mut done, &mut visiting, &mut sorted)?;
        }
        Ok(sorted)
    }

    fn visit(
        &self,
        canon: &str,
        structs: &BTreeMap<String, CType>,
        done: &mut BTreeSet<String>,
        visiting: &mut BTreeSet<String>,
        sorted: &mut Vec<CType>,
    ) -> Result<()> {
        if done.contains(canon) {
            return Ok(());
        }
        if !visiting.insert(canon.to_string()) {
            bail!("`{canon}` contains itself by value, which C cannot represent");
        }
        let ct = &structs[canon];
        let fields: Vec<&Field> = match ct.kind {
            Kind::Record => self.record_def(&ct.ty)?.fields().iter().collect(),
            _ => self
                .enum_def(&ct.ty)?
                .variants()
                .iter()
                .flat_map(|v| v.fields())
                .collect(),
        };
        for field in fields {
            let dep = CppCodeOracle.find(&field.as_type()).canonical_name();
            if structs.contains_key(&dep) {
                self.visit(&dep, structs, done, visiting, sorted)?;
            }
        }
        visiting.remove(canon);
        done.insert(canon.to_string());
        sorted.push(ct.clone());
        Ok(())
    }

    fn render_record(&mut self, h: &mut String, s: &mut String, ct: &CType) -> Result<()> {
        let rec = self.record_def(&ct.ty)?;
        h.push_str(&doc(rec.docstring(), 0));
        writeln!(h, "struct {} {{", ct.c)?;
        for field in rec.fields() {
            let fct = self.ctype(&field.as_type())?;
            h.push_str(&doc(field.docstring(), 4));
            let nullable = if matches!(fct.kind, Kind::Optional(_)) {
                " /* NULL when absent */"
            } else {
                ""
            };
            writeln!(
                h,
                "    {};{nullable}",
                decl(&fct.field(), &filters::var_name(field.name()).unwrap())
            )?;
        }
        writeln!(h, "}};\n")?;
        self.render_free(h, s, ct, "this record's fields and the record")?;

        if rec.fields().iter().any(|f| f.default_value().is_some()) {
            let name = format!("{}_default", ct.c);
            self.claim(&name)?;
            h.push_str(&line_doc(
                "A record with every defaulted field set and the rest zeroed, to fill in before passing it.",
            ));
            writeln!(h, "{} {name}(void);\n", ct.c)?;

            writeln!(s, "{} {name}(void) {{", ct.c)?;
            writeln!(
                s,
                "    {} out;\n    std::memset(&out, 0, sizeof out);",
                ct.c
            )?;
            for field in rec.fields() {
                if let Some(default) = field.default_value() {
                    let fct = self.ctype(&field.as_type())?;
                    if let Some(value) = self.default_literal(&fct, default)? {
                        writeln!(
                            s,
                            "    out.{} = {value};",
                            filters::var_name(field.name()).unwrap()
                        )?;
                    }
                }
            }
            writeln!(s, "    return out;\n}}\n")?;
        }
        Ok(())
    }

    /// The C expression for a field default, or None when zero already is the default.
    fn default_literal(&self, ct: &CType, default: &DefaultValue) -> Result<Option<String>> {
        let literal = match default {
            DefaultValue::Default => {
                return Ok(match &ct.kind {
                    Kind::String => Some("\"\"".into()),
                    Kind::Scalar
                    | Kind::Optional(_)
                    | Kind::OptionalValue(_)
                    | Kind::Bytes
                    | Kind::List(_)
                    | Kind::Map(..) => None,
                    Kind::Record if self.has_default(&ct.ty)? => {
                        Some(format!("{}_default()", ct.c))
                    }
                    Kind::Record => None,
                    _ => bail!("the C backend cannot render a default `{}`", ct.cpp),
                })
            }
            DefaultValue::Literal(literal) => literal,
        };
        Ok(match literal {
            Literal::Boolean(true) => Some("true".into()),
            Literal::Boolean(false)
            | Literal::None
            | Literal::EmptySequence
            | Literal::EmptyMap => None,
            Literal::String(value) => Some(filters::string_literal(value)),
            Literal::UInt(0, ..) | Literal::Int(0, ..) => None,
            Literal::UInt(value, radix, _) => Some(match radix {
                Radix::Hexadecimal => format!("{value:#x}ULL"),
                Radix::Octal => format!("0{value:o}ULL"),
                Radix::Decimal => format!("{value}ULL"),
            }),
            Literal::Int(value, ..) => Some(format!("{value}LL")),
            Literal::Float(value, _) => Some(value.clone()),
            Literal::Enum(variant, _) if matches!(ct.kind, Kind::FlatEnum) => {
                Some(variant_constant(&ct.c, variant))
            }
            _ => bail!(
                "the C backend cannot render the default {literal:?} for `{}`",
                ct.cpp
            ),
        })
    }

    fn has_default(&self, ty: &Type) -> Result<bool> {
        Ok(self
            .record_def(ty)?
            .fields()
            .iter()
            .any(|f| f.default_value().is_some()))
    }

    fn render_data_enum(&mut self, h: &mut String, s: &mut String, ct: &CType) -> Result<()> {
        let e = self.enum_def(&ct.ty)?;
        let error = self.ci.is_name_used_as_error(e.name());
        let tag = format!("{}_tag", ct.c);
        self.claim(&tag)?;

        h.push_str(&line_doc(&format!("Which variant a `{}` holds.", ct.c)));
        writeln!(h, "typedef enum {tag} {{")?;
        for (i, variant) in e.variants().iter().enumerate() {
            let constant = variant_constant(&ct.c, variant.name());
            self.claim(&constant)?;
            h.push_str(&doc(variant.docstring(), 4));
            let comma = if i + 1 < e.variants().len() { "," } else { "" };
            writeln!(h, "    {constant}{comma}")?;
        }
        writeln!(h, "}} {tag};\n")?;

        // C++ forbids types declared inside an anonymous union, so each variant's fields get a
        // named struct, declared first.
        let mut members = Vec::new();
        for variant in e.variants().iter().filter(|v| v.has_fields()) {
            // The anonymous union shares the struct's members, so a variant can't reuse one.
            let member = variant_member(variant.name());
            if member == "tag" || (error && member == "message") {
                bail!(
                    "the C backend cannot name `{}`'s variant `{}` `{member}`, which the struct already uses",
                    e.name(),
                    variant.name()
                );
            }
            let fields = format!("{}_{member}", ct.c);
            self.claim(&fields)?;
            h.push_str(&line_doc(&format!(
                "The fields of a `{}` holding `{}`.",
                ct.c,
                variant_constant(&ct.c, variant.name())
            )));
            writeln!(h, "typedef struct {fields} {{")?;
            for (i, field) in variant.fields().iter().enumerate() {
                let fct = self.ctype(&field.as_type())?;
                h.push_str(&doc(field.docstring(), 4));
                writeln!(
                    h,
                    "    {};",
                    decl(&fct.field(), &c_field_name(field, i + 1))
                )?;
            }
            writeln!(h, "}} {fields};\n")?;
            members.push((fields, member));
        }

        h.push_str(&doc(e.docstring(), 0));
        writeln!(h, "struct {} {{", ct.c)?;
        writeln!(h, "    {tag} tag;")?;
        if !members.is_empty() {
            writeln!(
                h,
                "    /** The fields of the variant `tag` names; variants without fields have no member. */"
            )?;
            writeln!(h, "    union {{")?;
            for (fields, member) in &members {
                writeln!(h, "        {fields} {member};")?;
            }
            writeln!(h, "    }};")?;
        }
        if error {
            writeln!(h, "    /** Describes the error, for logs. */")?;
            writeln!(h, "    const char *message;")?;
        }
        writeln!(h, "}};\n")?;

        if error {
            self.render_free(h, s, ct, "this error")?;
            let name = format!("{}_message", ct.c);
            self.claim(&name)?;
            h.push_str(&line_doc("The error's message, owned by the error."));
            writeln!(h, "const char *{name}(const {} *error);\n", ct.c)?;
            writeln!(
                s,
                "const char *{name}(const {} *error) {{\n    if (!error) {{\n        ::{}::c_detail::fail(\"{name}: error must not be NULL\");\n    }}\n    return error->message;\n}}\n",
                ct.c,
                self.ci.namespace()
            )?;
        } else {
            self.render_free(h, s, ct, "this value's fields and the value")?;
        }
        Ok(())
    }

    /// Declares and defines `<c>_free`, which releases a heap value the API returned.
    fn render_free(
        &mut self,
        h: &mut String,
        s: &mut String,
        ct: &CType,
        what: &str,
    ) -> Result<()> {
        let name = format!("{}_free", ct.c);
        self.claim(&name)?;
        h.push_str(&line_doc(&format!(
            "Frees {what}, as returned by this API; NULL is ignored."
        )));
        writeln!(h, "void {name}({} *value);\n", ct.c)?;
        writeln!(
            s,
            "void {name}({} *value) {{\n    if (!value) {{\n        return;\n    }}\n    ::{ns}::c_detail::free_c_{canon}(*value);\n    std::free(value);\n}}\n",
            ct.c,
            ns = self.ci.namespace(),
            canon = ct.canon
        )?;
        Ok(())
    }

    fn render_object(&mut self, h: &mut String, s: &mut String, obj: &Object) -> Result<()> {
        let c = c_name(obj.name());
        let class = CppCodeOracle.class_name(obj.name());
        let namespace = self.ci.namespace();

        // The object's docstring heads its section, since its typedef sits with the others.
        writeln!(h, "/* {c} */\n")?;
        if let Some(docstring) = obj.docstring() {
            writeln!(h, "{}", doc(Some(docstring), 0))?;
        }

        let free = format!("{c}_free");
        self.claim(&free)?;
        h.push_str(&line_doc(&format!(
            "Releases this `{c}` handle; NULL is ignored. Other handles to the same object stay valid."
        )));
        writeln!(h, "void {free}({c} *self);\n")?;
        writeln!(s, "void {free}({c} *self) {{\n    delete self;\n}}\n")?;

        for ctor in obj.constructors() {
            let (name, call) = if ctor.is_primary_constructor() {
                (format!("{c}_new"), format!("::{namespace}::{class}::init"))
            } else {
                (
                    format!("{c}_{}", c_name(ctor.name())),
                    format!("::{namespace}::{class}::{}", ctor.name()),
                )
            };
            self.render_callable(h, s, ctor, &name, None, &call)?;
        }

        for method in obj.methods() {
            let name = format!("{c}_{}", c_name(method.name()));
            let call = format!("self->inner->{}", filters::fn_name(method.name()).unwrap());
            self.render_callable(h, s, method, &name, Some(&c), &call)?;
        }
        Ok(())
    }

    /// Renders one function, constructor, or method. `call` names the C++ callee.
    fn render_callable(
        &mut self,
        h: &mut String,
        s: &mut String,
        callable: &impl Callable,
        name: &str,
        receiver: Option<&str>,
        call: &str,
    ) -> Result<()> {
        let namespace = self.ci.namespace().to_string();
        let ns = self.ns.clone();
        self.claim(name)?;

        let mut params = Vec::new();
        let mut checks = String::new();
        let mut args = Vec::new();
        let mut names = BTreeSet::new();

        let null_check = |checks: &mut String, param: &str| {
            writeln!(
                checks,
                "    if (!{param}) {{\n        ::{namespace}::c_detail::fail(\"{name}: {param} must not be NULL\");\n    }}"
            )
            .unwrap();
        };

        if let Some(receiver) = receiver {
            params.push(format!("{receiver} *self"));
            names.insert("self".to_string());
            null_check(&mut checks, "self");
        }

        let mut nullable = Vec::new();
        for arg in callable.arguments() {
            let ct = self.ctype(&arg.as_type())?;
            let pname = filters::var_name(arg.name()).unwrap();
            names.insert(pname.clone());
            if matches!(ct.kind, Kind::Optional(_)) {
                nullable.push(format!("`{pname}`"));
            }
            params.push(ct.param(&pname));
            if ct.required_pointer() {
                null_check(&mut checks, &pname);
            }
            let value = if ct.by_pointer() {
                format!("*{pname}")
            } else {
                pname.clone()
            };
            args.push(format!(
                "::{namespace}::c_detail::from_c_{}({value})",
                ct.canon
            ));
        }

        let ret = match callable.return_type() {
            Some(ty) => Some(self.ctype(ty)?),
            None => None,
        };
        let err = match callable.throws_type() {
            Some(ty) => {
                let ct = self.ctype(ty)?;
                if !matches!(ct.kind, Kind::DataEnum) {
                    bail!(
                        "the C backend supports only enum errors, but `{name}` throws `{}`",
                        ct.cpp
                    );
                }
                Some(ct)
            }
            None => None,
        };
        let ret_c = ret.as_ref().map(CType::ret);

        let call = format!("{call}({})", args.join(", "));
        let box_err = |err: &CType, expr: &str| {
            format!(
                "::{namespace}::c_detail::box(::{namespace}::c_detail::to_c_{}({expr}))",
                err.canon
            )
        };

        // Generated notes on nullability and ownership, after the uniffi docstring.
        let mut notes = Vec::new();
        if !nullable.is_empty() {
            notes.push(format!("{} may be NULL.", nullable.join(", ")));
        }
        let ret_free = ret.as_ref().and_then(|ret| ret.free_fn(&ns));
        let absent = ret
            .as_ref()
            .is_some_and(|ret| matches!(ret.kind, Kind::Optional(_)));

        if callable.is_async() {
            for reserved in ["callback", "user_data"] {
                if names.contains(reserved) {
                    bail!(
                        "`{name}` has an argument named `{reserved}`, which the C backend reserves"
                    );
                }
            }
            let callback = format!("{name}_callback");
            self.claim(&callback)?;

            let mut cb_params = vec!["void *user_data".to_string()];
            if let Some(err) = &err {
                cb_params.push(format!("{} *error", err.c));
            }
            if let Some(ret_c) = &ret_c {
                cb_params.push(decl(ret_c, "value"));
            }
            let mut cb_doc = vec![match (&err, &ret) {
                (None, None) => format!("Called once `{name}` completes."),
                _ => format!("Receives the result of `{name}`."),
            }];
            if let Some(err) = &err {
                cb_doc.push(format!(
                    "`error` is NULL on success, or freed with `{}_free`.",
                    err.c
                ));
            }
            if let Some(free) = &ret_free {
                cb_doc.push(format!(
                    "`value` is freed with `{free}`{}.",
                    if absent {
                        "; it is NULL when absent"
                    } else {
                        ""
                    }
                ));
            }
            h.push_str(&line_doc(&cb_doc.join(" ")));
            writeln!(h, "typedef void (*{callback})({});\n", cb_params.join(", "))?;

            params.push(format!("{callback} callback"));
            params.push("void *user_data".to_string());
            null_check(&mut checks, "callback");

            let deliver = match (&err, &ret) {
                (Some(err), Some(ret)) => format!(
                    "[callback, user_data](auto output) {{\n        if (!output) {{\n            callback(user_data, {}, {{}});\n            return;\n        }}\n        callback(user_data, nullptr, {});\n    }}",
                    box_err(err, "output.error()"),
                    ret.ret_expr(&namespace, "*output")
                ),
                (Some(err), None) => format!(
                    "[callback, user_data](auto output) {{\n        if (!output) {{\n            callback(user_data, {});\n            return;\n        }}\n        callback(user_data, nullptr);\n    }}",
                    box_err(err, "output.error()")
                ),
                (None, Some(ret)) => format!(
                    "[callback, user_data](auto output) {{\n        callback(user_data, {});\n    }}",
                    ret.ret_expr(&namespace, "output")
                ),
                (None, None) => {
                    "[callback, user_data]() {\n        callback(user_data);\n    }".to_string()
                }
            };

            let signature = format!("{ns}_task *{name}({})", params.join(", "));
            notes.push(
                "Calls `callback` once on the dispatcher, unless the returned task is freed first."
                    .into(),
            );
            h.push_str(&doc(callable.docstring(), 0));
            h.push_str(&line_doc(&notes.join(" ")));
            writeln!(h, "{signature};\n")?;
            writeln!(
                s,
                "{signature} {{\n{checks}    return ::{namespace}::c_detail::spawn({call}, {deliver});\n}}\n"
            )?;
            return Ok(());
        }

        let (ret_decl, body) = match (&err, &ret) {
            (Some(err), Some(ret)) => {
                if names.contains("out") {
                    bail!("`{name}` has an argument named `out`, which the C backend reserves");
                }
                params.push(decl(&pointer(ret_c.as_ref().unwrap()), "out"));
                null_check(&mut checks, "out");
                (
                    format!("{} *", err.c),
                    format!(
                        "    auto result = {call};\n    if (!result) {{\n        *out = {{}};\n        return {};\n    }}\n    *out = {};\n    return nullptr;\n",
                        box_err(err, "result.error()"),
                        ret.ret_expr(&namespace, "*result")
                    ),
                )
            }
            (Some(err), None) => (
                format!("{} *", err.c),
                format!(
                    "    auto result = {call};\n    if (!result) {{\n        return {};\n    }}\n    return nullptr;\n",
                    box_err(err, "result.error()")
                ),
            ),
            (None, Some(ret)) => (
                ret_c.clone().unwrap(),
                format!("    return {};\n", ret.ret_expr(&namespace, &call)),
            ),
            (None, None) => ("void".to_string(), format!("    {call};\n")),
        };

        if let Some(err) = &err {
            let out = if ret.is_some() { " and sets `out`" } else { "" };
            notes.push(format!(
                "Returns NULL on success{out}, or an error the caller frees with `{}_free`.",
                err.c
            ));
        }
        if let Some(free) = &ret_free {
            let result = if err.is_some() { "`out`" } else { "the result" };
            notes.push(format!(
                "The caller frees {result} with `{free}`{}.",
                if absent {
                    "; it is NULL when absent"
                } else {
                    ""
                }
            ));
        }
        h.push_str(&doc(callable.docstring(), 0));
        if !notes.is_empty() {
            h.push_str(&line_doc(&notes.join(" ")));
        }
        let params = if params.is_empty() {
            "void".to_string()
        } else {
            params.join(", ")
        };
        let signature = decl(&ret_decl, &format!("{name}({params})"));
        writeln!(h, "{signature};\n")?;
        writeln!(s, "{signature} {{\n{checks}{body}}}\n")?;
        Ok(())
    }

    /// Declares every converter, then defines them, so their order does not matter.
    fn render_converters(&self, out: &mut String) -> Result<()> {
        for ct in self.types.values() {
            let field = ct.field();
            writeln!(
                out,
                "{};",
                decl(
                    &field,
                    &format!("to_c_{}(const {} &value)", ct.canon, ct.cpp)
                )
            )?;
            writeln!(
                out,
                "{} from_c_{}({});",
                ct.cpp,
                ct.canon,
                decl(&ct.view(), "value")
            )?;
            writeln!(
                out,
                "void free_c_{}({});",
                ct.canon,
                decl(&format!("{field} &"), "value")
            )?;
        }
        out.push('\n');
        for ct in self.types.values() {
            self.render_converter(out, ct)?;
        }
        Ok(())
    }

    fn render_converter(&self, out: &mut String, ct: &CType) -> Result<()> {
        let field = ct.field();
        let to = decl(
            &field,
            &format!("to_c_{}(const {} &value)", ct.canon, ct.cpp),
        );
        let from = format!(
            "{} from_c_{}({})",
            ct.cpp,
            ct.canon,
            decl(&ct.view(), "value")
        );
        let free = format!(
            "void free_c_{}({})",
            ct.canon,
            decl(&format!("{field} &"), "value")
        );
        match &ct.kind {
            Kind::Scalar => {
                writeln!(out, "{to} {{\n    return value;\n}}\n")?;
                writeln!(out, "{from} {{\n    return value;\n}}\n")?;
                writeln!(out, "{free} {{\n    (void)value;\n}}\n")?;
            }
            Kind::FlatEnum => {
                let e = self.enum_def(&ct.ty)?;
                let mut to_cases = String::new();
                let mut from_cases = String::new();
                for variant in e.variants() {
                    let cpp_variant = format!(
                        "{}::{}",
                        ct.cpp,
                        CppCodeOracle.enum_variant_name(variant.name(), self.config.enum_style())
                    );
                    let constant = variant_constant(&ct.c, variant.name());
                    writeln!(
                        to_cases,
                        "    case {cpp_variant}:\n        return {constant};"
                    )?;
                    writeln!(
                        from_cases,
                        "    case {constant}:\n        return {cpp_variant};"
                    )?;
                }
                writeln!(
                    out,
                    "{to} {{\n    switch (value) {{\n{to_cases}    }}\n    fail(\"unknown {} variant\");\n}}\n",
                    ct.c
                )?;
                writeln!(
                    out,
                    "{from} {{\n    switch (value) {{\n{from_cases}    }}\n    fail(\"invalid {} value\");\n}}\n",
                    ct.c
                )?;
                writeln!(out, "{free} {{\n    (void)value;\n}}\n")?;
            }
            Kind::String => {
                writeln!(out, "{to} {{\n    return copy_string(value);\n}}\n")?;
                writeln!(
                    out,
                    "{from} {{\n    if (!value) {{\n        fail(\"a string must not be NULL\");\n    }}\n    return std::string(value);\n}}\n"
                )?;
                writeln!(out, "{free} {{\n    std::free(const_cast<char *>(value));\n    value = nullptr;\n}}\n")?;
            }
            Kind::Bytes => {
                writeln!(
                    out,
                    "{to} {{\n    {field} out{{}};\n    out.len = value.size();\n    if (!value.empty()) {{\n        auto *data = static_cast<uint8_t *>(alloc(value.size()));\n        std::memcpy(data, value.data(), value.size());\n        out.data = data;\n    }}\n    return out;\n}}\n"
                )?;
                writeln!(
                    out,
                    "{from} {{\n    if (!value.data && value.len) {{\n        fail(\"bytes with a length must have data\");\n    }}\n    if (!value.len) {{\n        return {{}};\n    }}\n    return {}(value.data, value.data + value.len);\n}}\n",
                    ct.cpp
                )?;
                writeln!(out, "{free} {{\n    std::free(const_cast<uint8_t *>(value.data));\n    value = {{}};\n}}\n")?;
            }
            Kind::Record => {
                let rec = self.record_def(&ct.ty)?;
                let mut to_body = String::new();
                let mut from_items = Vec::new();
                let mut free_body = String::new();
                for field in rec.fields() {
                    let fcanon = CppCodeOracle.find(&field.as_type()).canonical_name();
                    let name = filters::var_name(field.name()).unwrap();
                    writeln!(to_body, "    out.{name} = to_c_{fcanon}(value.{name});")?;
                    from_items.push(format!("        from_c_{fcanon}(value.{name})"));
                    writeln!(free_body, "    free_c_{fcanon}(value.{name});")?;
                }
                writeln!(
                    out,
                    "{to} {{\n    {field} out{{}};\n{to_body}    return out;\n}}\n"
                )?;
                writeln!(
                    out,
                    "{from} {{\n    return {}{{\n{}\n    }};\n}}\n",
                    ct.cpp,
                    from_items.join(",\n")
                )?;
                writeln!(out, "{free} {{\n{free_body}}}\n")?;
            }
            Kind::DataEnum => self.render_data_enum_converter(out, ct, &to, &from, &free)?,
            Kind::Object => {
                writeln!(
                    out,
                    "{to} {{\n    if (!value) {{\n        fail(\"unexpected null {}\");\n    }}\n    return new {}{{value}};\n}}\n",
                    ct.c, ct.c
                )?;
                writeln!(
                    out,
                    "{from} {{\n    if (!value) {{\n        fail(\"a {} must not be NULL\");\n    }}\n    return value->inner;\n}}\n",
                    ct.c
                )?;
                writeln!(
                    out,
                    "{free} {{\n    delete value;\n    value = nullptr;\n}}\n"
                )?;
            }
            Kind::Optional(inner) => {
                let icanon = &inner.canon;
                match inner.kind {
                    // A null shared_ptr is the C++ binding's empty optional object.
                    Kind::Object => {
                        writeln!(
                            out,
                            "{to} {{\n    return value ? new {}{{value}} : nullptr;\n}}\n",
                            inner.c
                        )?;
                        writeln!(
                            out,
                            "{from} {{\n    return value ? value->inner : nullptr;\n}}\n"
                        )?;
                        writeln!(out, "{free} {{\n    free_c_{icanon}(value);\n}}\n")?;
                    }
                    Kind::String => {
                        writeln!(
                            out,
                            "{to} {{\n    return value ? to_c_{icanon}(*value) : nullptr;\n}}\n"
                        )?;
                        writeln!(
                            out,
                            "{from} {{\n    if (!value) {{\n        return std::nullopt;\n    }}\n    return std::string(value);\n}}\n"
                        )?;
                        writeln!(out, "{free} {{\n    free_c_{icanon}(value);\n}}\n")?;
                    }
                    _ => {
                        writeln!(
                            out,
                            "{to} {{\n    if (!value) {{\n        return nullptr;\n    }}\n    return box(to_c_{icanon}(*value));\n}}\n"
                        )?;
                        writeln!(
                            out,
                            "{from} {{\n    if (!value) {{\n        return std::nullopt;\n    }}\n    return from_c_{icanon}(*value);\n}}\n"
                        )?;
                        writeln!(
                            out,
                            "{free} {{\n    if (value) {{\n        free_c_{icanon}(*value);\n        std::free(value);\n        value = nullptr;\n    }}\n}}\n"
                        )?;
                    }
                }
            }
            Kind::OptionalValue(inner) => {
                let icanon = &inner.canon;
                writeln!(
                    out,
                    "{to} {{\n    {field} out{{}};\n    if (value) {{\n        out.has_value = true;\n        out.value = to_c_{icanon}(*value);\n    }}\n    return out;\n}}\n"
                )?;
                writeln!(
                    out,
                    "{from} {{\n    if (!value.has_value) {{\n        return std::nullopt;\n    }}\n    return from_c_{icanon}(value.value);\n}}\n"
                )?;
                writeln!(out, "{free} {{\n    (void)value;\n}}\n")?;
            }
            Kind::List(inner) => {
                let item = inner.field();
                let icanon = &inner.canon;
                writeln!(
                    out,
                    "{to} {{\n    using Item = {item};\n    {field} out{{}};\n    out.len = value.size();\n    if (!value.empty()) {{\n        auto *data = static_cast<Item *>(alloc(sizeof(Item) * value.size()));\n        for (size_t i = 0; i < value.size(); ++i) {{\n            data[i] = to_c_{icanon}(value[i]);\n        }}\n        out.data = data;\n    }}\n    return out;\n}}\n"
                )?;
                writeln!(
                    out,
                    "{from} {{\n    if (!value.data && value.len) {{\n        fail(\"a list with a length must have data\");\n    }}\n    {} out;\n    out.reserve(value.len);\n    for (size_t i = 0; i < value.len; ++i) {{\n        out.push_back(from_c_{icanon}(value.data[i]));\n    }}\n    return out;\n}}\n",
                    ct.cpp
                )?;
                writeln!(
                    out,
                    "{free} {{\n    using Item = {item};\n    auto *data = const_cast<Item *>(value.data);\n    for (size_t i = 0; i < value.len; ++i) {{\n        free_c_{icanon}(data[i]);\n    }}\n    std::free(data);\n    value = {{}};\n}}\n"
                )?;
            }
            Kind::Map(key, val) => {
                let (kitem, vitem) = (key.field(), val.field());
                let (kcanon, vcanon) = (&key.canon, &val.canon);
                writeln!(
                    out,
                    "{to} {{\n    using Key = {kitem};\n    using Value = {vitem};\n    {field} out{{}};\n    out.len = value.size();\n    if (!value.empty()) {{\n        auto *keys = static_cast<Key *>(alloc(sizeof(Key) * value.size()));\n        auto *values = static_cast<Value *>(alloc(sizeof(Value) * value.size()));\n        size_t i = 0;\n        for (const auto &entry : value) {{\n            keys[i] = to_c_{kcanon}(entry.first);\n            values[i] = to_c_{vcanon}(entry.second);\n            ++i;\n        }}\n        out.keys = keys;\n        out.values = values;\n    }}\n    return out;\n}}\n"
                )?;
                writeln!(
                    out,
                    "{from} {{\n    if ((!value.keys || !value.values) && value.len) {{\n        fail(\"a map with a length must have keys and values\");\n    }}\n    {} out;\n    for (size_t i = 0; i < value.len; ++i) {{\n        if (!out.emplace(from_c_{kcanon}(value.keys[i]), from_c_{vcanon}(value.values[i])).second) {{\n            fail(\"a map must not repeat a key\");\n        }}\n    }}\n    return out;\n}}\n",
                    ct.cpp
                )?;
                writeln!(
                    out,
                    "{free} {{\n    using Key = {kitem};\n    using Value = {vitem};\n    auto *keys = const_cast<Key *>(value.keys);\n    auto *values = const_cast<Value *>(value.values);\n    for (size_t i = 0; i < value.len; ++i) {{\n        free_c_{kcanon}(keys[i]);\n        free_c_{vcanon}(values[i]);\n    }}\n    std::free(keys);\n    std::free(values);\n    value = {{}};\n}}\n"
                )?;
            }
        }
        Ok(())
    }

    fn render_data_enum_converter(
        &self,
        out: &mut String,
        ct: &CType,
        to: &str,
        from: &str,
        free: &str,
    ) -> Result<()> {
        let e = self.enum_def(&ct.ty)?;
        let error = self.ci.is_name_used_as_error(e.name());
        let display = e.uniffi_trait_methods().display_fmt.is_some();
        // Under the expected style each variant of a flat error carries its message.
        let flat_error = error && e.is_flat();
        let field = ct.field();

        let mut to_cases = String::new();
        let mut from_cases = String::new();
        let mut free_cases = String::new();
        for (index, variant) in e.variants().iter().enumerate() {
            let constant = variant_constant(&ct.c, variant.name());
            let cpp_variant = format!(
                "{}::{}",
                ct.cpp,
                CppCodeOracle.enum_variant_name(variant.name(), self.config.enum_style())
            );
            let member = variant_member(variant.name());

            writeln!(
                to_cases,
                "    case {index}: {{\n        out.tag = {constant};"
            )?;
            if variant.has_fields() || flat_error {
                writeln!(
                    to_cases,
                    "        const auto &inner = std::get<{index}>(variant);"
                )?;
            }
            let mut from_items = Vec::new();
            let mut free_case = String::new();
            for (i, f) in variant.fields().iter().enumerate() {
                let fcanon = CppCodeOracle.find(&f.as_type()).canonical_name();
                let name = c_field_name(f, i + 1);
                writeln!(
                    to_cases,
                    "        out.{member}.{name} = to_c_{fcanon}(inner.{name});"
                )?;
                from_items.push(format!("from_c_{fcanon}(value.{member}.{name})"));
                writeln!(free_case, "        free_c_{fcanon}(value.{member}.{name});")?;
            }
            if error && !display {
                let message = if flat_error {
                    "inner.message".to_string()
                } else {
                    filters::string_literal(variant.name())
                };
                writeln!(to_cases, "        out.message = copy_string({message});")?;
            }
            writeln!(to_cases, "        break;\n    }}")?;

            if flat_error {
                from_items
                    .push("value.message ? std::string(value.message) : std::string()".into());
            }
            writeln!(
                from_cases,
                "    case {constant}:\n        return {}({cpp_variant}{{{}}});",
                ct.cpp,
                from_items.join(", ")
            )?;
            if !free_case.is_empty() {
                writeln!(
                    free_cases,
                    "    case {constant}:\n{free_case}        break;"
                )?;
            }
        }

        let message = if error && display {
            "    out.message = copy_string(value.to_string());\n"
        } else {
            ""
        };
        writeln!(
            out,
            "{to} {{\n    {field} out{{}};\n    const auto &variant = value.get_variant();\n    switch (variant.index()) {{\n{to_cases}    default:\n        fail(\"unknown {} variant\");\n    }}\n{message}    return out;\n}}\n",
            ct.c
        )?;
        writeln!(
            out,
            "{from} {{\n    switch (value.tag) {{\n{from_cases}    }}\n    fail(\"invalid {} tag\");\n}}\n",
            ct.c
        )?;
        let free_message = if error {
            "    std::free(const_cast<char *>(value.message));\n    value.message = nullptr;\n"
        } else {
            ""
        };
        let switch = if free_cases.is_empty() {
            String::new()
        } else {
            format!("    switch (value.tag) {{\n{free_cases}    default:\n        break;\n    }}\n")
        };
        writeln!(out, "{free} {{\n{switch}{free_message}}}\n")?;
        Ok(())
    }
}

/// A pointer to const items, `const T *`, or `T const *` when `T` is itself a pointer.
fn const_ptr(item: &str) -> String {
    if item.ends_with('*') {
        format!("{item} const *")
    } else {
        format!("const {item} *")
    }
}

/// A pointer to `ty`.
fn pointer(ty: &str) -> String {
    if ty.ends_with('*') {
        format!("{ty}*")
    } else {
        format!("{ty} *")
    }
}

/// The constant naming one variant of a C enum, like `MOQ_TRANSPORT_QUIC`.
fn variant_constant(c: &str, variant: &str) -> String {
    format!(
        "{}_{}",
        c.to_shouty_snake_case(),
        variant.to_shouty_snake_case()
    )
}

/// The union member holding one variant's fields.
fn variant_member(variant: &str) -> String {
    filters::var_name(&variant.to_snake_case()).unwrap()
}
