# Configuration options

It's possible to configure some settings by passing `--config` argument to the generator. All
configuration keys are defined in `bindings.cpp` section.

```bash
uniffi-bindgen-cpp path/to/definitions.udl --config path/to/uniffi.toml
```

- `custom_types` - properties for custom type defined in UDL with `[Custom] typedef string Url;`.

    ```toml
    # Represent MyString as a C++ native `CustomString` class. The underlying type of MyString is a string.
    [bindings.cpp.custom_types.MyString]
    imports = ["CustomString.hpp"]
    type_name = "CustomString"
    into_custom = "CustomString({})"
    from_custom = "{}.to_string()"
    ```

  - `imports` (optional) - any imports required to satisfy this type.

  - `type_name` (optional) - the name to represent the type in generated bindings. Default is the
        type alias name from UDL, e.g. `Url`.

  - `into_custom` (required) - an expression to convert from the underlying type into custom type. `{}` will
        will be expanded into variable containing the underlying value. The expression is used in a
        return statement, i.e. `return <expression(value)>;`.

  - `from_custom` (required) - an expression to convert from the custom type into underlying type. `{}` will
        will be expanded into variable containing the custom value. The expression is used in a
        return statement, i.e. `return <expression(value);>`.

- `enum_style` - style for enum variant naming, possible options are:
  - `"Capitalized"` - producing enum variants named `ENUM_VARIANT`
  - `"Google"` - producing enum variants name `kEnumVariant` (default)

NOTE: the `enum_style` option is separate for bindings and scaffolding generators, to apply this to the scaffolding generator, use the section `[scaffolding.cpp]` instead.

- `error_style` - how a Rust `Result` error reaches C++, possible options are:
  - `"exceptions"` - throw the error (default)
  - `"expected"` - return it, and never throw, so the bindings build with `-fno-exceptions`

- `strip_type_prefix` - a prefix to drop from type names, for crates that prefix their exported
    types to keep them unique in languages without namespaces. With `strip_type_prefix = "Foo"`, the
    records, objects, enums, and callback interfaces `FooClient` and `FooError` become
    `Client` and `Error`. A name continues only after an uppercase letter, so `Foobar` stays as is,
    and custom types keep their names. Generation fails if a stripped name is already a type.

    ```toml
    [bindings.cpp]
    strip_type_prefix = "Foo"
    ```

### `error_style = "expected"`

A fallible function returns `uniffi::expected<T, E>`, and an async one returns
`uniffi::Future<T, E>` whose `get()` and `then()` continuation deliver the same `expected`. An
infallible async function returns `uniffi::Future<T>`, which delivers a plain `T`.
`uniffi::expected` is a bundled [`tl::expected`](https://github.com/TartanLlama/expected), written
next to the bindings as `uniffi_expected.hpp`. It is never `std::expected`, even at C++23, so
generated sources built at one standard link with code built at another. With exceptions disabled,
`value()` on an error aborts with a message.

Error enums are plain values rather than exception classes: every error enum becomes a struct
holding a `std::variant` of its variants, like any other enum with fields. A flat error
(`#[uniffi(flat_error)]`, or a UDL `[Error] enum`) gives each variant a
`message` field holding the Rust `Display` text.

Failures that have no error value abort with a message on `stderr` instead of throwing: a Rust
panic, a contract or checksum mismatch, an unknown enum variant, consuming a future twice, and
misusing the dispatcher. Cancelling a future or dropping it abandons it: its continuation never
runs, and a later `get()` on it aborts. A dispatcher that rejects a task abandons the future the
same way, so the default dispatcher's queue is unbounded under this style; after
`shutdown_async_dispatcher()` every pending future is abandoned.

Callback interfaces and trait interfaces implemented in C++ are not supported yet under this style,
and generation fails if the component declares one.

