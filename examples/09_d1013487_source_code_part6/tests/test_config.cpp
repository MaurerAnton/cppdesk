// test_config.cpp — CTest parity for config.rs serialization (upstream
// test_serialize for Config and PeerConfig).

#include <hbb_common/config.hpp>
#include <hbb_common/toml.hpp>

#include <cstdio>

using hbb_common::Config;
using hbb_common::Config2;
using hbb_common::PeerConfig;
using hbb_common::PeerInfoSerde;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

static void test_config_roundtrip() {
    Config c;
    c.id = "test-id";
    c.password = "secret";
    c.salt = "salt123";
    c.key_pair[0] = {1, 2, 3};
    c.key_pair[1] = {4, 5, 6};
    c.key_confirmed = true;
    c.keys_confirmed["host1"] = true;
    c.keys_confirmed["host2"] = false;

    auto t = hbb_common::Config_to_toml(c);
    auto back = hbb_common::Config_from_toml(t);
    CHECK(back.id == "test-id");
    CHECK(back.password == "secret");
    CHECK(back.salt == "salt123");
    CHECK((back.key_pair[0] == std::vector<uint8_t>{1, 2, 3}));
    CHECK((back.key_pair[1] == std::vector<uint8_t>{4, 5, 6}));
    CHECK(back.key_confirmed == true);
    CHECK(back.keys_confirmed.size() == 2);
    CHECK(back.keys_confirmed["host1"] == true);
    CHECK(back.keys_confirmed["host2"] == false);
}

static void test_config2_roundtrip() {
    Config2 c;
    c.remote_id = "remote-123";
    c.size = {10, 20, 800, 600};
    c.rendezvous_server = "example.com:21116";
    c.nat_type = 2;
    c.serial = 5;
    c.options["custom-rendezvous-server"] = "custom.example.com";

    auto t = hbb_common::Config2_to_toml(c);
    auto back = hbb_common::Config2_from_toml(t);
    CHECK(back.remote_id == "remote-123");
    CHECK(back.size[0] == 10 && back.size[1] == 20 && back.size[2] == 800 && back.size[3] == 600);
    CHECK(back.rendezvous_server == "example.com:21116");
    CHECK(back.nat_type == 2);
    CHECK(back.serial == 5);
    CHECK(back.options.size() == 1);
    CHECK(back.options["custom-rendezvous-server"] == "custom.example.com");
}

static void test_peer_config_roundtrip() {
    PeerConfig c;
    c.password = {9, 8, 7};
    c.size = {0, 0, 1024, 768};
    c.size_ft = {0, 0, 512, 512};
    c.size_pf = {0, 0, 256, 256};
    c.view_style = "scale";
    c.image_quality = "high";
    c.custom_image_quality = {80, 90};
    c.show_remote_cursor = true;
    c.lock_after_session_end = false;
    c.privacy_mode = true;
    c.port_forwards.emplace_back(8080, "localhost", 80);
    c.direct_failures = 3;
    c.disable_audio = true;
    c.disable_clipboard = false;
    c.options["custom"] = "value";
    c.info.username = "user";
    c.info.hostname = "host";
    c.info.platform = "linux";

    auto t = hbb_common::PeerConfig_to_toml(c);
    auto back = hbb_common::PeerConfig_from_toml(t);
    CHECK((back.password == std::vector<uint8_t>{9, 8, 7}));
    CHECK(back.size[0] == 0 && back.size[1] == 0 && back.size[2] == 1024 && back.size[3] == 768);
    CHECK(back.view_style == "scale");
    CHECK(back.image_quality == "high");
    CHECK(back.custom_image_quality.size() == 2);
    CHECK(back.custom_image_quality[0] == 80);
    CHECK(back.show_remote_cursor == true);
    CHECK(back.lock_after_session_end == false);
    CHECK(back.privacy_mode == true);
    CHECK(back.port_forwards.size() == 1);
    CHECK(std::get<0>(back.port_forwards[0]) == 8080);
    CHECK(std::get<1>(back.port_forwards[0]) == "localhost");
    CHECK(std::get<2>(back.port_forwards[0]) == 80);
    CHECK(back.direct_failures == 3);
    CHECK(back.disable_audio == true);
    CHECK(back.disable_clipboard == false);
    CHECK(back.options.size() == 1);
    CHECK(back.options["custom"] == "value");
    CHECK(back.info.username == "user");
    CHECK(back.info.hostname == "host");
    CHECK(back.info.platform == "linux");
}

int main() {
    test_config_roundtrip();
    test_config2_roundtrip();
    test_peer_config_roundtrip();
    if (g_failures == 0) {
        std::puts("test_config: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}