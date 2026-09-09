// connection.cpp — see connection.hpp (part 1: queue + ConnInner).
#include <rustdesk/connection.hpp>

namespace rustdesk {

void ConnQueue::send(MessagePtr msg) {
    {
        std::unique_lock l(mutex_);
        queue_.push_back(QueuedMessage{std::chrono::steady_clock::now(), std::move(msg)});
    }
    cv_.notify_one();
}

std::vector<MessagePtr> ConnQueue::drain() {
    std::vector<MessagePtr> out;
    std::unique_lock l(mutex_);
    while (!queue_.empty()) {
        out.push_back(std::move(queue_.front().message));
        queue_.pop_front();
    }
    return out;
}

std::optional<MessagePtr> ConnQueue::wait_receive(uint64_t timeout_ms) {
    std::unique_lock l(mutex_);
    if (queue_.empty() && !closed_) {
        cv_.wait_for(l, std::chrono::milliseconds(timeout_ms),
                     [&] { return !queue_.empty() || closed_; });
    }
    if (queue_.empty()) {
        return std::nullopt;  // timeout or close() parity (mpsc disconnect)
    }
    MessagePtr msg = std::move(queue_.front().message);
    queue_.pop_front();
    return msg;
}

void ConnQueue::close() {
    {
        std::unique_lock l(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
}

size_t ConnQueue::size() const {
    std::unique_lock l(mutex_);
    return queue_.size();
}

void ConnInner::send(const MessagePtr& msg) const {
    if (tx) {
        tx->send(msg);  // unbounded: never blocks, never fails (allow_err parity)
    }
    // tx: None parity: silent no-op.
}

}  // namespace rustdesk
