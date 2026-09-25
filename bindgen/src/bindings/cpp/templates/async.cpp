constexpr int8_t UNIFFI_RUST_FUTURE_POLL_READY = 0;
constexpr int8_t UNIFFI_RUST_FUTURE_POLL_WAKE = 1;
{%- if config.expected() %}
{% include "async_expected.cpp" %}
{%- else %}

class ForeignFutureTaskState {
public:
    bool finish() noexcept {
        std::lock_guard<std::mutex> guard(mutex_);
        if (finished_) {
            return false;
        }
        finished_ = true;
        return true;
    }

    void set_cancel(std::function<void()> cancel) noexcept {
        std::function<void()> cancel_now;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (cancel_requested_) {
                cancel_now = std::move(cancel);
            } else if (!finished_) {
                cancel_ = std::move(cancel);
            }
        }
        if (cancel_now) {
            try {
                cancel_now();
            } catch (...) {
            }
        }
    }

    void cancel() noexcept {
        std::function<void()> cancel;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (finished_) {
                return;
            }
            finished_ = true;
            cancel_requested_ = true;
            cancel = std::move(cancel_);
        }
        if (cancel) {
            try {
                cancel();
            } catch (...) {
            }
        }
    }

private:
    std::mutex mutex_;
    bool finished_ = false;
    bool cancel_requested_ = false;
    std::function<void()> cancel_;
};

inline uint64_t foreign_future_handle(const std::shared_ptr<ForeignFutureTaskState> &state) {
    return reinterpret_cast<uint64_t>(
        new std::shared_ptr<ForeignFutureTaskState>(state)
    );
}

inline void foreign_future_drop(uint64_t handle) noexcept {
    auto state = std::unique_ptr<std::shared_ptr<ForeignFutureTaskState>>(
        reinterpret_cast<std::shared_ptr<ForeignFutureTaskState> *>(handle)
    );
    (*state)->cancel();
}

template <typename T, typename Poll, typename Cancel, typename Complete, typename Free, typename Lift, typename ErrorHandler>
class RustFutureState: public std::enable_shared_from_this<RustFutureState<T, Poll, Cancel, Complete, Free, Lift, ErrorHandler>> {
public:
    RustFutureState(
        uint64_t handle,
        Poll poll,
        Cancel cancel,
        Complete complete,
        Free free,
        Lift lift,
        ErrorHandler error_handler,
        std::shared_ptr<::uniffi::detail::FutureCompletionState<T>> completion_state
    ):
        handle_(handle),
        poll_(std::move(poll)),
        cancel_(std::move(cancel)),
        complete_(std::move(complete)),
        free_(std::move(free)),
        lift_(std::move(lift)),
        error_handler_(std::move(error_handler)),
        completion_state_(std::move(completion_state)) {}

    void poll() {
        using State = RustFutureState<T, Poll, Cancel, Complete, Free, Lift, ErrorHandler>;
        auto *callback_state = new std::shared_ptr<State>(this->shared_from_this());
        poll_(handle_, &RustFutureState::continuation, reinterpret_cast<uint64_t>(callback_state));
    }

    void cancel() noexcept {
        if (!finished_.load()) {
            cancelled_.store(true);
            cancel_(handle_);
        }
    }

private:
    static void continuation(uint64_t callback_data, int8_t poll_result) noexcept {
        using State = RustFutureState<T, Poll, Cancel, Complete, Free, Lift, ErrorHandler>;
        auto callback_state = std::unique_ptr<std::shared_ptr<State>>(
            reinterpret_cast<std::shared_ptr<State> *>(callback_data)
        );
        auto state = *callback_state;

        if (!::uniffi::detail::dispatch_async(
            [state, poll_result]() {
                state->resume(poll_result);
            }
        )) {
            state->dispatch_failed();
        }
    }

    void resume(int8_t poll_result) noexcept {
        if (poll_result == UNIFFI_RUST_FUTURE_POLL_WAKE) {
            try {
                poll();
            } catch (...) {
                fail(std::current_exception());
            }
        } else if (poll_result == UNIFFI_RUST_FUTURE_POLL_READY) {
            complete();
        } else {
            fail(std::make_exception_ptr(
                std::runtime_error("Unexpected UniFFI Rust future poll result")
            ));
        }
    }

    void complete() noexcept {
        if (finished_.exchange(true)) {
            return;
        }

        try {
            RustCallStatus status{};
            if constexpr (std::is_void_v<T>) {
                complete_(handle_, &status);
                if (cancelled_.load()) {
                    completion_state_->complete(::uniffi::FutureResult<void>(
                        std::make_exception_ptr(::uniffi::AsyncCancelledError())
                    ));
                } else {
                    check_rust_call(status, error_handler_);
                    completion_state_->complete(::uniffi::FutureResult<void>());
                }
            } else {
                auto value = complete_(handle_, &status);
                if (cancelled_.load()) {
                    completion_state_->complete(::uniffi::FutureResult<T>(
                        std::make_exception_ptr(::uniffi::AsyncCancelledError())
                    ));
                } else {
                    check_rust_call(status, error_handler_);
                    completion_state_->complete(::uniffi::FutureResult<T>(lift_(value)));
                }
            }
        } catch (...) {
            completion_state_->complete(::uniffi::FutureResult<T>(std::current_exception()));
        }
        free_(handle_);
    }

    void fail(std::exception_ptr error) noexcept {
        if (finished_.exchange(true)) {
            return;
        }
        completion_state_->complete(::uniffi::FutureResult<T>(std::move(error)));
        free_(handle_);
    }

    void dispatch_failed() noexcept {
        try {
            cancel_(handle_);
        } catch (...) {
        }
        fail(std::make_exception_ptr(::uniffi::AsyncDispatcherError()));
    }

    uint64_t handle_;
    Poll poll_;
    Cancel cancel_;
    Complete complete_;
    Free free_;
    Lift lift_;
    ErrorHandler error_handler_;
    std::shared_ptr<::uniffi::detail::FutureCompletionState<T>> completion_state_;
    std::atomic<bool> cancelled_ = false;
    std::atomic<bool> finished_ = false;
};

template <typename T, typename RustFuture, typename Poll, typename Cancel, typename Complete, typename Free, typename Lift, typename ErrorHandler>
Future<T> rust_call_async(
    RustFuture rust_future,
    Poll poll,
    Cancel cancel,
    Complete complete,
    Free free,
    Lift lift,
    ErrorHandler error_handler
) {
    initialize();
    const auto handle = rust_future();
    using State = RustFutureState<T, Poll, Cancel, Complete, Free, Lift, ErrorHandler>;

    auto completion_state = std::make_shared<::uniffi::detail::FutureCompletionState<T>>();
    auto state = std::make_shared<State>(
        handle, poll, cancel, complete, free, lift, error_handler, completion_state
    );
    try {
        state->poll();
    } catch (...) {
        free(handle);
        throw;
    }

    return Future<T>(std::move(completion_state), [weak_state = std::weak_ptr<State>(state)]() {
        if (auto state = weak_state.lock()) {
            state->cancel();
        }
    });
}
{%- endif %}
