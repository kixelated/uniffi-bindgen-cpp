{%- let fallible = config.expected() && func.throws_type().is_some() %}
{%- match func.return_type() %}
{%- when Some with (return_type) %}
{% call macros::result_type(func, return_type|type_name(ci)) %} {{ func.name()|fn_name }}({% call macros::param_list(func) %}) {
    {%- if func.is_async() %}
    return {% call macros::rust_call_async(func, return_type|type_name(ci)) %};
    {%- else if fallible %}
    return uniffi::lift_expected<{{ return_type|type_name(ci) }}>({% call macros::rust_call(func) %}, [](auto ret) {
        return uniffi::{{ return_type|lift_fn }}(ret);
    });
    {%- else %}
    auto ret = {% call macros::rust_call(func) %};

    return uniffi::{{ return_type|lift_fn }}(ret);
    {%- endif %}
}
{%- when None -%}
{% call macros::result_type(func, "void") %} {{ func.name()|fn_name }}({% call macros::param_list(func) %}) {
    {%- if func.is_async() %}
    return {% call macros::rust_call_async_void(func) %};
    {%- else if fallible %}
    return {% call macros::rust_call(func) %};
    {%- else %}
    {% call macros::rust_call(func) %};
    {%- endif %}
}
{%- endmatch -%}
