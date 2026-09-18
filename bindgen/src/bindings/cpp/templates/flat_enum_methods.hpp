{%- for method in methods %}
{%- if !method.is_async() %}
{%- call macros::docstring(method, 0) %}
{% match method.return_type() %}{% when Some with (return_type) %}{{ return_type|type_name(ci) }} {% else %}void {% endmatch -%}
{{ method.name()|fn_name }}({{ type_name }} _uniffi_self{% if !method.arguments().is_empty() %}, {% endif %}{% call macros::param_list(method) %});
{%- endif %}
{%- endfor %}

{%- if uniffi_trait_methods.display_fmt.is_some() %}
/** Calls the value's Rust `Display` implementation. */
std::string to_string({{ type_name }} _uniffi_self);
{%- endif %}
{%- if uniffi_trait_methods.debug_fmt.is_some() %}
/** Calls the value's Rust `Debug` implementation. */
std::string to_debug_string({{ type_name }} _uniffi_self);
{%- endif %}
{%- if uniffi_trait_methods.eq_eq.is_some() %}
/** Calls the value's Rust `Eq` implementation. */
bool eq({{ type_name }} _uniffi_self, {{ type_name }} other);
/** Calls the value's Rust `Ne` implementation. */
bool ne({{ type_name }} _uniffi_self, {{ type_name }} other);
{%- endif %}
{%- if uniffi_trait_methods.hash_hash.is_some() %}
/** Calls the value's Rust `Hash` implementation. */
uint64_t hash({{ type_name }} _uniffi_self);
{%- endif %}
{%- if uniffi_trait_methods.ord_cmp.is_some() %}
/** Calls the value's Rust `Ord` implementation. */
int8_t cmp({{ type_name }} _uniffi_self, {{ type_name }} other);
{%- endif %}
