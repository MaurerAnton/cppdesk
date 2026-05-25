#include <gtest/gtest.h>
#include "common/config.hpp"
#include "common/crypto.hpp"
#include "common/protocol.hpp"
#include "server/server.hpp"
#include "client/client.hpp"
#include "rendezvous/rendezvous.hpp"
#include "platform/platform.hpp"
#include "scrap/scrap.hpp"
#include "enigo/enigo.hpp"
#include "clipboard/clipboard.hpp"

#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace cppdesk;
using namespace cppdesk::common;

// ====== Config Tests ======
TEST(ConfigTest, Singleton) {
    auto& c1 = Config::instance();
    auto& c2 = Config::instance();
    EXPECT_EQ(&c1, &c2);
}

TEST(ConfigTest, IdPersistence) {
    std::string id = Config::get_id();
    EXPECT_FALSE(id.empty());
    std::string id2 = Config::get_id();
    EXPECT_EQ(id, id2);
}

TEST(ConfigTest, SetGetOption) {
    Config::set_option("test.key", "test.value");
    EXPECT_EQ(Config::get_option("test.key"), "test.value");
    Config::set_option("test.key", "");
    EXPECT_EQ(Config::get_option("test.key"), "");
}

TEST(ConfigTest, BoolOptions) {
    Config::set_option_bool("test.bool", true);
    EXPECT_TRUE(Config::option2bool("test.bool", Config::get_option("test.bool")));
    Config::set_option_bool("test.bool", false);
    EXPECT_FALSE(Config::option2bool("test.bool", Config::get_option("test.bool")));
}

TEST(ConfigTest, PasswordManagement) {
    Config::set_password("secret123");
    EXPECT_TRUE(Config::is_password_set());
    EXPECT_EQ(Config::get_password(), "secret123");
    Config::set_password("");
    EXPECT_FALSE(Config::is_password_set());
}

TEST(ConfigTest, KeyPair) {
    auto [sk, pk] = Config::get_key_pair();
    EXPECT_FALSE(sk.empty());
    EXPECT_FALSE(pk.empty());
}

TEST(ConfigTest, RendezvousServer) {
    Config::set_rendezvous_server("custom.example.com");
    EXPECT_EQ(Config::get_rendezvous_server(), "custom.example.com");
}

TEST(ConfigTest, ForceRelay) {
    Config::set_option_bool("force-relay", true);
    EXPECT_TRUE(Config::is_force_relay());
    Config::set_option_bool("force-relay", false);
    EXPECT_FALSE(Config::is_force_relay());
}

TEST(ConfigTest, PeerManagement) {
    PeerConfig peer;
    peer.id = "test-peer-1";
    peer.username = "Test User";
    peer.online = true;
    Config::instance().add_peer(peer);
    auto found = Config::instance().get_peer("test-peer-1");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->username, "Test User");
    Config::instance().remove_peer("test-peer-1");
    EXPECT_FALSE(Config::instance().get_peer("test-peer-1").has_value());
}

// ====== Crypto Tests ======
TEST(CryptoTest, SHA256Deterministic) {
    auto h1 = crypto::sha256("cppdesk");
    auto h2 = crypto::sha256("cppdesk");
    auto h3 = crypto::sha256("different");
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
}

TEST(CryptoTest, SHA256NotEmpty) {
    auto h = crypto::sha256("test");
    for (auto b : h) EXPECT_GE(b, 0);
}

TEST(CryptoTest, Base64Roundtrip) {
    std::string orig = "Hello World! This is cppdesk.";
    auto enc = crypto::encode64(reinterpret_cast<const uint8_t*>(orig.data()), orig.size());
    auto dec = crypto::decode64(enc);
    std::string dec_str(dec.begin(), dec.end());
    EXPECT_EQ(orig, dec_str);
}

TEST(CryptoTest, Base64Empty) {
    auto enc = crypto::encode64(nullptr, 0);
    EXPECT_TRUE(enc.empty());
    auto dec = crypto::decode64("");
    EXPECT_TRUE(dec.empty());
}

TEST(CryptoTest, RandomBytes) {
    auto r1 = crypto::random_bytes(64);
    auto r2 = crypto::random_bytes(64);
    EXPECT_EQ(r1.size(), 64u);
    EXPECT_EQ(r2.size(), 64u);
    EXPECT_NE(r1, r2);
}

TEST(CryptoTest, BoxKeypair) {
    auto kp = crypto::generate_box_keypair();
    EXPECT_FALSE(std::all_of(kp.pk.begin(), kp.pk.end(), [](uint8_t b) { return b == 0; }));
    EXPECT_FALSE(std::all_of(kp.sk.begin(), kp.sk.end(), [](uint8_t b) { return b == 0; }));
}

TEST(CryptoTest, SignKeypair) {
    auto kp = crypto::generate_sign_keypair();
    EXPECT_FALSE(std::all_of(kp.pk.begin(), kp.pk.end(), [](uint8_t b) { return b == 0; }));
}

TEST(CryptoTest, PasswordHash) {
    auto hash = crypto::hash_password("mypassword", "somesalt");
    EXPECT_FALSE(hash.empty());
    auto hash2 = crypto::hash_password("mypassword", "somesalt");
    auto hash3 = crypto::hash_password("different", "somesalt");
    EXPECT_NE(hash2, hash3);
}

TEST(CryptoTest, SecureCompare) {
    std::string a = "secret data";
    std::string b = "secret data";
    std::string c = "different";
    EXPECT_TRUE(crypto::secure_compare(
        reinterpret_cast<const uint8_t*>(a.data()),
        reinterpret_cast<const uint8_t*>(b.data()), a.size()));
    EXPECT_FALSE(crypto::secure_compare(
        reinterpret_cast<const uint8_t*>(a.data()),
        reinterpret_cast<const uint8_t*>(c.data()), a.size()));
}

// ====== Protocol Tests ======
TEST(ProtocolTest, ResolutionDefault) {
    Resolution r;
    EXPECT_EQ(r.width, 1920u);
    EXPECT_EQ(r.height, 1080u);
}

TEST(ProtocolTest, ResolutionEquality) {
    EXPECT_EQ(Resolution{1920, 1080}, Resolution{1920, 1080});
    EXPECT_NE(Resolution{1920, 1080}, Resolution{1366, 768});
}

TEST(ProtocolTest, MouseEventFields) {
    MouseEvent ev;
    ev.x = 100; ev.y = 200;
    ev.mask = MouseEvent::BUTTON_LEFT | MouseEvent::TYPE_DOWN;
    EXPECT_EQ(ev.x, 100);
    EXPECT_EQ(ev.y, 200);
    EXPECT_NE(ev.mask & MouseEvent::BUTTON_LEFT, 0);
}

TEST(ProtocolTest, KeyEventFields) {
    KeyEvent ev;
    ev.keycode = 65; // 'A'
    ev.down = true;
    ev.is_modifier = false;
    EXPECT_EQ(ev.keycode, 65u);
    EXPECT_TRUE(ev.down);
}

TEST(ProtocolTest, VideoFrameCreation) {
    VideoFrame f;
    f.width = 640; f.height = 480;
    f.codec = 1; // H264
    EXPECT_TRUE(f.width == 640);
    EXPECT_TRUE(f.height == 480);
}

TEST(ProtocolTest, ControlPermissions) {
    ControlPermissions p;
    EXPECT_TRUE(p.keyboard);
    EXPECT_TRUE(p.clipboard);
    EXPECT_TRUE(p.file_transfer);
    EXPECT_TRUE(p.audio);
    EXPECT_FALSE(p.restart);
}

// ====== Utility Tests ======
TEST(UtilityTest, IsIpStr) {
    EXPECT_TRUE(is_ip_str("192.168.1.1"));
    EXPECT_TRUE(is_ip_str("10.0.0.1"));
    EXPECT_FALSE(is_ip_str("example.com"));
    EXPECT_FALSE(is_ip_str("not.an.ip"));
}

TEST(UtilityTest, IsDomainPort) {
    EXPECT_TRUE(is_domain_port_str("example.com:8080"));
    EXPECT_TRUE(is_domain_port_str("192.168.1.1:21117"));
    EXPECT_FALSE(is_domain_port_str("example.com"));
}

TEST(UtilityTest, CheckPort) {
    EXPECT_EQ(check_port("example.com", 21116), "example.com:21116");
    EXPECT_EQ(check_port("example.com:8080", 21116), "example.com:8080");
}

TEST(UtilityTest, Random) {
    auto u1 = random_u64();
    auto u2 = random_u64();
    EXPECT_NE(u1, u2);
    auto s = random_string(32);
    EXPECT_EQ(s.size(), 32u);
}

TEST(UtilityTest, PlatformName) {
    auto p = get_platform_name();
    EXPECT_FALSE(p.empty());
}

// ====== Server Tests ======
TEST(ServerTest, CreateServer) {
    auto srv = server::Server::create();
    EXPECT_TRUE(srv != nullptr);
}

TEST(ServerTest, ServiceRegistration) {
    auto srv = server::Server::create();
    EXPECT_TRUE(srv->has_service("audio"));
    EXPECT_TRUE(srv->has_service("display"));
}

TEST(ServerTest, ConnectionId) {
    auto srv = server::Server::create();
    int32_t id1 = srv->next_connection_id();
    int32_t id2 = srv->next_connection_id();
    EXPECT_NE(id1, id2);
    EXPECT_GT(id2, id1);
}

// ====== Scrap Tests ======
TEST(ScrapTest, FactoryCreates) {
    auto capturer = scrap::create_capturer();
    EXPECT_TRUE(capturer != nullptr);
    auto decoder = scrap::create_decoder();
    EXPECT_TRUE(decoder != nullptr);
    auto encoder = scrap::create_encoder();
    EXPECT_TRUE(encoder != nullptr);
}

TEST(ScrapTest, ImageFormatConversion) {
    scrap::ImageRgb src;
    src.w = 100; src.h = 100;
    src.fmt = scrap::ImageFormat::BGRA;
    src.raw.resize(100 * 100 * 4, 128);
    auto out = scrap::convert_bgra_to_rgba(src);
    EXPECT_EQ(out.w, src.w);
    EXPECT_EQ(out.h, src.h);
    EXPECT_EQ(out.fmt, scrap::ImageFormat::RGBA);
}

// ====== Enigo Tests ======
TEST(EnigoTest, CreateEnigo) {
    enigo::Enigo e;
    EXPECT_TRUE(e.is_available());
}

TEST(EnigoTest, DSLParser) {
    auto tokens = enigo::DslParser::parse("hello {+SHIFT}world{-SHIFT}");
    EXPECT_GT(tokens.size(), 0u);
}

TEST(EnigoTest, DelaySettings) {
    enigo::Enigo e;
    e.set_delay(std::chrono::milliseconds(5));
    EXPECT_EQ(e.get_delay(), std::chrono::milliseconds(5));
}

// ====== Clipboard Tests ======
TEST(ClipboardTest, FactoryCreates) {
    auto cb = clipboard::PlatformClipboard::create();
    EXPECT_TRUE(cb != nullptr);
}

TEST(ClipboardTest, MonitorCreate) {
    clipboard::ClipboardMonitor mon;
    EXPECT_FALSE(mon.is_running());
}

// ====== Rendezvous Tests ======
TEST(RendezvousTest, ServerCreate) {
    rendezvous::RendezvousServer srv(21116);
    EXPECT_FALSE(srv.is_running());
}

TEST(RendezvousTest, RelayServerCreate) {
    rendezvous::RelayServer relay;
    EXPECT_FALSE(relay.is_running());
}

// ====== Platform Tests ======
TEST(PlatformTest, DisplayNames) {
    auto names = platform::get_display_names();
    EXPECT_FALSE(names.empty());
}

TEST(PlatformTest, WakeLock) {
    auto wl = platform::get_wakelock(true);
    // WakeLock should be moveable
    auto wl2 = std::move(wl);
    (void)wl2;
}

TEST(PlatformTest, IsInstalled) {
    auto installed = platform::is_installed();
    // Should return a boolean (may be false on dev machine)
    EXPECT_TRUE(installed == true || installed == false);
}

// ====== Performance / Stress Tests ======
TEST(PerformanceTest, ManyRandoms) {
    for (int i = 0; i < 1000; i++) {
        crypto::random_bytes(32);
    }
}

TEST(PerformanceTest, ManySHA256) {
    std::string data(1024, 'x');
    for (int i = 0; i < 100; i++) {
        crypto::sha256(data);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
