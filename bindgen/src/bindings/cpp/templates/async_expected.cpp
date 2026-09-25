// Drives one Rust future. The handle is freed only when the last reference to this state
// drops: a pending poll and an in-progress `cancel()` each hold one, so neither can race
// the free.
template <typename T, typename E, typename Poll, typename Cancel, typename Complete, typename Free, typename Lift, typename ErrorHandler>
class RustFutureState: public std::enable_shared_from_this<RustFutureState<T, E, Poll, Cancel, Complete, Free, Lift, ErrorHandler>> {
public:
    using Output = ::uniffi::FutureOutput<T, E>;
    using Completion = ::uniffi::detail::FutureCompletionState<Output>;

    RustFutureState(
        uint64_t handle,
        Poll poll,
        Cancel cancel,
        Complete complete,
        Free free,
        Lift lift,
        ErrorHandler error_handler,
        std::shared_ptr<Completion> completion_state
    ):
        handle_(handle),
        poll_(std::move(poll)),
        cancel_(std::move(cancel)),
        complete_(std::move(complete)),
        free_(std::move(free)),
        lift_(std::move(lift)),
        error_handler_(std::move(error_handler)),
        completion_state_(std::move(completion_state)) {}

    RustFutureState(const RustFutureState &) = delete;
    RustFutureState &operator=(const RustFutureState &) = delete;

    ~RustFutureState() {
        if (!finished_.load()) {
            release_uncollected(handle_, complete_, lift_, error_handler_);
        }
        free_(handle_);
    }

    void poll() noexcept {
        using State = RustFutureState<T, E, Poll, Cancel, Complete, Free, Lift, ErrorHandler>;
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
        using State = RustFutureState<T, E, Poll, Cancel, Complete, Free, Lift, ErrorHandler>;
        auto callback_state = std::unique_ptr<std::shared_ptr<State>>(
            reinterpret_cast<std::shared_ptr<State> *>(callback_data)
        );
        auto state = *callback_state;

        if (!::uniffi::detail::dispatch_async(
            [state, poll_result]() {
                state->resume(poll_result);
            }
        )) {
            // Nothing will poll again, so drop the Rust future with the last reference.
            state->abandon();
        }
    }

    void resume(int8_t poll_result) noexcept {
        if (poll_result == UNIFFI_RUST_FUTURE_POLL_WAKE) {
            poll();
        } else if (poll_result == UNIFFI_RUST_FUTURE_POLL_READY) {
            complete();
        } else {
            ::uniffi::detail::fatal("Unexpected UniFFI Rust future poll result");
        }
    }

    void complete() noexcept {
        if (finished_.exchange(true)) {
            return;
        }

        RustCallStatus status{};
        using Stored = typename Completion::Stored;
        std::optional<Stored> result;
        if constexpr (std::is_void_v<T>) {
            complete_(handle_, &status);
            if (status.code == 0) {
                result.emplace();
            }
        } else {
            auto value = complete_(handle_, &status);
            if (status.code == 0) {
                // Lifted even when cancelled, so the value's buffer or handle is released.
                result.emplace(lift_(value));
            }
        }

        if (status.code == UNIFFI_RUST_CALL_CANCELLED) {
            completion_state_->abandon();
            return;
        }
        if (status.code == UNIFFI_RUST_CALL_ERROR) {
            if constexpr (!std::is_void_v<E>) {
                result.emplace(::uniffi::unexpected<E>(error_handler_(status.error_buf)));
            }
        }
        check_rust_call(status, error_handler_);

        if (cancelled_.load()) {
            completion_state_->abandon();
        } else {
            completion_state_->complete(std::move(*result));
        }
    }

    // Leaves `finished_` unset so the destructor releases the Rust result.
    void abandon() noexcept {
        completion_state_->abandon();
    }

    uint64_t handle_;
    Poll poll_;
    Cancel cancel_;
    Complete complete_;
    Free free_;
    Lift lift_;
    ErrorHandler error_handler_;
    std::shared_ptr<Completion> completion_state_;
    std::atomic<bool> cancelled_ = false;
    std::atomic<bool> finished_ = false;
};

template <typename T, typename E = void, typename RustFuture, typename Poll, typename Cancel, typename Complete, typename Free, typename Lift, typename ErrorHandler>
::uniffi::Future<T, E> rust_call_async(
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
    using State = RustFutureState<T, E, Poll, Cancel, Complete, Free, Lift, ErrorHandler>;

    auto completion_state = std::make_shared<typename State::Completion>();
    auto state = std::make_shared<State>(
        handle, poll, cancel, complete, free, lift, error_handler, completion_state
    );
    state->poll();

    return ::uniffi::Future<T, E>(std::move(completion_state), [weak_state = std::weak_ptr<State>(state)]() {
        if (auto state = weak_state.lock()) {
            state->cancel();
        }
    });
}
