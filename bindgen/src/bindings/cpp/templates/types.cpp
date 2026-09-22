{%- import "macros.cpp" as macros %}

{%- for typ in ci.iter_local_types() %}
{%- let type_name = typ|type_name(ci) %}
{%- let ffi_converter_name = typ|ffi_converter_name %}
{%- let canonical_type_name = typ|canonical_name %}
{%- let contains_object_references = ci.item_contains_object_references(typ) %}
{%- let namespace = ci.namespace() %}

{%- match typ %}
{%- when Type::Object { module_path, name, imp } %}
{% include "obj.cpp" %}
{%- when Type::Record { module_path, name } %}
{%- let rec = ci.get_record_definition(name).unwrap() %}
{%- let methods = rec.methods() %}
{%- let uniffi_trait_methods = rec.uniffi_trait_methods() %}
{% include "value_methods.cpp" %}
{%- when Type::Enum { name, module_path } %}
{%- let e = ci.get_enum_definition(name).unwrap() %}
{%- let methods = e.methods() %}
{%- let uniffi_trait_methods = e.uniffi_trait_methods() %}
{%- if ci.is_name_used_as_error(name) %}
{%- let type_name = typ|canonical_name %}
{% include "value_methods.cpp" %}
{%- else if e.is_flat() %}
{% include "flat_enum_methods.cpp" %}
{%- else %}
{% include "value_methods.cpp" %}
{%- endif %}
{%- else %}
{%- endmatch %}
{% endfor ~%}

namespace uniffi {
{%- for typ in ci.iter_local_types() %}
{%- let type_name = typ|type_name(ci) %}
{%- let ffi_converter_name = typ|ffi_converter_name %}
{%- let canonical_type_name = typ|canonical_name %}
{%- let contains_object_references = ci.item_contains_object_references(typ) %}
{%- let namespace = ci.namespace() %}

{%- match typ %}
{%- when Type::Enum { name, module_path } %}
{%- let e = ci.get_enum_definition(name).unwrap() %}
{%- if ci.is_name_used_as_error(name) %}
{% include "err_tmpl.cpp" %}
{%- else %}
{% include "enum_tmpl.cpp" %}
{%- endif %}
{%- when Type::Object { module_path, name, imp } %}
{% include "obj_conv.cpp" %}
{%- when Type::Record { module_path, name } %}
{% include "rec.cpp" %}
{%- when Type::Optional { inner_type } %}
{% include "opt_tmpl.cpp" %}
{%- when Type::Sequence { inner_type } %}
{% include "seq_tmpl.cpp" %}
{%- when Type::Map { key_type, value_type } %}
{% include "map_tmpl.cpp" %}
{%- when Type::CallbackInterface { module_path, name } %}
{%- let cbi = ci.get_callback_interface_definition(name).unwrap() %}
{%- let ffi_init_callback = cbi.ffi_init_callback() %}
{%- let interface_name = name %}
{%- let methods = cbi.methods() %}
{%- let vtable = cbi.vtable() %}
{%- let vtable_methods = cbi.vtable_methods() %}
{% include "callback_conv.cpp" %}
{% include "callback_iface_tmpl.cpp" %}
{%- when Type::Timestamp %}
{% include "timestamp_helper.cpp" %}
{%- when Type::Duration %}
{% include "duration_helper.cpp" %}
{%- when Type::Custom { module_path, name, builtin } %}
{%- include "custom.cpp" %}
{%- else %}
{%- endmatch %}
{% endfor ~%}

}
