// service.hpp — translation of src/server/service.rs (step 13).
//
// Generic publish/subscribe service framework. Upstream is generic over
// `T: Subscriber + From<ConnInner>`; here T must be copy-constructible from
// `const ConnInner&` with `int get_id() const` and
// `void send(const MessagePtr&)` (see connection.hpp; `id()` is renamed
// `get_id()` because a C++ member function cannot share its field's name).
//
// Async/threading mapping:
// - `Arc<RwLock<ServiceInner>>` → shared_ptr<State> (copies share state,
//   exactly like Clone on ServiceTmpl) with an internal shared_mutex.
// - `repeat`/`run` spawn std::thread workers; callback `ResultType<()>` maps
//   to void + exceptions (an error return becomes a throw at the ported
//   call site; the backoff handling is identical).
// - `JoinHandle::join` on drop-server → Service::join() + Server dtor.
// - `#[cfg(windows)] try_change_desktop()` in error paths is deferred to the
//   platform step (marked inline).

#pragma once

#include <rustdesk/connection.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace rustdesk {

// HIBERATE_TIMEOUT / MAX_ERROR_TIMEOUT parity (milliseconds).
inline constexpr uint64_t kHiberateTimeoutMs = 30;
inline constexpr uint64_t kMaxErrorTimeoutMs = 1000;

// Reset parity (per-service loop state reset when nobody subscribes).
struct Reset {
    virtual ~Reset() = default;
    virtual void reset() = 0;
};

// Service parity (Box<dyn Service> becomes shared_ptr<Service>).
class Service {
public:
    virtual ~Service() = default;
    virtual const std::string& name() const = 0;
    virtual void on_subscribe(ConnInner sub) = 0;
    virtual void on_unsubscribe(int32_t id) = 0;
    virtual bool is_subed(int32_t id) const = 0;
    virtual void join() = 0;
};

// Subscriber parity (structural: satisfied by ConnInner, MouseCursorSub,
// test fakes — no inheritance required by the template below).
// Documented here for the contract; dynamic dispatch is not used for T.
struct SubscriberConcept {
    virtual ~SubscriberConcept() = default;
    virtual int get_id() const = 0;
    virtual void send(const MessagePtr& msg) = 0;
};

template <typename T>
class ServiceTmpl;

template <typename T>
class ServiceSwap;

// ServiceTmpl<T> parity (Clone shares state via shared_ptr, like the Arc).
template <typename T>
class ServiceTmpl : public Service {
public:
    ServiceTmpl(std::string name, bool need_snapshot)
        : state_(std::make_shared<State>()) {
        state_->name = std::move(name);
        state_->need_snapshot = need_snapshot;
    }

    const std::string& name() const override {
        std::shared_lock l(state_->mutex);
        return state_->name;
    }

    bool is_subed(int32_t id) const override {
        std::shared_lock l(state_->mutex);
        return state_->subscribes.count(id) > 0;  // new_subscribes excluded (parity)
    }

    void on_subscribe(ConnInner sub) override {
        std::unique_lock l(state_->mutex);
        if (state_->subscribes.count(sub.get_id()) > 0) {
            return;
        }
        if (state_->need_snapshot) {
            state_->new_subscribes.emplace(sub.get_id(), T(sub));
        } else {
            state_->subscribes.emplace(sub.get_id(), T(sub));
        }
    }

    void on_unsubscribe(int32_t id) override {
        std::unique_lock l(state_->mutex);
        if (state_->subscribes.erase(id) == 0) {
            state_->new_subscribes.erase(id);
        }
    }

    void join() override {
        std::optional<std::thread> taken;
        {
            std::unique_lock l(state_->mutex);
            state_->active = false;
            taken = std::move(state_->handle);
        }
        if (taken && taken->joinable()) {
            taken->join();  // JoinHandle::join parity (take() then join)
        }
    }

    bool has_subscribes() const {
        std::shared_lock l(state_->mutex);
        return !state_->subscribes.empty() || !state_->new_subscribes.empty();
    }

    bool ok() const {
        std::shared_lock l(state_->mutex);
        return state_->active &&
               (!state_->subscribes.empty() || !state_->new_subscribes.empty());
    }

    bool active() const {
        std::shared_lock l(state_->mutex);
        return state_->active;
    }

    // snapshot() parity: runs the callback only with pending new subscribers.
    void snapshot(const std::function<void(ServiceSwap<T>)>& callback) {
        bool pending = false;
        {
            std::shared_lock l(state_->mutex);
            pending = !state_->new_subscribes.empty();
        }
        if (pending) {
            callback(ServiceSwap<T>(*this));  // may throw (upstream `?`)
        }
    }

    void send(const hbb::Message& msg) { send_shared(std::make_shared<hbb::Message>(msg)); }

    void send_shared(MessagePtr msg) {
        std::unique_lock l(state_->mutex);
        for (auto& [id, s] : state_->subscribes) {
            (void)id;
            s.send(msg);
        }
    }

    void send_without(const hbb::Message& msg, int32_t skip_id) {
        MessagePtr shared = std::make_shared<hbb::Message>(msg);
        std::unique_lock l(state_->mutex);
        for (auto& [id, s] : state_->subscribes) {
            if (id != skip_id) {
                s.send(shared);
            }
        }
    }

    // repeat() parity: paced worker; errors sleep MAX_ERROR_TIMEOUT.
    // S must be default-constructible with void reset().
    template <typename S>
    void repeat(uint64_t interval_ms, std::function<void(ServiceTmpl<T>, S&)> callback) {
        ServiceTmpl<T> self = *this;
        std::thread worker([self, callback = std::move(callback), interval_ms]() mutable {
            S state;
            while (self.active()) {
                const auto start = std::chrono::steady_clock::now();
                if (self.has_subscribes()) {
                    try {
                        callback(self, state);
                    } catch (const std::exception&) {
                        // log::error parity (silent) + backoff sleep.
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(kMaxErrorTimeoutMs));
                        // #[cfg(windows)] try_change_desktop() deferred (platform step).
                    }
                } else {
                    state.reset();
                }
                const auto elapsed = std::chrono::steady_clock::now() - start;
                const auto budget = std::chrono::milliseconds(interval_ms);
                if (elapsed < budget) {
                    std::this_thread::sleep_for(budget - elapsed);
                }
            }
        });
        std::unique_lock l(state_->mutex);
        state_->handle = std::move(worker);
    }

    // run() parity: hibernate loop with doubling error backoff.
    void run(std::function<void(ServiceTmpl<T>)> callback) {
        ServiceTmpl<T> self = *this;
        std::thread worker([self, callback = std::move(callback)]() mutable {
            uint64_t error_timeout = kHiberateTimeoutMs;
            while (self.active()) {
                if (self.has_subscribes()) {
                    const auto start = std::chrono::steady_clock::now();
                    try {
                        callback(self);
                    } catch (const std::exception&) {
                        if (std::chrono::steady_clock::now() - start >
                            std::chrono::milliseconds(kMaxErrorTimeoutMs)) {
                            error_timeout = kHiberateTimeoutMs;
                        } else {
                            error_timeout *= 2;
                        }
                        if (error_timeout > kMaxErrorTimeoutMs) {
                            error_timeout = kMaxErrorTimeoutMs;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(error_timeout));
                        // #[cfg(windows)] try_change_desktop() deferred (platform step).
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kHiberateTimeoutMs));
            }
        });
        std::unique_lock l(state_->mutex);
        state_->handle = std::move(worker);
    }

private:
    // ServiceInner method parity (used by ServiceSwap and the template itself).
    void send_new_subscribes(MessagePtr msg) {
        std::unique_lock l(state_->mutex);
        for (auto& [id, s] : state_->new_subscribes) {
            (void)id;
            s.send(msg);
        }
    }

    void swap_new_subscribes() {
        std::unique_lock l(state_->mutex);
        for (auto it = state_->new_subscribes.begin(); it != state_->new_subscribes.end();) {
            auto node = state_->new_subscribes.extract(it++);
            node.key() = node.mapped().get_id();
            state_->subscribes.insert(std::move(node));
        }
    }

    bool has_new_subscribes() const {
        std::shared_lock l(state_->mutex);
        return !state_->new_subscribes.empty();
    }

    // ServiceSwap::has_subscribes parity: main set ONLY (unlike has_subscribes).
    bool has_main_subscribes() const {
        std::shared_lock l(state_->mutex);
        return !state_->subscribes.empty();
    }

    struct State {
        std::string name;
        std::optional<std::thread> handle;
        std::unordered_map<int32_t, T> subscribes;
        std::unordered_map<int32_t, T> new_subscribes;
        bool active = true;
        bool need_snapshot = false;
        mutable std::shared_mutex mutex;
    };
    std::shared_ptr<State> state_;

    friend class ServiceSwap<T>;
};

// ServiceSwap<T> parity: sends go to new subscribers only; destruction
// promotes them into the main set (Drop parity).
template <typename T>
class ServiceSwap {
public:
    explicit ServiceSwap(ServiceTmpl<T> service) : service_(std::move(service)) {}
    // Drop parity with a C++ caveat: the callback chain moves this object
    // several times (function argument, lambda parameter), and every
    // moved-from temporary is destroyed too. Only the live holder swaps;
    // moved-from holders (null state_) must skip, or they lock garbage.
    // (Rust moves have a single Drop owner, so upstream needs no guard.)
    ~ServiceSwap() {
        if (service_.state_) {
            swap_new_subscribes();
        }
    }

    ServiceSwap(const ServiceSwap&) = delete;
    ServiceSwap& operator=(const ServiceSwap&) = delete;
    ServiceSwap(ServiceSwap&&) = default;
    ServiceSwap& operator=(ServiceSwap&&) = default;

    void send(const hbb::Message& msg) { send_shared(std::make_shared<hbb::Message>(msg)); }

    void send_shared(MessagePtr msg) { service_.send_new_subscribes(msg); }

    bool has_subscribes() const { return service_.has_main_subscribes(); }

private:
    void swap_new_subscribes() { service_.swap_new_subscribes(); }

    ServiceTmpl<T> service_;
};

// GenericService parity (services whose subscriber IS the connection).
using GenericService = ServiceTmpl<ConnInner>;

}  // namespace rustdesk
