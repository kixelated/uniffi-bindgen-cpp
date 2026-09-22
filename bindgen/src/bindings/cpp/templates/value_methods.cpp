{%- for method in methods %}
{%- if !method.is_async() %}
{% match method.return_type() %}{% when Some with (return_type) %}{{ return_type|type_name(ci) }} {% else %}void {% endmatch -%}
{{ type_name }}::{{ method.name()|fn_name }}({% call macros::param_list(method) %}) const {
    {%- match method.return_type() %}
    {%- when Some with (return_type) %}
    return uniffi::{{ return_type|lift_fn }}({% call macros::rust_call_with_self(method, typ) %});
    {%- when None %}
    {% call macros::rust_call_with_self(method, typ) %};
    {%- endmatch %}
}
{%- endif %}
{%- endfor %}

{%- if let Some(method) = uniffi_trait_methods.display_fmt %}
std::string {{ type_name }}::to_string() const {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_self(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.debug_fmt %}
std::string {{ type_name }}::to_debug_string() const {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_self(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.eq_eq %}
bool {{ type_name }}::eq(const {{ type_name }} &other) const {
    return uniffi::{{ Type::Boolean.borrow()|lift_fn }}({% call macros::rust_call_with_self_and_other(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.eq_ne %}
bool {{ type_name }}::ne(const {{ type_name }} &other) const {
    return uniffi::{{ Type::Boolean.borrow()|lift_fn }}({% call macros::rust_call_with_self_and_other(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.hash_hash %}
uint64_t {{ type_name }}::hash() const {
    return uniffi::{{ Type::UInt64.borrow()|lift_fn }}({% call macros::rust_call_with_self(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.ord_cmp %}
int8_t {{ type_name }}::cmp(const {{ type_name }} &other) const {
    return uniffi::{{ Type::Int8.borrow()|lift_fn }}({% call macros::rust_call_with_self_and_other(method, typ) %});
}
{%- endif %}
