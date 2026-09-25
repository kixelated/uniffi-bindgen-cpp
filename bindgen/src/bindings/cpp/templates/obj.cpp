{%- let obj = ci.get_object_definition(name).unwrap() %}
{%- let (interface_name, impl_class_name) = obj|object_names %}
{%- let foreign_interface_name = interface_name %}
{%- let class_name = type_name|class_name %}
{%- let ffi_converter_name = typ|ffi_converter_name %}
{%- let canonical_type_name = typ|canonical_name %}

{%- if obj.has_callback_interface() %}
{%- let vtable = obj.vtable().expect("trait interface should have a vtable") %}
{%- let vtable_methods = obj.vtable_methods() %}
{%- let ffi_init_callback = obj.ffi_init_callback() %}
namespace uniffi {
{% include "callback_iface_tmpl.cpp" %}
} // namespace uniffi
{%- endif %}


{{ impl_class_name }}::{{ impl_class_name }}(uint64_t handle): instance(handle) {}

{{ impl_class_name }}::{{ impl_class_name }}(const {{ impl_class_name }} &other) : instance(0) {
    if (other.instance) {
        instance = other._uniffi_internal_clone_pointer();
    }
}

{% if ci.is_name_used_as_error(name) && !config.expected() %}
    void {{ impl_class_name }}::throw_underlying() {
        throw *this;
    }
{% endif %}

{% match obj.primary_constructor() -%}
{%- when Some with (ctor) %}
{% call macros::result_type(ctor, type_name) %} {{ impl_class_name }}::init({% call macros::param_list(ctor) %}) {
    {%- if ctor.is_async() %}
    return {% call macros::rust_call_async(ctor, type_name) %};
    {%- else if config.expected() && ctor.throws_type().is_some() %}
    return uniffi::lift_expected<{{ type_name }}>({% call macros::rust_call(ctor) %}, [](uint64_t handle) {
        return {{ type_name }}(new {{ impl_class_name }}(handle));
    });
    {%- else %}
    return {{ type_name }}(
        new {{ impl_class_name }}({%- call macros::rust_call(ctor) -%})
    );
    {%- endif %}
}
{% else -%}
{% endmatch -%}

{% for ctor in obj.alternate_constructors() %}
{% call macros::result_type(ctor, type_name) %} {{ impl_class_name }}::{{ ctor.name() }}({% call macros::param_list(ctor) %}) {
    {%- if ctor.is_async() %}
    return {% call macros::rust_call_async(ctor, type_name) %};
    {%- else if config.expected() && ctor.throws_type().is_some() %}
    return uniffi::lift_expected<{{ type_name }}>({% call macros::rust_call(ctor) %}, [](uint64_t handle) {
        return {{ type_name }}(new {{ impl_class_name }}(handle));
    });
    {%- else %}
    return {{ type_name }}(new {{ impl_class_name }}({% call macros::rust_call(ctor) %}));
    {%- endif %}
}
{% endfor %}

{%- for method in obj.methods() %}
{% match method.return_type() %}{% when Some with (return_type) %}{% call macros::result_type(method, return_type|type_name(ci)) %}{% else %}{% call macros::result_type(method, "void") %}{% endmatch %}
{{ impl_class_name }}::{{ method.name()|fn_name }}({% call macros::param_list(method) %}) {
    auto ptr = this->_uniffi_internal_clone_pointer();
    {%- match method.return_type() %}
    {% when Some with (return_type) %}
    {%- if method.is_async() %}
    return {% call macros::rust_call_async_with_prefix("ptr", method, return_type|type_name(ci)) %};
    {%- else if config.expected() && method.throws_type().is_some() %}
    return uniffi::lift_expected<{{ return_type|type_name(ci) }}>({% call macros::rust_call_with_prefix("ptr", method) %}, [](auto ret) {
        return uniffi::{{ return_type|lift_fn }}(ret);
    });
    {%- else %}
    return uniffi::{{ return_type|lift_fn }}({% call macros::rust_call_with_prefix("ptr", method) %});
    {%- endif %}
    {%- else %}
    {%- if method.is_async() %}
    return {% call macros::rust_call_async_void_with_prefix("ptr", method) %};
    {%- else if config.expected() && method.throws_type().is_some() %}
    return {% call macros::rust_call_with_prefix("ptr", method) %};
    {%- else %}
    {% call macros::rust_call_with_prefix("ptr", method) -%};
    {%- endif %}
    {%- endmatch %}
}
{%- endfor %}

{{ impl_class_name }}::~{{ impl_class_name }}() {
    uniffi::rust_call(
        {{ obj.ffi_object_free().name() }},
        nullptr,
        this->instance
    );
}

uint64_t {{ impl_class_name }}::_uniffi_internal_clone_pointer() const {
    return uniffi::rust_call(
        {{ obj.ffi_object_clone().name() }},
        nullptr,
        this->instance
    );
}

{%- for method in obj.uniffi_traits() %}
{% match method %}
{% when UniffiTrait::Display { fmt } %}
std::string {{ impl_class_name }}::to_string() const {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("this->_uniffi_internal_clone_pointer()", fmt) %});
}
{% when UniffiTrait::Debug { fmt } %}
std::string {{ impl_class_name }}::to_debug_string() const {
    return uniffi::{{ Type::String.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("this->_uniffi_internal_clone_pointer()", fmt) %});
}
{% when UniffiTrait::Eq { eq, ne } %}
bool {{ impl_class_name }}::eq(const {{ type_name }} &other) const {
    return uniffi::{{ Type::Boolean.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("this->_uniffi_internal_clone_pointer()", eq) %});
}
bool {{ impl_class_name }}::ne(const {{ type_name }} &other) const {
    return uniffi::{{ Type::Boolean.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("this->_uniffi_internal_clone_pointer()", ne) %});
}
{% when UniffiTrait::Hash { hash } %}
uint64_t {{ impl_class_name }}::hash() const {
    return uniffi::{{ Type::UInt64.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("this->_uniffi_internal_clone_pointer()", hash) %});
}
{% when UniffiTrait::Ord { cmp } %}
int8_t {{ impl_class_name }}::cmp(const {{ type_name }} &other) const {
    return uniffi::{{ Type::Int8.borrow()|lift_fn }}({% call macros::rust_call_with_prefix("this->_uniffi_internal_clone_pointer()", cmp) %});
}
{% endmatch %}
{%- endfor %}
