// connection.hpp — translation of src/server/connection.rs, part 1 (step 13).
//
// Only ConnInner + its message queue land here. ConnInner is the unit the
// service framework passes around; the queue replaces tokio's unbounded
// mpsc (send never blocks, like upstream). The Connection event loop,
// on_message handlers and start_ipc need ipc/services/platform and arrive
// in later steps.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

// Generated protobuf (message.proto): queued payload type.
#include "message.pb.h"

namespace rustdesk {

using MessagePtr = std::shared_ptr<hbb::Message>;

// One queued item: (send Instant, message) parity.
struct QueuedMessage {
    std::chrono::steady_clock::time_point instant;
    MessagePtr message;
};

// Unbounded multi-producer channel (mpsc::unbounded_channel parity).
// send() never blocks and never fails; waiters unblock on close().
class ConnQueue {
public:
    void send(MessagePtr msg);
    // Non-blocking drain (test + service fan-out support).
    std::vector<MessagePtr> drain();
    // Blocking receive with timeout; nullopt on timeout or after close().
    std::optional<MessagePtr> wait_receive(uint64_t timeout_ms);
    void close();
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QueuedMessage> queue_;
    bool closed_ = false;
};

using ConnQueuePtr = std::shared_ptr<ConnQueue>;

// ConnInner parity (id + optional sender). Default-constructed (id 0, no
// queue) sends are silent no-ops, mirroring tx: None. Copyable (id + shared
// queue); upstream derives Clone for the same reason.
struct ConnInner {
    int32_t id = 0;
    ConnQueuePtr tx;  // nullptr == None

    // Structural Subscriber model: ServiceTmpl<T> calls get_id()/send().
    // (Upstream's trait method is id(), which cannot share the field's name
    // in C++; renamed here and in the template, semantics unchanged.)
    int get_id() const { return id; }
    // Subscriber::send parity: forwards into the queue when present.
    void send(const MessagePtr& msg) const;
};

}  // namespace rustdesk
