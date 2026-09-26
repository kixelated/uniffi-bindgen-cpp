{%- if name|enum_struct(ci) && !(name|error_class(ci)) %}
{%- let trait_methods = e.uniffi_trait_methods() %}
{%- match trait_methods.display_fmt %}
{%- when Some with (fmt) %}
std::string {{ type_name }}::to_string() const {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("uniffi::" ~ ffi_converter_name ~ "::lower(*this)", fmt) %});
}
{%- else %}
{%- endmatch %}
{%- match trait_methods.debug_fmt %}
{%- when Some with (fmt) %}
std::string {{ type_name }}::to_debug_string() const {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("uniffi::" ~ ffi_converter_name ~ "::lower(*this)", fmt) %});
}
{%- else %}
{%- endmatch %}
{%- endif %}
