# uniffi-bindgen-cpp - UniFFI C++ bindings/scaffolding generator

Generate [UniFFI](https://github.com/mozilla/uniffi-rs) bindings for C++. `uniffi-bindgen-cpp` lives
as a separate project from `uniffi-rs`, as per
[uniffi-rs #1355](https://github.com/mozilla/uniffi-rs/issues/1355).

# How to install

Minimum Rust version required to install `uniffi-bindgen-cpp` is `1.91`, which UniFFI 0.32 requires.
Newer Rust versions should also work fine.

```bash
cargo install uniffi-bindgen-cpp --git https://github.com/NordSecurity/uniffi-bindgen-cpp --tag v0.8.1+v0.29.4
```

This fork carries LiveKit's async support (`livekit/uniffi-0.31-async`) ported to UniFFI 0.32, plus
the `error_style = "expected"` option. Its tags add a `-kixelated.N` pre-release so they never
collide with upstream's:

```bash
cargo install uniffi-bindgen-cpp --locked --git https://github.com/kixelated/uniffi-bindgen-cpp --tag v0.11.0-kixelated.1+v0.32.2
```

# How to generate bindings

## Generating with a single UDL file

```bash
uniffi-bindgen-cpp path/to/definitions.udl
```

## Generating using a library file

```bash
uniffi-bindgen-cpp --library your_rust_library.so --out-dir output_directory
```

Regardless of the generation method, these files are produced:

* `path/to/definitions.hpp`
* `path/to/definitions.cpp`
* `path/to/definitions_scaffolding.hpp`

# How to integrate bindings

To integrate the bindings into your projects, simply add the generated bindings files to your project.
C++17 or newer is required to compile the bindings.

Async calls return a move-only `uniffi::Future<T>`. It supports `get()`, `wait()`, `wait_for()`,
and `cancel()`. Destroying an incomplete future cancels it and releases the Rust future. See
[the LiveKit integration notes](docs/LIVEKIT_INTEGRATION.md) for the migration shape and current
tradeoffs.

To adapt without a blocking waiter thread, consume the future with
`std::move(future).then(executor, callback)`. The callback receives `uniffi::FutureResult<T>` and
the returned `uniffi::FutureContinuation` owns cancellation until completion. Destroying that
token early cancels the Rust operation.

C++ implementations of async callback interfaces return `uniffi::ForeignFuture<T>`. Its start
function receives success and failure callbacks and returns a cancellation function. The generated
bridge accepts the first completion and ignores duplicates, allowing the implementation to use its
own executor while preserving UniFFI typed-error and cancellation semantics.

Rust-future continuations use one bounded background dispatcher by default. Before making any async
call, applications can call `uniffi::set_async_dispatcher(dispatch, shutdown)` to use their own
executor. Before unloading the executor or generated bindings, call
`uniffi::shutdown_async_dispatcher()`; it rejects new continuations and waits for the registered
shutdown function to drain accepted work.

# Configuration options

It's possible to [configure some settings](docs/CONFIGURATION.md) by passing `--config`
argument to the generator.

Note: configuration is not supported when using library mode

```bash
uniffi-bindgen-cpp path/to/definitions.udl --config path/to/uniffi.toml
```

# C++ Scaffolding

It is possible to generate C++ uniffi scaffolding that allows bridging C++ code with any other uniffi supported language (except Rust).
More documentation and limitations can be found in the [scaffolding documentation](docs/SCAFFOLDING.md)

# Versioning

`uniffi-bindgen-cpp` is versioned separately from `uniffi-rs`. UniFFI follows the [SemVer rules from
the Cargo Book](https://doc.rust-lang.org/cargo/reference/resolver.html#semver-compatibility)
which states "Versions are considered compatible if their left-most non-zero
major/minor/patch component is the same". A breaking change is any modification to the C++ bindings
that demands the consumer of the bindings to make corresponding changes to their code to ensure that
the bindings continue to function properly. `uniffi-bindgen-cpp` is young, and it's unclear how stable
the generated bindings are going to be between versions. For this reason, major version is currently
0, and most changes are probably going to bump minor version.

To ensure consistent feature set across external binding generators, `uniffi-bindgen-cpp` targets
a specific `uniffi-rs` version. A consumer using Go bindings (in `uniffi-bindgen-go`) and C#
bindings (in `uniffi-bindgen-cs`) expects the same features to be available across multiple bindings
generators. This means that the consumer should choose external binding generator versions such that
each generator targets the same `uniffi-rs` version.

To simplify this choice `uniffi-bindgen-cpp`, `uniffi-bindgen-cs` and `uniffi-bindgen-go` use tag naming convention
as follows: `vX.Y.Z+vA.B.C`, where `X.Y.Z` is the version of the generator itself, and `A.B.C` is
the version of uniffi-rs it is based on.

The table shows `uniffi-rs` version history for tags that were published before tag naming convention described above was introduced.

| uniffi-bindgen-cpp version               | uniffi-rs version                                |
|------------------------------------------|--------------------------------------------------|
| v0.1.0                                   | v0.25.0                                          |
| v0.2.0                                   | v0.25.0                                          |
| v0.2.1                                   | v0.25.0                                          |
| v0.2.2                                   | v0.25.0                                          |
| v0.7.0                                   | v0.28.3                                          |
| v0.8.0                                   | v0.29.4                                          |

# Documentation

More documentation is available in [docs](docs) directory.

# Contributing

For contribution guidelines, read [CONTRIBUTING.md](CONTRIBUTING.md).
