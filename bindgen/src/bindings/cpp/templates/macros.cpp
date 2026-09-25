{% macro rust_call(func) -%}
    uniffi::rust_call(
        {{ func.ffi_func().name() }},
{%- match func.throws_type() %}
{% when Some with (e) %}
        uniffi::{{ e|ffi_error_converter_name }}::lift
{%- else %}
        nullptr
{%- endmatch %}
{%- if !func.arguments().is_empty() %}, {% else %}{% endif %}
        {%- call arg_list_lowered(func) -%})
{%- endmacro %}

{% macro rust_call_with_prefix(prefix, func) -%}
    uniffi::rust_call(
        {{ func.ffi_func().name() }},
{%- match func.throws_type() %}
{% when Some with (e) %}
        uniffi::{{ e|ffi_error_converter_name }}::lift,
{%- else %}
        nullptr,
{%- endmatch %}
        {{ prefix }}
{%- if !func.arguments().is_empty() %}, {% else %}{% endif %}
        {%- call arg_list_lowered(func) -%})
{%- endmacro %}

{% macro rust_call_async(func, return_type) -%}
    uniffi::rust_call_async<{{ return_type }}{% call async_error_arg(func) %}>(
        [&]() {
            return {{ func.ffi_func().name() }}(
                {%- call arg_list_lowered(func) -%}
            );
        },
        {{ func.ffi_rust_future_poll(ci) }},
        {{ func.ffi_rust_future_cancel(ci) }},
        {{ func.ffi_rust_future_complete(ci) }},
        {{ func.ffi_rust_future_free(ci) }},
        [](auto value) { return uniffi::{{ func.return_type().unwrap()|lift_fn }}(value); },
{%- match func.throws_type() %}
{% when Some with (e) %}
        uniffi::{{ e|ffi_error_converter_name }}::lift
{%- else %}
        nullptr
{%- endmatch %}
    )
{%- endmacro %}

{% macro rust_call_async_void(func) -%}
    uniffi::rust_call_async<void{% call async_error_arg(func) %}>(
        [&]() {
            return {{ func.ffi_func().name() }}(
                {%- call arg_list_lowered(func) -%}
            );
        },
        {{ func.ffi_rust_future_poll(ci) }},
        {{ func.ffi_rust_future_cancel(ci) }},
        {{ func.ffi_rust_future_complete(ci) }},
        {{ func.ffi_rust_future_free(ci) }},
        nullptr,
{%- match func.throws_type() %}
{% when Some with (e) %}
        uniffi::{{ e|ffi_error_converter_name }}::lift
{%- else %}
        nullptr
{%- endmatch %}
    )
{%- endmacro %}

{% macro rust_call_async_with_prefix(prefix, func, return_type) -%}
    uniffi::rust_call_async<{{ return_type }}{% call async_error_arg(func) %}>(
        [&]() {
            return {{ func.ffi_func().name() }}(
                {{ prefix }}{% if !func.arguments().is_empty() %}, {% endif %}
                {%- call arg_list_lowered(func) -%}
            );
        },
        {{ func.ffi_rust_future_poll(ci) }},
        {{ func.ffi_rust_future_cancel(ci) }},
        {{ func.ffi_rust_future_complete(ci) }},
        {{ func.ffi_rust_future_free(ci) }},
        [](auto value) { return uniffi::{{ func.return_type().unwrap()|lift_fn }}(value); },
{%- match func.throws_type() %}
{% when Some with (e) %}
        uniffi::{{ e|ffi_error_converter_name }}::lift
{%- else %}
        nullptr
{%- endmatch %}
    )
{%- endmacro %}

{% macro rust_call_async_void_with_prefix(prefix, func) -%}
    uniffi::rust_call_async<void{% call async_error_arg(func) %}>(
        [&]() {
            return {{ func.ffi_func().name() }}(
                {{ prefix }}{% if !func.arguments().is_empty() %}, {% endif %}
                {%- call arg_list_lowered(func) -%}
            );
        },
        {{ func.ffi_rust_future_poll(ci) }},
        {{ func.ffi_rust_future_cancel(ci) }},
        {{ func.ffi_rust_future_complete(ci) }},
        {{ func.ffi_rust_future_free(ci) }},
        nullptr,
{%- match func.throws_type() %}
{% when Some with (e) %}
        uniffi::{{ e|ffi_error_converter_name }}::lift
{%- else %}
        nullptr
{%- endmatch %}
    )
{%- endmacro %}

{% macro param_list(func) %}
{%- for arg in func.arguments() -%}
{{ arg|parameter(ci) }}
{%- if !loop.last -%}, {% endif -%}
{% endfor -%}
{% endmacro %}

{% macro field_name(field, field_num) %}
{%- if field.name().is_empty() -%}
v{{- field_num -}}
{%- else -%}
{{ field.name()|var_name }}
{%- endif -%}
{%- endmacro %}

{% macro arg_list_lowered(func) %}
{%- for arg in func.arguments() -%}
uniffi::{{ arg|lower_fn }}({{ arg.name()|var_name }})
{%- if !loop.last -%}, {% endif -%}
{% endfor -%}
{% endmacro %}

{%- macro arg_list_ffi_decl_xx(func) %}
    {%- for arg in func.arguments() %}
        {{- arg.type_().borrow()|ffi_type_name }} {{ arg.name()|var_name -}}{%- if !loop.last || func.has_rust_call_status_arg() -%},{%- endif -%}
    {%- endfor %}
    {%- if func.has_rust_call_status_arg() %}RustCallStatus *out_status{% endif %}
{%- endmacro -%}

{%- macro ffi_return_type(func) %}
    {%- match func.return_type() %}
    {%- when Some(return_type) %}{{ return_type|ffi_type_name }}
    {%- when None %}{{ "void" }}
    {%- endmatch %}
{%- endmacro %}

{%- macro docstring_value(maybe_docstring, indent_spaces) %}
{%- match maybe_docstring %}
{%- when Some(docstring) %}
{{ docstring|docstring(indent_spaces) }}
{%- else %}
{%- endmatch %}
{%- endmacro %}

{%- macro docstring(defn, indent_spaces) %}
{%- call docstring_value(defn.docstring(), indent_spaces) %}
{%- endmacro %}

{#- Reports a bug: a throw under the exceptions style, an abort under the expected style. #}
{%- macro fail(message) -%}
{%- if config.expected() -%}
::uniffi::detail::fatal("{{ message }}");
{%- else -%}
throw std::runtime_error("{{ message }}");
{%- endif -%}
{%- endmacro %}

{#- The error type argument of an expected-style future, present when the call can fail. #}
{%- macro async_error_arg(func) -%}
{%- if config.expected() -%}
{%- match func.throws_type() %}{% when Some with (e) %}, {{ e|type_name(ci) }}{% else %}{% endmatch -%}
{%- endif -%}
{%- endmacro %}

{#- What a callable returns, given the type of its value: a future when async, and
    `expected<T, E>` when it can fail under the expected style. #}
{%- macro result_type(func, value_type) -%}
{%- if func.is_async() -%}
::uniffi::Future<{{ value_type }}{% call async_error_arg(func) %}>
{%- else if config.expected() && func.throws_type().is_some() -%}
::uniffi::expected<{{ value_type }}, {{ func.throws_type().unwrap()|type_name(ci) }}>
{%- else -%}
{{ value_type }}
{%- endif -%}
{%- endmacro %}
