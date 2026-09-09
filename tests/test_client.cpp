// test_client.cpp — tests for rustdesk::LoginConfigHandler + handshake helpers.
// Hermetic: XDG_CONFIG_HOME/HOME point at a temp dir before any Config global
// loads (peer files then stay inside it); network stays on loopback.

#include <rustdesk/client.hpp>
#include <rustdesk/common.hpp>

#include <openssl/sha.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

struct FakeInterface : rustdesk::Interface {
    struct Box {
        std::string type, title, text;
    };
    std::vector<Box> boxes;
    void msgbox(const std::string& t, const std::string& ti, const std::string& tx) override {
        boxes.push_back({t, ti, tx});
    }
    bool handle_login_error(const std::string&) override { return false; }
    void handle_peer_info(const hbb::PeerInfo&) override {}
    void handle_hash(const hbb::Hash&, hbb_common::TcpFramedStream&) override {}
    void handle_login_from_ui(const std::string&, bool, hbb_common::TcpFramedStream&) override {}
    void handle_test_delay(const hbb::TestDelay&, hbb_common::TcpFramedStream&) override {}
};

std::vector<uint8_t> sha256_str(const std::string& s) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out.data());
    return out;
}

void test_key_map() {
    const auto& m = rustdesk::key_map();
    CHECK(m.size() >= 100);
    const auto it_a = m.find("VK_A");
    CHECK(it_a != m.end() && it_a->second == rustdesk::Key::chr('a'));
    const auto it_enter = m.find("VK_ENTER");
    CHECK(it_enter != m.end() && it_enter->second == rustdesk::Key::control_key(hbb::Return));
    const auto it_f12 = m.find("VK_F12");
    CHECK(it_f12 != m.end() && it_f12->second == rustdesk::Key::control_key(hbb::F12));
    const auto it_cad = m.find("CTRL_ALT_DEL");
    CHECK(it_cad != m.end() && it_cad->second == rustdesk::Key::control_key(hbb::CtrlAltDel));
    const auto it_lock = m.find("LOCK_SCREEN");
    CHECK(it_lock != m.end() && it_lock->second == rustdesk::Key::control_key(hbb::LockScreen));
    const auto it_plus = m.find("VK_PLUS");
    CHECK(it_plus != m.end() && it_plus->second == rustdesk::Key::chr('='));
    const auto it_quote = m.find("VK_QUOTE");
    CHECK(it_quote != m.end() && it_quote->second == rustdesk::Key::chr('\''));
    CHECK(m.find("VK_NOPE") == m.end());
}

void test_login_config() {
    // Fresh peer: no quality, no toggles → login carries no option message.
    rustdesk::LoginConfigHandler fresh;
    fresh.initialize("freshpeer", false, false);
    CHECK(!fresh.create_login_msg({}).login_request().has_option());

    rustdesk::LoginConfigHandler lc;
    lc.initialize("clitest", false, false);
    CHECK(!lc.remember);  // fresh peer file: no stored password

    auto msg = lc.toggle_option("show-remote-cursor");
    CHECK(msg.has_value());
    CHECK(msg->misc().option().show_remote_cursor() == hbb::Yes);
    CHECK(lc.get_toggle_option("show-remote-cursor"));
    CHECK(hbb_common::peer_load("clitest").show_remote_cursor);

    msg = lc.toggle_option("show-remote-cursor");
    CHECK(msg.has_value());
    CHECK(msg->misc().option().show_remote_cursor() == hbb::No);

    auto q = lc.save_image_quality("best");
    CHECK(q.has_value());
    CHECK(q->misc().option().image_quality() == hbb::Best);
    CHECK(hbb_common::peer_load("clitest").image_quality == "best");

    // Unknown quality persists but yields no message.
    q = lc.save_image_quality("weird");
    CHECK(!q.has_value());
    CHECK(hbb_common::peer_load("clitest").image_quality == "weird");

    // Plain option toggle: no message, in-memory + on-disk flip.
    CHECK(lc.toggle_option("custom-flag") == std::nullopt);
    CHECK(lc.get_toggle_option("custom-flag"));
    CHECK(lc.toggle_option("custom-flag") == std::nullopt);
    CHECK(!lc.get_toggle_option("custom-flag"));

    // Login message carries the session id + our id.
    const hbb::Message login = lc.create_login_msg({});
    CHECK(login.has_login_request());
    CHECK(login.login_request().username() == "clitest");
    CHECK(login.login_request().my_id() == hbb_common::get_id());
    CHECK(login.login_request().password().empty());
}

void test_login_error() {
    rustdesk::LoginConfigHandler lc;
    lc.initialize("clitest", false, false);
    FakeInterface iface;
    CHECK(lc.handle_login_error("Wrong Password", iface));
    CHECK(iface.boxes.size() == 1 && iface.boxes[0].type == "re-input-password");
    CHECK(!lc.handle_login_error("Offline", iface));
    CHECK(iface.boxes.size() == 2 && iface.boxes[1].type == "error");
}

void test_username() {
    rustdesk::LoginConfigHandler lc;
    lc.initialize("clitest", false, false);
    hbb::PeerInfo pi;
    pi.set_username("alice");
    CHECK(lc.get_username(pi) == "alice");
    hbb::PeerInfo empty;
    CHECK(lc.get_username(empty) == "");  // falls back to stored (empty) info
}

// Loopback pair: client side reads what handle_* sends on the server side.
struct Loopback {
    hbb_common::TcpListener listener;
    hbb_common::TcpFramedStream client;
    hbb_common::TcpFramedStream server;
};

std::optional<Loopback> make_loopback() {
    auto listener =
        hbb_common::TcpListener::bind(hbb_common::to_socket_addr("127.0.0.1:0"), true);
    if (!listener || !listener->local_addr()) {
        return std::nullopt;
    }
    auto client = hbb_common::TcpFramedStream::connect(
        *listener->local_addr(), hbb_common::get_any_listen_addr(), 5000);
    if (!client) {
        return std::nullopt;
    }
    auto server = listener->accept(5000);
    if (!server) {
        return std::nullopt;
    }
    Loopback lb{std::move(*listener), std::move(*client), std::move(*server)};
    return std::make_optional(std::move(lb));
}

hbb::Message recv_login(hbb_common::TcpFramedStream& s) {
    auto frame = s.next_timeout(5000);
    CHECK(frame.has_value());
    hbb::Message msg;
    CHECK(msg.ParseFromArray(frame->data(), static_cast<int>(frame->size())));
    CHECK(msg.has_login_request());
    return msg;
}

void test_hash_flows() {
    auto lb = make_loopback();
    CHECK(lb.has_value());
    if (!lb) {
        return;
    }
    auto lc = std::make_shared<rustdesk::LoginShared>();
    lc->handler.initialize("clitest", false, false);
    FakeInterface iface;

    // Empty password: empty login goes out, UI is asked for a password.
    hbb::Hash h;
    h.set_salt("ss");
    h.set_challenge("cc");
    rustdesk::handle_hash(lc, h, iface, lb->server);
    const hbb::Message first = recv_login(lb->client);
    CHECK(first.login_request().password().empty());
    CHECK(iface.boxes.size() == 1 && iface.boxes[0].type == "input-password");

    // UI password: sha256 chains (salt, then challenge) go out hashed.
    rustdesk::handle_login_from_ui(lc, "secret", true, lb->server);
    CHECK(lc->handler.remember);
    const hbb::Message second = recv_login(lb->client);
    const std::vector<uint8_t> r1 = sha256_str("secretss");
    std::string r1s(r1.begin(), r1.end());
    const std::vector<uint8_t> r2 = sha256_str(r1s + "cc");
    CHECK(second.login_request().password() == std::string(r2.begin(), r2.end()));
}

void test_data_enum() {
    rustdesk::Data d = rustdesk::DataLogin{"pw", true};
    CHECK(std::holds_alternative<rustdesk::DataLogin>(d));
    CHECK(std::get<rustdesk::DataLogin>(d).password == "pw");
    d = rustdesk::DataClose{};
    CHECK(std::holds_alternative<rustdesk::DataClose>(d));
    d = rustdesk::DataAddPortForward{1, "h", 80};
    CHECK(std::get<rustdesk::DataAddPortForward>(d).b == 80);
}

}  // namespace

int main() {
    // Hermetic config dir BEFORE any Config global can load.
    const fs::path tmp = fs::temp_directory_path() / "cppdesk_test_client_home";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    ::setenv("XDG_CONFIG_HOME", tmp.c_str(), 1);
    ::setenv("HOME", tmp.c_str(), 1);

    test_key_map();
    test_login_config();
    test_login_error();
    test_username();
    test_hash_flows();
    test_data_enum();
    if (g_failures == 0) {
        std::puts("test_client: all tests passed");
    }
    fs::remove_all(tmp, ec);
    return g_failures == 0 ? 0 : 1;
}
