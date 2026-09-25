{%- if config.expected() %}
namespace detail {

// Reports a bug the expected style has no error value for, such as a Rust panic, and aborts.
[[noreturn]] inline void fatal(const char *message) noexcept {
    std::fprintf(stderr, "uniffi: %s\n", message);
    std::fflush(stderr);
    std::abort();
}

} // namespace detail
{%- else %}
class AsyncCancelledError: public std::runtime_error {
public:
    AsyncCancelledError(): std::runtime_error("UniFFI async call cancelled") {}
};

class AsyncDispatcherError: public std::runtime_error {
public:
    AsyncDispatcherError(): std::runtime_error("UniFFI async dispatcher rejected a continuation") {}
};
{%- endif %}

using AsyncTask = std::function<void()>;
using AsyncDispatcher = std::function<bool(AsyncTask)>;
using AsyncDispatcherShutdown = std::function<void()>;

namespace detail {

class DefaultAsyncDispatcher {
public:
    DefaultAsyncDispatcher(): worker_([this] { run(); }) {}

    DefaultAsyncDispatcher(const DefaultAsyncDispatcher &) = delete;
    DefaultAsyncDispatcher &operator=(const DefaultAsyncDispatcher &) = delete;

    ~DefaultAsyncDispatcher() {
        shutdown();
    }

    bool dispatch(AsyncTask task) {
        std::lock_guard<std::mutex> guard(mutex_);
        {%- if config.expected() %}
        // Unbounded: a rejected poll drops its future, which only shutdown may do.
        if (!accepting_) {
        {%- else %}
        if (!accepting_ || tasks_.size() >= MAX_QUEUED_TASKS) {
        {%- endif %}
            return false;
        }
        tasks_.push_back(std::move(task));
        ready_.notify_one();
        return true;
    }

    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            accepting_ = false;
        }
        ready_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void run() noexcept {
        for (;;) {
            AsyncTask task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return !tasks_.empty() || !accepting_; });
                if (tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            {%- if config.expected() %}
            task();
            {%- else %}
            try {
                task();
            } catch (...) {
                // Generated continuation tasks are noexcept. Protect the worker from
                // consumer-provided tasks that violate that contract.
            }
            {%- endif %}
        }
    }
{% if !config.expected() %}
    static constexpr std::size_t MAX_QUEUED_TASKS = 1024;
{%- endif %}
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<AsyncTask> tasks_;
    bool accepting_ = true;
    std::thread worker_;
};

inline DefaultAsyncDispatcher &default_async_dispatcher() {
    static DefaultAsyncDispatcher dispatcher;
    return dispatcher;
}

struct AsyncDispatcherState {
    AsyncDispatcherState():
        dispatch([](AsyncTask task) {
            return default_async_dispatcher().dispatch(std::move(task));
        }),
        shutdown([] { default_async_dispatcher().shutdown(); }) {}

    std::mutex mutex;
    bool accepting = true;
    bool started = false;
    AsyncDispatcher dispatch;
    AsyncDispatcherShutdown shutdown;
};

inline AsyncDispatcherState &async_dispatcher_state() {
    static AsyncDispatcherState state;
    return state;
}

inline bool dispatch_async(AsyncTask task) noexcept {
    auto &state = async_dispatcher_state();
    AsyncDispatcher dispatch;
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.accepting) {
            return false;
        }
        state.started = true;
        dispatch = state.dispatch;
    }
    {%- if config.expected() %}
    return dispatch(std::move(task));
    {%- else %}
    try {
        return dispatch(std::move(task));
    } catch (...) {
        return false;
    }
    {%- endif %}
}

} // namespace detail

{%- if config.expected() %}
// Installs the process-wide continuation dispatcher. The dispatcher returns true only
// when it has accepted the task; rejecting a task drops the future it would have polled.
// shutdown must stop accepting work, drain accepted tasks, and return only when they can
// no longer call generated code. Aborts when either function is empty, or when called
// after shutdown or after the first async call.
inline void set_async_dispatcher(
    AsyncDispatcher dispatcher,
    AsyncDispatcherShutdown shutdown
) noexcept {
    if (!dispatcher || !shutdown) {
        detail::fatal("UniFFI async dispatcher and shutdown function must not be empty");
    }
    auto &state = detail::async_dispatcher_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.accepting) {
        detail::fatal("UniFFI async dispatcher has already shut down");
    }
    if (state.started) {
        detail::fatal("UniFFI async dispatcher must be installed before the first async call");
    }
{%- else %}
// Installs the process-wide continuation dispatcher. The dispatcher returns true only
// when it has accepted the task. shutdown must stop accepting work, drain accepted
// tasks, and return only when they can no longer call generated code.
inline void set_async_dispatcher(
    AsyncDispatcher dispatcher,
    AsyncDispatcherShutdown shutdown
) {
    if (!dispatcher || !shutdown) {
        throw std::invalid_argument(
            "UniFFI async dispatcher and shutdown function must not be empty"
        );
    }
    auto &state = detail::async_dispatcher_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.accepting) {
        throw std::logic_error("UniFFI async dispatcher has already shut down");
    }
    if (state.started) {
        throw std::logic_error(
            "UniFFI async dispatcher must be installed before the first async call"
        );
    }
{%- endif %}
    state.dispatch = std::move(dispatcher);
    state.shutdown = std::move(shutdown);
}

// Permanently stops continuation dispatch for this process. Call this before unloading
// code used by a custom dispatcher or generated bindings.
inline void shutdown_async_dispatcher() noexcept {
    AsyncDispatcher dispatch;
    AsyncDispatcherShutdown shutdown;
    {
        auto &state = detail::async_dispatcher_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.accepting) {
            return;
        }
        state.accepting = false;
        dispatch = std::move(state.dispatch);
        shutdown = std::move(state.shutdown);
        state.dispatch = {};
        state.shutdown = {};
    }
    if (shutdown) {
        {%- if config.expected() %}
        shutdown();
        {%- else %}
        try {
            shutdown();
        } catch (...) {
        }
        {%- endif %}
    }
    shutdown = {};
    dispatch = {};
}

{%- if config.expected() %}
{% include "async_expected.hpp" %}
{%- else %}
// A C++17-compatible asynchronous operation returned by foreign implementations of
// UniFFI async callback interfaces. Implementations arrange their own scheduling. The
// generated bridge accepts the first success or failure callback and ignores later
// completions. The returned function requests cancellation of an incomplete operation.
template <typename T>
class ForeignFuture {
public:
    using Cancel = std::function<void()>;
    using Success = std::function<void(T)>;
    using Failure = std::function<void(std::exception_ptr)>;
    using Start = std::function<Cancel(Success, Failure)>;

    explicit ForeignFuture(Start start): start_(std::move(start)) {}

    ForeignFuture(const ForeignFuture &) = delete;
    ForeignFuture &operator=(const ForeignFuture &) = delete;
    ForeignFuture(ForeignFuture &&) noexcept = default;
    ForeignFuture &operator=(ForeignFuture &&) noexcept = default;

    Cancel start(Success success, Failure failure) && {
        if (!start_) {
            throw std::logic_error("UniFFI foreign future has already been started");
        }
        auto start = std::move(start_);
        return start(std::move(success), std::move(failure));
    }

private:
    Start start_;
};

template <>
class ForeignFuture<void> {
public:
    using Cancel = std::function<void()>;
    using Success = std::function<void()>;
    using Failure = std::function<void(std::exception_ptr)>;
    using Start = std::function<Cancel(Success, Failure)>;

    explicit ForeignFuture(Start start): start_(std::move(start)) {}

    ForeignFuture(const ForeignFuture &) = delete;
    ForeignFuture &operator=(const ForeignFuture &) = delete;
    ForeignFuture(ForeignFuture &&) noexcept = default;
    ForeignFuture &operator=(ForeignFuture &&) noexcept = default;

    Cancel start(Success success, Failure failure) && {
        if (!start_) {
            throw std::logic_error("UniFFI foreign future has already been started");
        }
        auto start = std::move(start_);
        return start(std::move(success), std::move(failure));
    }

private:
    Start start_;
};

template <typename T>
class FutureResult {
public:
    explicit FutureResult(T value): value_(std::move(value)) {}
    explicit FutureResult(std::exception_ptr error): error_(std::move(error)) {}

    bool has_value() const noexcept { return !error_; }
    std::exception_ptr error() const noexcept { return error_; }

    T get() && {
        if (error_) {
            std::rethrow_exception(error_);
        }
        return std::move(*value_);
    }

private:
    std::optional<T> value_;
    std::exception_ptr error_;
};

template <>
class FutureResult<void> {
public:
    FutureResult() = default;
    explicit FutureResult(std::exception_ptr error): error_(std::move(error)) {}

    bool has_value() const noexcept { return !error_; }
    std::exception_ptr error() const noexcept { return error_; }

    void get() const {
        if (error_) {
            std::rethrow_exception(error_);
        }
    }

private:
    std::exception_ptr error_;
};

namespace detail {

template <typename T>
class FutureCompletionState {
public:
    using Result = FutureResult<T>;
    using Callback = std::function<void(Result)>;

    void complete(Result result) noexcept {
        Callback callback;
        AsyncDispatcher executor;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (result_) {
                return;
            }
            result_.emplace(std::move(result));
            callback = std::move(callback_);
            executor = std::move(executor_);
        }
        ready_.notify_all();
        if (callback) {
            dispatch_continuation(std::move(executor), std::move(callback));
        }
    }

    T get() {
        std::unique_lock<std::mutex> lock(mutex_);
        claim_locked();
        ready_.wait(lock, [this] { return result_.has_value(); });
        auto result = std::move(*result_);
        lock.unlock();
        return std::move(result).get();
    }

    void wait() const {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return result_.has_value(); });
    }

    template <typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period> &timeout) const {
        std::unique_lock<std::mutex> lock(mutex_);
        return ready_.wait_for(lock, timeout, [this] { return result_.has_value(); })
            ? std::future_status::ready
            : std::future_status::timeout;
    }

    void then(AsyncDispatcher executor, Callback callback) {
        bool dispatch_now = false;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            claim_locked();
            executor_ = std::move(executor);
            callback_ = std::move(callback);
            dispatch_now = result_.has_value();
        }
        if (dispatch_now) {
            Callback ready_callback;
            AsyncDispatcher ready_executor;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                ready_callback = std::move(callback_);
                ready_executor = std::move(executor_);
            }
            dispatch_continuation(std::move(ready_executor), std::move(ready_callback));
        }
    }

private:
    void claim_locked() {
        if (claimed_) {
            throw std::logic_error("UniFFI future result has already been consumed");
        }
        claimed_ = true;
    }

    void dispatch_continuation(AsyncDispatcher executor, Callback callback) noexcept {
        Result result = [&] {
            std::lock_guard<std::mutex> guard(mutex_);
            return std::move(*result_);
        }();
        auto invoked = std::make_shared<std::atomic<bool>>(false);
        auto shared_callback = std::make_shared<Callback>(std::move(callback));
        auto shared_result = std::make_shared<std::optional<Result>>(std::move(result));
        auto task = [invoked, shared_callback, shared_result]() mutable {
            if (invoked->exchange(true)) {
                return;
            }
            try {
                (*shared_callback)(std::move(**shared_result));
            } catch (...) {
            }
            shared_result->reset();
        };
        auto reject = [invoked, shared_callback]() {
            if (invoked->exchange(true)) {
                return;
            }
            try {
                (*shared_callback)(Result(std::make_exception_ptr(AsyncDispatcherError())));
            } catch (...) {
            }
        };
        try {
            if (!executor(task)) {
                reject();
            }
        } catch (...) {
            reject();
        }
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable ready_;
    std::optional<Result> result_;
    bool claimed_ = false;
    AsyncDispatcher executor_;
    Callback callback_;
};

template <>
inline void FutureCompletionState<void>::get() {
    std::unique_lock<std::mutex> lock(mutex_);
    claim_locked();
    ready_.wait(lock, [this] { return result_.has_value(); });
    auto result = std::move(*result_);
    lock.unlock();
    result.get();
}

} // namespace detail

class FutureContinuation {
public:
    explicit FutureContinuation(std::function<void()> cancel): cancel_(std::move(cancel)) {}
    FutureContinuation(const FutureContinuation &) = delete;
    FutureContinuation &operator=(const FutureContinuation &) = delete;
    FutureContinuation(FutureContinuation &&other) noexcept:
        cancel_(std::move(other.cancel_)) {
        other.cancel_ = nullptr;
    }
    FutureContinuation &operator=(FutureContinuation &&other) noexcept {
        if (this != &other) {
            cancel();
            cancel_ = std::move(other.cancel_);
            other.cancel_ = nullptr;
        }
        return *this;
    }

    ~FutureContinuation() { cancel(); }

    void cancel() noexcept {
        if (cancel_) {
            auto cancel = std::move(cancel_);
            cancel();
        }
    }

private:
    std::function<void()> cancel_;
};

template <typename T>
class Future {
public:
    using Result = FutureResult<T>;
    using Callback = std::function<void(Result)>;

    Future(std::shared_ptr<detail::FutureCompletionState<T>> state, std::function<void()> cancel):
        state_(std::move(state)), cancel_(std::move(cancel)) {}

    Future(const Future &) = delete;
    Future &operator=(const Future &) = delete;

    Future(Future &&other) noexcept:
        state_(std::move(other.state_)), cancel_(std::move(other.cancel_)) {
        other.cancel_ = nullptr;
    }

    Future &operator=(Future &&other) noexcept {
        if (this != &other) {
            cancel();
            state_ = std::move(other.state_);
            cancel_ = std::move(other.cancel_);
            other.cancel_ = nullptr;
        }
        return *this;
    }

    ~Future() {
        cancel();
    }

    bool valid() const noexcept {
        return state_ != nullptr;
    }

    decltype(auto) get() {
        require_state();
        auto state = std::move(state_);
        cancel_ = nullptr;
        return state->get();
    }

    void wait() const {
        require_state();
        state_->wait();
    }

    template <typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period> &timeout) const {
        require_state();
        return state_->wait_for(timeout);
    }

    FutureContinuation then(AsyncDispatcher executor, Callback callback) && {
        require_state();
        if (!executor || !callback) {
            throw std::invalid_argument("UniFFI future continuation must not be empty");
        }
        state_->then(std::move(executor), std::move(callback));
        auto cancel = std::move(cancel_);
        cancel_ = nullptr;
        state_.reset();
        return FutureContinuation(std::move(cancel));
    }

    void cancel() noexcept {
        if (cancel_) {
            auto cancel = std::move(cancel_);
            cancel_ = nullptr;
            cancel();
        }
    }

private:
    void require_state() const {
        if (!state_) {
            throw std::future_error(std::future_errc::no_state);
        }
    }

    std::shared_ptr<detail::FutureCompletionState<T>> state_;
    std::function<void()> cancel_;
};
{%- endif %}
