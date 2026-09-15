class AsyncCancelledError: public std::runtime_error {
public:
    AsyncCancelledError(): std::runtime_error("UniFFI async call cancelled") {}
};

template <typename T>
class Future {
public:
    Future(std::future<T> future, std::function<void()> cancel):
        future_(std::move(future)), cancel_(std::move(cancel)) {}

    Future(const Future &) = delete;
    Future &operator=(const Future &) = delete;

    Future(Future &&other) noexcept:
        future_(std::move(other.future_)), cancel_(std::move(other.cancel_)) {
        other.cancel_ = nullptr;
    }

    Future &operator=(Future &&other) noexcept {
        if (this != &other) {
            cancel();
            future_ = std::move(other.future_);
            cancel_ = std::move(other.cancel_);
            other.cancel_ = nullptr;
        }
        return *this;
    }

    ~Future() {
        cancel();
    }

    bool valid() const noexcept {
        return future_.valid();
    }

    decltype(auto) get() {
        return future_.get();
    }

    void wait() const {
        future_.wait();
    }

    template <typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period> &timeout) const {
        return future_.wait_for(timeout);
    }

    void cancel() noexcept {
        if (cancel_) {
            auto cancel_callback = std::move(cancel_);
            cancel_ = nullptr;
            cancel_callback();
        }
    }

private:
    std::future<T> future_;
    std::function<void()> cancel_;
};
