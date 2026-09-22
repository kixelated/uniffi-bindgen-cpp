{%- for method in methods %}
{%- if !method.is_async() %}
{% match method.return_type() %}{% when Some with (return_type) %}{{ return_type|type_name(ci) }} {% else %}void {% endmatch -%}
{{ method.name()|fn_name }}({{ type_name }} _uniffi_self{% if !method.arguments().is_empty() %}, {% endif %}{% call macros::param_list(method) %}) {
    {%- match method.return_type() %}
    {%- when Some with (return_type) %}
    return uniffi::{{ return_type|lift_fn }}({% call macros::rust_call_with_value(method, typ) %});
    {%- when None %}
    {% call macros::rust_call_with_value(method, typ) %};
    {%- endmatch %}
}
{%- endif %}
{%- endfor %}

{%- if let Some(method) = uniffi_trait_methods.display_fmt %}
std::string to_string({{ type_name }} _uniffi_self) {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_value(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.debug_fmt %}
std::string to_debug_string({{ type_name }} _uniffi_self) {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_value(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.eq_eq %}
bool eq({{ type_name }} _uniffi_self, {{ type_name }} other) {
    return uniffi::{{ Type::Boolean.borrow()|lift_fn }}({% call macros::rust_call_with_value(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.eq_ne %}
bool ne({{ type_name }} _uniffi_self, {{ type_name }} other) {
    return uniffi::{{ Type::Boolean.borrow()|lift_fn }}({% call macros::rust_call_with_value(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.hash_hash %}
uint64_t hash({{ type_name }} _uniffi_self) {
    return uniffi::{{ Type::UInt64.borrow()|lift_fn }}({% call macros::rust_call_with_value(method, typ) %});
}
{%- endif %}
{%- if let Some(method) = uniffi_trait_methods.ord_cmp %}
int8_t cmp({{ type_name }} _uniffi_self, {{ type_name }} other) {
    return uniffi::{{ Type::Int8.borrow()|lift_fn }}({% call macros::rust_call_with_value(method, typ) %});
}
{%- endif %}
