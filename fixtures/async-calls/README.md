# Async calls fixture

This fixture is adapted from the MPL-2.0 licensed
[`async-calls` fixture](https://github.com/jhugman/uniffi-bindgen-react-native/tree/main/fixtures/async-calls)
in `uniffi-bindgen-react-native`.

The public async cases are retained, including the hybrid UDL/proc-macro API, functions,
constructors, object methods, records, optional objects, errors, and timeouts. The adaptation:

- uses `futures-timer` in place of the upstream repository's private timer service;
- includes only the native shared-resource implementation needed by the C++ tests; and
- exports the upstream `void` function as `void_return`, since `void` is a C++ keyword.
