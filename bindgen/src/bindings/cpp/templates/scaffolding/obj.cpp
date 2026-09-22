{{ type_name }} {{ ffi_converter_name }}::lift(uint64_t handle) {
    return {{ name }}_map.at(handle);
}

uint64_t {{ ffi_converter_name }}::lower(const {{ type_name }} &obj) {
    return {{ name }}_map.insert(obj);
}

{{ type_name }} {{ ffi_converter_name }}::read(RustStream &stream) {
    uint64_t handle;
    stream >> handle;

    return {{ name }}_map.at(handle);
}

void {{ ffi_converter_name }}::write(RustStream &stream, const {{ type_name }} &obj) {
    {{ name }}_map.insert(obj);
    stream << (uint64_t)obj.get();
}

int32_t {{ ffi_converter_name }}::allocation_size(const {{ type_name }} &) {
    return 8;
}
