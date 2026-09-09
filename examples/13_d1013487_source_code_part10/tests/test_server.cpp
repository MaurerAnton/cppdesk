// test_server.cpp — tests for the server foundation (service framework,
// ConnInner queue, cursor cache, Server registry, zombie reaper).
// Hermetic: no network/files; the reaper test uses fork+_exit only.

#include <rustdesk/audio_service.hpp>
#include <rustdesk/clipboard_service.hpp>
#include <rustdesk/connection.hpp>
#include <rustdesk/input_service.hpp>
#include <rustdesk/server.hpp>
#include <rustdesk/service.hpp>
#include <rustdesk/video_service.hpp>

#include <cstdio>

#include <algorithm>
#include <chrono>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

using rustdesk::ConnInner;
using rustdesk::MessagePtr;

// Fake subscriber: proves ServiceTmpl works for non-ConnInner T by wrapping
// one (all traffic observable through the shared queue, like GenericService).
struct FakeSub {
    ConnInner inner;
    explicit FakeSub(const ConnInner& c) : inner(c) {}
    int get_id() const { return inner.get_id(); }
    void send(const MessagePtr& msg) { inner.send(msg); }
};

using FakeService = rustdesk::ServiceTmpl<FakeSub>;

ConnInner make_conn(int32_t id) {
    ConnInner c;
    c.id = id;
    c.tx = std::make_shared<rustdesk::ConnQueue>();
    return c;
}

MessagePtr make_msg() {
    auto m = std::make_shared<hbb::Message>();
    m->mutable_misc()->set_refresh_video(true);
    return m;
}

void test_queue() {
    rustdesk::ConnQueue q;
    CHECK(q.size() == 0);
    auto m1 = make_msg();
    auto m2 = make_msg();
    q.send(m1);
    q.send(m2);
    CHECK(q.size() == 2);
    // Drain preserves order + shared identity (Arc parity).
    auto out = q.drain();
    CHECK(out.size() == 2);
    CHECK(out[0].get() == m1.get() && out[1].get() == m2.get());
    CHECK(q.size() == 0);
    // Empty queue: immediate nullopt (no wait).
    CHECK(!q.wait_receive(0).has_value());
    // Close unblocks waiters.
    q.close();
    CHECK(!q.wait_receive(50).has_value());

    // Default ConnInner (no queue): silent no-op, never throws.
    ConnInner bare;
    CHECK(bare.get_id() == 0);
    bare.send(m1);

    // ConnInner fan-out through the queue.
    const ConnInner c = make_conn(7);
    CHECK(c.get_id() == 7);
    c.send(m1);
    auto got = c.tx->drain();
    CHECK(got.size() == 1 && got[0].get() == m1.get());
}

void test_service_framework() {
    FakeService svc("fake", false);
    CHECK(svc.name() == "fake");
    CHECK(!svc.has_subscribes() && svc.ok() == false);  // ok() needs active+subs
    CHECK(svc.active());

    const ConnInner a = make_conn(1);
    const ConnInner b = make_conn(2);
    svc.on_subscribe(a);
    svc.on_subscribe(a);  // duplicate ignored
    svc.on_subscribe(b);
    CHECK(svc.is_subed(1) && svc.is_subed(2));
    CHECK(svc.has_subscribes() && svc.ok());

    // Fan-out shares one Arc (pointer identity, like upstream).
    auto m = make_msg();
    svc.send(*m);
    auto fa = a.tx->drain();
    auto fb = b.tx->drain();
    CHECK(fa.size() == 1 && fb.size() == 1);
    CHECK(fa[0].get() == fb[0].get());

    // send_without skips exactly one subscriber.
    svc.send_without(*m, 1);
    CHECK(a.tx->drain().empty());
    CHECK(b.tx->drain().size() == 1);

    svc.on_unsubscribe(1);
    CHECK(!svc.is_subed(1) && svc.is_subed(2));
    svc.on_unsubscribe(2);
    CHECK(!svc.has_subscribes());

    // Snapshot path (need_snapshot=true): newcomers wait in new_subscribes,
    // get snapshot sends, then promote on scope exit.
    FakeService snap("snap", true);
    snap.on_subscribe(a);
    CHECK(!snap.is_subed(1));  // not in the MAIN set yet
    bool saw_new = false;
    snap.snapshot([&](rustdesk::ServiceSwap<FakeSub> swap) {
        CHECK(!swap.has_subscribes());  // main set still empty (verbatim quirk)
        auto sm = make_msg();
        swap.send(*sm);
        CHECK(a.tx->drain().size() == 1);  // new_subscribes got it
        saw_new = true;
    });
    CHECK(saw_new);
    CHECK(snap.is_subed(1));  // promoted by ServiceSwap drop

    svc.join();
    snap.join();
    CHECK(!svc.active());
}

void test_cursor_cache() {
    const ConnInner c = make_conn(3);
    rustdesk::input_service::MouseCursorSub sub(c);

    hbb::Message full;
    hbb::CursorData* cd = full.mutable_cursor_data();
    cd->set_id(7);
    cd->set_width(32);
    auto full_ptr = std::make_shared<hbb::Message>(full);

    sub.send(full_ptr);  // first delivery: full message forwarded...
    auto got = c.tx->drain();
    CHECK(got.size() == 1);
    CHECK(got[0]->has_cursor_data() && got[0]->cursor_data().id() == 7);

    sub.send(full_ptr);  // ...second delivery: cached id-only message.
    got = c.tx->drain();
    CHECK(got.size() == 1);
    CHECK(!got[0]->has_cursor_data());
    CHECK(got[0]->has_cursor_id() && got[0]->cursor_id() == 7);

    // Non-cursor traffic passes through untouched.
    auto misc = make_msg();
    sub.send(misc);
    got = c.tx->drain();
    CHECK(got.size() == 1 && got[0].get() == misc.get());

    // Service shells carry the upstream names.
    CHECK(std::string(rustdesk::audio_service::kName) == "audio");
    CHECK(std::string(rustdesk::video_service::kName) == "video");
    CHECK(std::string(rustdesk::clipboard_service::kName) == "clipboard");
    CHECK(std::string(rustdesk::input_service::kNameCursor) == "mouse_cursor");
    CHECK(std::string(rustdesk::input_service::kNamePos) == "mouse_pos");
}

void test_server_registry() {
    auto server = rustdesk::Server::create();
    for (const char* name : {"audio", "video", "clipboard", "mouse_cursor", "mouse_pos"}) {
        CHECK(server->find_service(name) != nullptr);
    }
    CHECK(server->find_service("nope") == nullptr);

    const ConnInner c = make_conn(11);
    server->add_connection(c, {"audio"});  // audio skipped via noperms
    CHECK(server->has_connection(11));
    CHECK(!server->find_service("audio")->is_subed(11));
    // Snapshot services (video/mouse_cursor) stage into new_subscribes on
    // subscribe — is_subed stays false until a snapshot() promotes them
    // (mechanics covered in test_service_framework; here the observable part).
    CHECK(!server->find_service("video")->is_subed(11));
    CHECK(!server->find_service("mouse_cursor")->is_subed(11));
    // Non-snapshot services land in the main set immediately.
    CHECK(server->find_service("clipboard")->is_subed(11));
    CHECK(server->find_service("mouse_pos")->is_subed(11));

    // Server::subscribe toggle path on a directly-subscribed service.
    server->subscribe("clipboard", c, false);
    CHECK(!server->find_service("clipboard")->is_subed(11));
    server->subscribe("clipboard", c, true);
    CHECK(server->find_service("clipboard")->is_subed(11));
    server->subscribe("clipboard", c, true);  // idempotent (is_subed == sub)
    CHECK(server->find_service("clipboard")->is_subed(11));
    server->subscribe("missing", c, true);  // unknown service: silent no-op

    server->remove_connection(c);
    CHECK(!server->has_connection(11));
    CHECK(!server->find_service("clipboard")->is_subed(11));
    CHECK(!server->find_service("mouse_pos")->is_subed(11));

    CHECK(server->next_id() == 1);
    CHECK(server->next_id() == 2);
}

void test_zombie_reaper() {
#if defined(__unix__) || defined(__APPLE__)
    rustdesk::check_zombie();  // detached 100ms sweeper (idempotent start)
    const pid_t pid = ::fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        _exit(0);  // child exits immediately
    }
    rustdesk::track_child_pid(static_cast<intptr_t>(pid));
    bool reaped = false;
    for (int i = 0; i < 40; ++i) {  // up to ~4s
        {
            std::unique_lock l(rustdesk::child_lock());
            const auto& pids = rustdesk::child_processes();
            reaped = std::find(pids.begin(), pids.end(), static_cast<intptr_t>(pid)) == pids.end();
        }
        if (reaped) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(reaped);
#else
    CHECK(true);  // Windows reaper deferred to the platform step
#endif
}

}  // namespace

int main() {
    test_queue();
    test_service_framework();
    test_cursor_cache();
    test_server_registry();
    test_zombie_reaper();
    if (g_failures == 0) {
        std::puts("test_server: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
