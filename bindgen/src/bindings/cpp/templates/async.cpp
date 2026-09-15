constexpr int8_t UNIFFI_RUST_FUTURE_POLL_READY = 0;
constexpr int8_t UNIFFI_RUST_FUTURE_POLL_WAKE = 1;

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
        ErrorHandler error_handler
    ):
        handle_(handle),
        poll_(std::move(poll)),
        cancel_(std::move(cancel)),
        complete_(std::move(complete)),
        free_(std::move(free)),
        lift_(std::move(lift)),
        error_handler_(std::move(error_handler)) {}

    std::future<T> get_future() {
        return promise_.get_future();
    }

    void start() noexcept {
        try {
            auto state = this->shared_from_this();
            std::thread([state = std::move(state)]() {
                state->run();
            }).detach();
        } catch (...) {
            auto error = std::current_exception();
            free_(handle_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                finished_ = true;
            }
            try {
                promise_.set_exception(std::move(error));
            } catch (...) {
            }
        }
    }

    void cancel() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finished_) {
                return;
            }
            cancel_requested_ = true;
        }
        wake_.notify_one();
    }

private:
    enum class Work {
        PollFuture,
        CancelFuture,
        CompleteFuture,
    };

    using State = RustFutureState<T, Poll, Cancel, Complete, Free, Lift, ErrorHandler>;

    static void continuation(uint64_t callback_data, int8_t poll_result) noexcept {
        auto callback_state = std::unique_ptr<std::shared_ptr<State>>(
            reinterpret_cast<std::shared_ptr<State> *>(callback_data)
        );
        (*callback_state)->resume(poll_result);
    }

    void resume(int8_t poll_result) noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finished_) {
                return;
            }

            if (poll_result == UNIFFI_RUST_FUTURE_POLL_READY) {
                ready_ = true;
            } else if (poll_result == UNIFFI_RUST_FUTURE_POLL_WAKE) {
                poll_requested_ = true;
            } else {
                failure_ = std::make_exception_ptr(
                    std::runtime_error("Unexpected UniFFI Rust future poll result")
                );
                cancel_requested_ = true;
            }
        }
        wake_.notify_one();
    }

    void run() noexcept {
        for (;;) {
            Work work;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this]() {
                    return finished_ || ready_ || poll_requested_ ||
                        (cancel_requested_ && !cancel_sent_);
                });

                if (finished_) {
                    return;
                }

                if (cancel_requested_ && !cancel_sent_) {
                    cancel_sent_ = true;
                    work = Work::CancelFuture;
                } else if (ready_) {
                    ready_ = false;
                    work = Work::CompleteFuture;
                } else {
                    poll_requested_ = false;
                    work = Work::PollFuture;
                }
            }

            if (work == Work::PollFuture) {
                poll();
            } else if (work == Work::CancelFuture) {
                cancel_rust_future();
            } else {
                complete();
                return;
            }
        }
    }

    void poll() noexcept {
        std::shared_ptr<State> *callback_state = nullptr;
        try {
            callback_state = new std::shared_ptr<State>(this->shared_from_this());
            poll_(handle_, &RustFutureState::continuation, reinterpret_cast<uint64_t>(callback_state));
        } catch (...) {
            delete callback_state;
            fail(std::current_exception());
        }
    }

    void cancel_rust_future() noexcept {
        try {
            cancel_(handle_);
        } catch (...) {
            fail(std::current_exception());
        }

        // Cancellation makes a Rust future ready even if it had not yet been polled.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_ = true;
        }
        wake_.notify_one();
    }

    void fail(std::exception_ptr error) noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!failure_) {
                failure_ = std::move(error);
            }
            cancel_requested_ = true;
        }
        wake_.notify_one();
    }

    void complete() noexcept {
        bool cancelled;
        std::exception_ptr failure;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled = cancel_requested_;
            failure = failure_;
        }

        try {
            RustCallStatus status = { 0 };
            if constexpr (std::is_void_v<T>) {
                complete_(handle_, &status);
                if (failure || cancelled) {
                    try {
                        check_rust_call(status, error_handler_);
                    } catch (...) {
                    }
                    set_terminal_exception(failure, cancelled);
                } else {
                    check_rust_call(status, error_handler_);
                    promise_.set_value();
                }
            } else {
                auto value = complete_(handle_, &status);
                if (failure || cancelled) {
                    try {
                        check_rust_call(status, error_handler_);
                        static_cast<void>(lift_(value));
                    } catch (...) {
                    }
                    set_terminal_exception(failure, cancelled);
                } else {
                    check_rust_call(status, error_handler_);
                    promise_.set_value(lift_(value));
                }
            }
        } catch (...) {
            try {
                promise_.set_exception(std::current_exception());
            } catch (...) {
            }
        }

        free_(handle_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
    }

    void set_terminal_exception(const std::exception_ptr &failure, bool cancelled) noexcept {
        try {
            if (failure) {
                promise_.set_exception(failure);
            } else if (cancelled) {
                promise_.set_exception(std::make_exception_ptr(::uniffi::AsyncCancelledError()));
            }
        } catch (...) {
        }
    }

    uint64_t handle_;
    Poll poll_;
    Cancel cancel_;
    Complete complete_;
    Free free_;
    Lift lift_;
    ErrorHandler error_handler_;
    std::promise<T> promise_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::exception_ptr failure_;
    bool poll_requested_ = true;
    bool cancel_requested_ = false;
    bool cancel_sent_ = false;
    bool ready_ = false;
    bool finished_ = false;
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

    auto state = std::make_shared<State>(
        handle,
        std::move(poll),
        std::move(cancel),
        std::move(complete),
        std::move(free),
        std::move(lift),
        std::move(error_handler)
    );
    auto future = state->get_future();
    state->start();

    return Future<T>(std::move(future), [weak_state = std::weak_ptr<State>(state)]() {
        if (auto state = weak_state.lock()) {
            state->cancel();
        }
    });
}
