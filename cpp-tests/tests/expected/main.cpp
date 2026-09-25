// Bindings generated with `error_style = "expected"`, built with exceptions and RTTI disabled.

#include <error_types_builtin.hpp>
#include <futures.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <variant>

using namespace std::chrono_literals;

// Unlike assert, this survives a release build.
#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            std::abort();                                                        \
        }                                                                        \
    } while (0)

namespace errors = error_types_builtin;

template <typename V, typename E>
const V *variant_of(const E &error) {
    return std::get_if<V>(&error.get_variant());
}

void sync_errors() {
    auto oops = errors::oops_enum(0);
    CHECK(!oops);
    CHECK(variant_of<errors::Error::kOops>(oops.error()));

    auto value = errors::oops_enum(1);
    CHECK(!value);
    auto value_variant = variant_of<errors::Error::kValue>(value.error());
    CHECK(value_variant && value_variant->value == "value");

    auto int_value = errors::oops_enum(2);
    auto int_variant = variant_of<errors::Error::kIntValue>(int_value.error());
    CHECK(int_variant && int_variant->value == 2);

    // A flat error carries only its message, one per variant.
    auto flat_a = errors::oops_enum(3);
    auto flat_a_variant = variant_of<errors::Error::kFlatInnerError>(flat_a.error());
    CHECK(flat_a_variant);
    auto case_a = variant_of<errors::FlatInner::kCaseA>(flat_a_variant->error);
    CHECK(case_a && case_a->message == "inner");

    auto flat_b = errors::oops_enum(4);
    auto flat_b_variant = variant_of<errors::Error::kFlatInnerError>(flat_b.error());
    CHECK(flat_b_variant);
    auto case_b = variant_of<errors::FlatInner::kCaseB>(flat_b_variant->error);
    CHECK(case_b && case_b->message == "NonUniffiTypeValue: value");

    auto tuple = errors::oops_tuple(1);
    auto tuple_variant = variant_of<errors::TupleError::kValue>(tuple.error());
    CHECK(tuple_variant && tuple_variant->v1 == 1);

    // Errors are plain values, so they cross the boundary as arguments too.
    auto echoed = errors::get_tuple(errors::TupleError(errors::TupleError::kValue{7}));
    auto echoed_variant = variant_of<errors::TupleError::kValue>(echoed);
    CHECK(echoed_variant && echoed_variant->v1 == 7);

    // An object used as an error arrives as the object.
    auto object = errors::oops();
    CHECK(!object);
    CHECK(object.error()->chain().size() == 2);

    auto fallible = errors::TestInterface::fallible_new();
    CHECK(!fallible);
    auto infallible = errors::TestInterface::init();
    CHECK(infallible != nullptr);
    CHECK(!infallible->oops());
}

void async_results() {
    CHECK(futures::greet("C++") == "Hello, C++");
    CHECK(futures::always_ready().get());
    futures::void_return().get();
    futures::sleep_no_return(1).get();
    CHECK(futures::say_after(1, "Future").get() == "Hello, Future!");

    auto ok = futures::fallible_me(false).get();
    CHECK(ok && *ok == 42);

    auto failed = futures::fallible_me(true).get();
    CHECK(!failed);
    CHECK(variant_of<futures::MyError::kFoo>(failed.error()));

    auto megaphone = futures::new_megaphone();
    CHECK(megaphone->say_after(1, "World").get() == "HELLO, WORLD!");
    CHECK(!megaphone->fallible_me(true).get());
    CHECK(*megaphone->fallible_me(false).get() == 42);

    auto made = futures::fallible_struct(false).get();
    CHECK(made && *made != nullptr);
    CHECK(!futures::fallible_struct(true).get());
    CHECK(!futures::FallibleMegaphone::init().get());
    CHECK(futures::Megaphone::init().get() != nullptr);

    // The fixture exposes no "acquired" signal, so give the holder a wide margin to take the
    // resource first; valgrind slows it by an order of magnitude.
    auto holding = futures::use_shared_resource({1000, 100});
    std::this_thread::sleep_for(200ms);
    auto timeout = futures::use_shared_resource({0, 5}).get();
    CHECK(!timeout);
    CHECK(variant_of<futures::AsyncError::kTimeout>(timeout.error()));
    CHECK(holding.get());
}

void continuations() {
    auto inline_executor = [](uniffi::AsyncTask task) {
        task();
        return true;
    };

    std::atomic<int> value{0};
    auto continuation = std::move(futures::fallible_me(false))
        .then(inline_executor, [&](uniffi::expected<uint8_t, futures::MyError> result) {
            value = result ? *result : -1;
        });
    for (int i = 0; i < 500 && value == 0; i++) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(value == 42);

    std::atomic<bool> finished{false};
    auto void_continuation = std::move(futures::sleep_no_return(1))
        .then(inline_executor, [&]() { finished = true; });
    for (int i = 0; i < 500 && !finished; i++) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(finished);
}

void cancellation() {
    auto pending = futures::sleep(10000);
    CHECK(pending.wait_for(10ms) == std::future_status::timeout);
    pending.cancel();
    CHECK(!pending.valid());

    // A cancelled continuation never runs.
    std::atomic<bool> ran{false};
    auto continuation = std::move(futures::sleep(20)).then(
        [](uniffi::AsyncTask task) {
            task();
            return true;
        },
        [&](bool) { ran = true; }
    );
    continuation.cancel();
    std::this_thread::sleep_for(100ms);
    CHECK(!ran);

    // Dropping an incomplete future cancels it rather than blocking.
    auto start = std::chrono::steady_clock::now();
    {
        auto dropped = futures::say_after(10000, "never");
    }
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

void shutdown() {
    // A future still pending when the dispatcher shuts down is abandoned: its
    // continuation does not run and nothing calls back into unloaded code.
    std::atomic<bool> ran{false};
    auto continuation = std::move(futures::sleep(50)).then(
        [](uniffi::AsyncTask task) {
            task();
            return true;
        },
        [&](bool) { ran = true; }
    );
    uniffi::shutdown_async_dispatcher();
    std::this_thread::sleep_for(200ms);
    CHECK(!ran);
}

int main() {
    sync_errors();
    async_results();
    continuations();
    cancellation();
    shutdown();
    return 0;
}
