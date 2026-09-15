{%- match func.return_type() %}
{%- when Some with (return_type) %}
{% if func.is_async() %}uniffi::Future<{% endif %}{{ return_type|type_name(ci) }}{% if func.is_async() %}>{% endif %} {{ func.name()|fn_name }}({% call macros::param_list(func) %}) {
    {%- if func.is_async() %}
    return {% call macros::rust_call_async(func, return_type|type_name(ci)) %};
    {%- else %}
    auto ret = {% call macros::rust_call(func) %};

    return uniffi::{{ return_type|lift_fn }}(ret);
    {%- endif %}
}
{%- when None -%}
{% if func.is_async() %}uniffi::Future<{% endif %}void{% if func.is_async() %}>{% endif %} {{ func.name()|fn_name }}({% call macros::param_list(func) %}) {
    {%- if func.is_async() %}
    return {% call macros::rust_call_async_void(func) %};
    {%- else %}
    {% call macros::rust_call(func) %};
    {%- endif %}
}
{%- endmatch -%}
