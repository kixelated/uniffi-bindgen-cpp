namespace detail {

template <typename T, typename E>
struct FutureOutput {
    using type = expected<T, E>;
};

template <typename T>
struct FutureOutput<T, void> {
    using type = T;
};

// `std::function<void(T)>` rejects a dependent `void` parameter.
template <typename Output>
struct FutureCallback {
    using type = std::function<void(Output)>;
};

template <>
struct FutureCallback<void> {
    using type = std::function<void()>;
};

// Holds one async result until a blocking `get()` or a continuation claims it. A future
// whose Rust side was cancelled or dropped is abandoned instead: its continuation never
// runs, and a `get()` on it aborts.
template <typename Output>
class FutureCompletionState {
public:
    using Stored = std::conditional_t<std::is_void_v<Output>, std::monostate, Output>;
    using Callback = typename detail::FutureCallback<Output>::type;

    void complete(Stored result) noexcept {
        Callback callback;
        AsyncDispatcher executor;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (status_ != Status::Pending) {
                return;
            }
            status_ = Status::Ready;
            result_.emplace(std::move(result));
            callback = std::move(callback_);
            executor = std::move(executor_);
        }
        ready_.notify_all();
        if (callback) {
            dispatch_continuation(std::move(executor), std::move(callback));
        }
    }

    void abandon() noexcept {
        Callback callback;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (status_ != Status::Pending) {
                return;
            }
            status_ = Status::Abandoned;
            // Released outside the lock: the callback's captures may own anything.
            callback = std::move(callback_);
            executor_ = nullptr;
        }
        ready_.notify_all();
    }

    Output get() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        claim_locked();
        ready_.wait(lock, [this] { return status_ != Status::Pending; });
        if (status_ == Status::Abandoned) {
            fatal("UniFFI future abandoned: its dispatcher shut down or rejected a poll");
        }
        auto result = std::move(*result_);
        result_.reset();
        lock.unlock();
        if constexpr (std::is_void_v<Output>) {
            (void)result;
        } else {
            return result;
        }
    }

    void wait() const noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return status_ != Status::Pending; });
    }

    template <typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period> &timeout) const noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        return ready_.wait_for(lock, timeout, [this] { return status_ != Status::Pending; })
            ? std::future_status::ready
            : std::future_status::timeout;
    }

    void then(AsyncDispatcher executor, Callback callback) noexcept {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            claim_locked();
            if (status_ == Status::Pending) {
                executor_ = std::move(executor);
                callback_ = std::move(callback);
                return;
            }
            if (status_ == Status::Abandoned) {
                return;
            }
        }
        dispatch_continuation(std::move(executor), std::move(callback));
    }

private:
    enum class Status { Pending, Ready, Abandoned };

    void claim_locked() noexcept {
        if (claimed_) {
            fatal("UniFFI future result has already been consumed");
        }
        claimed_ = true;
    }

    // An executor that rejects the task drops the continuation along with the result.
    void dispatch_continuation(AsyncDispatcher executor, Callback callback) noexcept {
        auto result = [&] {
            std::lock_guard<std::mutex> guard(mutex_);
            auto result = std::make_shared<Stored>(std::move(*result_));
            result_.reset();
            return result;
        }();
        executor([callback = std::move(callback), result]() mutable {
            if constexpr (std::is_void_v<Output>) {
                callback();
            } else {
                callback(std::move(*result));
            }
        });
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable ready_;
    Status status_ = Status::Pending;
    std::optional<Stored> result_;
    bool claimed_ = false;
    AsyncDispatcher executor_;
    Callback callback_;
};

} // namespace detail

// What an async call delivers: `expected<T, E>` when the Rust function returns a `Result`,
// otherwise `T`.
template <typename T, typename E = void>
using FutureOutput = typename detail::FutureOutput<T, E>::type;

// Owns a continuation attached with `Future::then`. Destroying or cancelling it cancels the
// Rust future, and the continuation does not run.
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

    // Cancels the Rust future; the continuation will not run.
    void cancel() noexcept {
        if (cancel_) {
            auto cancel = std::move(cancel_);
            cancel_ = nullptr;
            cancel();
        }
    }

private:
    std::function<void()> cancel_;
};

// A pending Rust async call. Block on it with `get()`, or attach a continuation with
// `std::move(future).then(executor, callback)`. Destroying or cancelling an incomplete
// future drops the Rust future, which aborts its work.
template <typename T, typename E = void>
class Future {
public:
    using Output = FutureOutput<T, E>;
    using Callback = typename detail::FutureCallback<Output>::type;

    Future(std::shared_ptr<detail::FutureCompletionState<Output>> state, std::function<void()> cancel):
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

    // False once the result was claimed by `get()` or `then()`, or the future was cancelled.
    bool valid() const noexcept {
        return state_ != nullptr;
    }

    // Blocks until the result is ready and returns it, leaving the future invalid.
    Output get() noexcept {
        require_state();
        auto state = std::move(state_);
        cancel_ = nullptr;
        return state->get();
    }

    // Blocks until the result is ready.
    void wait() const noexcept {
        require_state();
        state_->wait();
    }

    // Blocks until the result is ready or the timeout elapses.
    template <typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period> &timeout) const noexcept {
        require_state();
        return state_->wait_for(timeout);
    }

    // Runs `callback` with the result on `executor`. The returned handle owns the Rust
    // future: destroying it cancels the call and the callback does not run.
    [[nodiscard]] FutureContinuation then(AsyncDispatcher executor, Callback callback) && noexcept {
        require_state();
        if (!executor || !callback) {
            detail::fatal("UniFFI future continuation must not be empty");
        }
        state_->then(std::move(executor), std::move(callback));
        auto cancel = std::move(cancel_);
        cancel_ = nullptr;
        state_.reset();
        return FutureContinuation(std::move(cancel));
    }

    // Drops the Rust future, aborting its work, and leaves this future invalid.
    void cancel() noexcept {
        state_.reset();
        if (cancel_) {
            auto cancel = std::move(cancel_);
            cancel_ = nullptr;
            cancel();
        }
    }

private:
    void require_state() const noexcept {
        if (!state_) {
            detail::fatal("UniFFI future has no state: it was consumed or cancelled");
        }
    }

    std::shared_ptr<detail::FutureCompletionState<Output>> state_;
    std::function<void()> cancel_;
};
