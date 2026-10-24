{{ type_name }} {{ ffi_converter_name }}::lift(RustBuffer buf) {
    auto stream = RustStream(&buf);
    auto val = {{ ffi_converter_name }}::read(stream);

    rustbuffer_free(buf);

    return val;
}

RustBuffer {{ ffi_converter_name }}::lower(const {{ type_name }} &val) {
    auto buf = rustbuffer_alloc(allocation_size(val));
    auto stream = RustStream(&buf);

    {{ ffi_converter_name }}::write(stream, val);

    return buf;
}

{{ type_name }} {{ ffi_converter_name }}::read(RustStream &stream) {
    uint64_t secs;
    uint32_t nanos;

    stream >> secs;
    stream >> nanos;

    // Rust's Duration reaches about 584 billion years, far past what int64_t nanoseconds hold.
    constexpr auto max = {{ type_name }}::max();
    constexpr auto max_secs = std::chrono::duration_cast<std::chrono::seconds>(max);
    constexpr auto max_nanos = static_cast<uint32_t>((max - max_secs).count());
    if (secs > static_cast<uint64_t>(max_secs.count()) ||
        (secs == static_cast<uint64_t>(max_secs.count()) && nanos > max_nanos)) {
        {% call macros::fail("Duration does not fit in int64_t nanoseconds") %}
    }

    return std::chrono::seconds(static_cast<int64_t>(secs)) + std::chrono::nanoseconds(nanos);
}

void {{ ffi_converter_name }}::write(RustStream &stream, const {{ type_name }} &val) {
    // Rust's Duration is unsigned.
    if (val.count() < 0) {
        {% call macros::fail("Duration must not be negative") %}
    }
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(val);
    auto nanos = (val - secs).count();

    stream << static_cast<uint64_t>(secs.count()) << static_cast<uint32_t>(nanos);
}

uint64_t {{ ffi_converter_name }}::allocation_size(const {{ type_name }} &) {
    return static_cast<uint64_t>(sizeof(uint64_t) + sizeof(uint32_t));
}
