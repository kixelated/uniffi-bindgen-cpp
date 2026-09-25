{%- let ffi_converter_name = typ|ffi_converter_name %}
{%- let class_name = ffi_converter_name|class_name %}
{%- let canonical_type_name = typ|canonical_name %}
{%- let trait_impl = canonical_type_name|callback_interface_name %}

{%- for (ffi_callback, method) in vtable_methods.iter() %}
 {% call macros::ffi_return_type(ffi_callback) %} {{ trait_impl}}::{{ method.name()|var_name }}({% call macros::arg_list_ffi_decl_xx(ffi_callback) %}) {
    {%- if method.is_async() %}
    {%- let result_struct = method.foreign_future_ffi_result_struct() %}
    using Result = {{ result_struct.name()|ffi_struct_name }};
    using Complete = void (*)(uint64_t, Result);
    auto complete = reinterpret_cast<Complete>(uniffi_future_callback);
    auto state = std::make_shared<ForeignFutureTaskState>();

    uniffi_out_dropped_callback.handle = foreign_future_handle(state);
    uniffi_out_dropped_callback.free = reinterpret_cast<void *>(&foreign_future_drop);

    state->set_complete_cancelled([complete, uniffi_callback_data]() {
        Result result{};
        result.call_status.code = 3;
        complete(uniffi_callback_data, result);
    });

    auto complete_failure = [state, complete, uniffi_callback_data](std::exception_ptr error) {
        if (!state->finish()) {
            return;
        }

        Result result{};
        try {
            if (error) {
                std::rethrow_exception(error);
            }
            throw std::runtime_error("UniFFI foreign future failed without an exception");
        }
        {%- match method.throws_type() %}
        {%- when Some(error) %}
        catch ({{ error|canonical_name }} &e) {
            result.call_status.code = 1;
            result.call_status.error_buf = {{ error|lower_fn }}(e);
        }
        {%- when None %}
        {%- endmatch %}
        catch (std::exception &e) {
            result.call_status.code = 2;
            result.call_status.error_buf = {{ Type::String.borrow()|lower_fn }}(e.what());
        } catch (...) {
            result.call_status.code = 2;
            result.call_status.error_buf = {{ Type::String.borrow()|lower_fn }}("Unknown C++ exception");
        }
        complete(uniffi_callback_data, result);
    };

    {% match method.return_type() %}
    {% when Some(t) %}
    auto complete_success = [state, complete, uniffi_callback_data]({{ t|type_name(ci) }} value) {
        if (!state->finish()) {
            return;
        }

        Result result{};
        try {
            result.return_value = {{ t|lower_fn }}(value);
        } catch (std::exception &e) {
            result.call_status.code = 2;
            result.call_status.error_buf = {{ Type::String.borrow()|lower_fn }}(e.what());
        } catch (...) {
            result.call_status.code = 2;
            result.call_status.error_buf = {{ Type::String.borrow()|lower_fn }}("Unknown C++ exception");
        }
        complete(uniffi_callback_data, result);
    };
    {% when None %}
    auto complete_success = [state, complete, uniffi_callback_data]() {
        if (!state->finish()) {
            return;
        }
        complete(uniffi_callback_data, Result{});
    };
    {% endmatch %}

    try {
        auto obj = std::dynamic_pointer_cast<{{ foreign_interface_name }}>(
            {{ ffi_converter_name }}::handle_map.at(uniffi_handle)
        );
        if (!obj) {
            throw std::runtime_error("UniFFI callback handle has the wrong implementation type");
        }
        {%- for arg in method.arguments() %}
        auto arg{{ loop.index0 }} = {{- arg|lift_fn }}({{ arg.name()|var_name }});
        {%- endfor %}
        auto future = obj->{{ method.name()|var_name }}(
        {%- for arg in method.arguments() %}
            arg{{ loop.index0 }}{%- if !loop.last %}, {% endif %}
        {%- endfor %}
        );
        auto cancel = std::move(future).start(complete_success, complete_failure);
        state->set_cancel(std::move(cancel));
    } catch (...) {
        complete_failure(std::current_exception());
    }
    {%- else %}
    auto obj = {{ ffi_converter_name }}::handle_map.at(uniffi_handle);

    auto make_call = [&]() {% match method.return_type() %}{% when Some(t) %}-> {{ t|type_name(ci) }}{% when None %}{% endmatch %} {
        {%- for arg in method.arguments() %}
        auto arg{{ loop.index0 }} = {{- arg|lift_fn }}({{ arg.name()|var_name }});
        {%- endfor -%}

        {%- if method.return_type().is_some() %}return {% endif -%}
         obj->{{ method.name()|var_name }}(
        {%- for arg in method.arguments() %}
        arg{{ loop.index0 }}{%- if !loop.last %}, {% else %}{% endif %}
        {%- endfor -%}
        );
    };

    {% match method.return_type() %}
    {% when Some(t) %}
    auto write_value = [&]({{ t|type_name(ci) }} v) {
        uniffi_out_return = {{ t|lower_fn }}(v);
    };
    {% when None %}
    (void)uniffi_out_return;
    auto write_value = [](){};
    {% endmatch %}

    {% match method.throws_type() %}
    {% when Some(error) %}
        rust_call_trait_interface_with_error<{{ error|canonical_name }}>(out_status, make_call, write_value, {{ error|lower_fn }});
    {% when None %}
        rust_call_trait_interface(out_status, make_call, write_value);
    {% endmatch %}
    {%- endif %}
}
{%- endfor %}

void {{ trait_impl }}::uniffi_free(uint64_t uniffi_handle) {
    {{ ffi_converter_name }}::handle_map.erase(uniffi_handle);
}

uint64_t {{ trait_impl }}::uniffi_clone(uint64_t uniffi_handle) {
    return {{ ffi_converter_name }}::handle_map.insert(
        {{ ffi_converter_name }}::handle_map.at(uniffi_handle)
    );
}

void {{ trait_impl }}::init() {
    {{ ffi_init_callback.name() }}(vtable);
}
