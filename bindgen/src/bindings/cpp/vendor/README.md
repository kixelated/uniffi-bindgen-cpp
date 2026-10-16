`tl_expected.hpp` is [TartanLlama/expected](https://github.com/TartanLlama/expected) v1.1.0
(`include/tl/expected.hpp`) under CC0. The `error_style = "expected"` bindings ship it as their
`expected` type at every C++ standard. One change: with exceptions disabled, `value()` on an error
prints a message and aborts instead of being `__builtin_unreachable()`.
