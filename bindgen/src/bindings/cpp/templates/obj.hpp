{%- let obj = ci.get_object_definition(name).unwrap() %}
{%- let (interface_name, impl_class_name) = obj|object_names %}
{%- let class_name = type_name|class_name %}
{%- let ffi_converter_name = typ|ffi_converter_name %}
{%- let canonical_type_name = typ|canonical_name %}
{%- if obj.has_callback_interface() %}
{%- if obj.has_async_method() %}
struct {{ canonical_type_name }} {
    virtual ~{{ canonical_type_name }}() = default;
};
{%- endif %}
{%- let vtable = obj.vtable().expect("trait interface should have a vtable") %}
{%- let vtable_methods = obj.vtable_methods() %}
{%- let methods = obj.methods() %}
{%- let ffi_init_callback = obj.ffi_init_callback() %}
{%- let interface_docstring = obj.docstring() %}
{%- if obj.has_async_method() %}
{%- let interface_base_name = canonical_type_name %}
{% include "callback.hpp" %}
{%- else %}
{%- let interface_base_name = "" %}
{% include "callback.hpp" %}
{%- endif %}
{%- endif %}

namespace uniffi {
    struct {{ ffi_converter_name|class_name }};
} // namespace uniffi

{%~ call macros::docstring(obj, 0) %}
struct {{ impl_class_name }}
{#
    Since an interface being a callback interface or an error is mutually exclusive,
    we don't need to complex branching for multiple inheritance
#}
{% if obj.has_callback_interface() %} : public {% if obj.has_async_method() %}{{ canonical_type_name }}{% else %}{{ interface_name }}{% endif %} {% endif %}
{% if ci.is_name_used_as_error(name) %} : public std::exception {% endif %}
{
    friend uniffi::{{ ffi_converter_name|class_name }};

    {{ impl_class_name }}() = delete;

    {{ impl_class_name }}({{ impl_class_name }} &&) = delete;

    {{ impl_class_name }} &operator=(const {{ impl_class_name }} &) = delete;
    {{ impl_class_name }} &operator=({{ impl_class_name }} &&) = delete;

    ~{{ impl_class_name }}();

    {%- match obj.primary_constructor() %}
    {%- when Some with (ctor) %}
    {%- call macros::docstring(ctor, 4) %}
    static {% call macros::result_type(ctor, type_name) %} init({% call macros::param_list(ctor) %});
    {%- else %}
    {%- endmatch %}

    {%- for ctor in obj.alternate_constructors() %}
    {%- call macros::docstring(ctor, 4) %}
    static {% call macros::result_type(ctor, type_name) %} {{ ctor.name() }}({% call macros::param_list(ctor) %});
    {%- endfor %}

    {%- for method in obj.methods() %}
    {%- call macros::docstring(method, 4) %}
    {% match method.return_type() %}{% when Some with (return_type) %}{% call macros::result_type(method, return_type|type_name(ci)) %}{% else %}{% call macros::result_type(method, "void") %}{% endmatch %} {{ method.name()|fn_name }}({% call macros::param_list(method) %});
    {%- endfor %}

    {%- for method in obj.uniffi_traits() %}
    {%- match method %}
    {%- when UniffiTrait::Display { fmt } %}
    /**
     * Returns a string representation of the object, internally calls Rust's `Display` trait.
     */
    std::string to_string() const;
    {%- when UniffiTrait::Debug { fmt } %}
    /**
     * Returns a string representation of the object, internally calls Rust's `Debug` trait.
     */
    std::string to_debug_string() const;
    {%- when UniffiTrait::Eq { eq, ne } %}
    /**
     * Equality check, internally calls Rust's `Eq` trait.
     */
    bool eq(const {{ type_name }} &other) const;
    /**
     * Inequality check, internally calls Rust's `Ne` trait.
     */
    bool ne(const {{ type_name }} &other) const;
    {%- when UniffiTrait::Hash { hash } %}
    /**
     * Returns a hash of the object, internally calls Rust's `Hash` trait.
     */
    uint64_t hash() const;
    {%- when UniffiTrait::Ord { cmp } %}
    /**
     * Three-way comparison, internally calls Rust's `Ord` trait.
     */
    int8_t cmp(const {{ type_name }} &other) const;
    {%- endmatch %}
    {%- endfor %}

    {% if ci.is_name_used_as_error(name) && !config.expected() %}
    void throw_underlying();
    {%- endif -%}
private:
    {{ impl_class_name }}(const {{ impl_class_name }} &);

    {{ impl_class_name }}(uint64_t);

    uint64_t _uniffi_internal_clone_pointer() const;

    uint64_t instance = 0;
};
