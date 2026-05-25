#include <gtest/gtest.h>
#include "common/config.hpp"
#include "common/crypto.hpp"
#include "common/protocol.hpp"

using namespace cppdesk::common;

TEST(ConfigTest, Singleton) {
    auto& c1 = Config::instance();
    auto& c2 = Config::instance();
    EXPECT_EQ(&c1, &c2);
}

TEST(ConfigTest, SetGetOption) {
    Config::set_option("test_key", "test_value");
    EXPECT_EQ(Config::get_option("test_key"), "test_value");
}

TEST(ConfigTest, PasswordManagement) {
    Config::set_password("secret123");
    EXPECT_TRUE(Config::is_password_set());
    EXPECT_EQ(Config::get_password(), "secret123");
    Config::set_password("");
    EXPECT_FALSE(Config::is_password_set());
}

TEST(ConfigTest, IDGeneration) {
    std::string id1 = Config::get_id();
    std::string id2 = Config::get_id();
    EXPECT_EQ(id1, id2);
    EXPECT_FALSE(id1.empty());
}

TEST(CryptoTest, SHA256) {
    auto hash1 = crypto::sha256("hello");
    auto hash2 = crypto::sha256("hello");
    auto hash3 = crypto::sha256("world");
    EXPECT_EQ(hash1, hash2);
    EXPECT_NE(hash1, hash3);
}

TEST(CryptoTest, Base64Roundtrip) {
    std::string original = "Hello, cppdesk!";
    auto encoded = crypto::encode64(
        reinterpret_cast<const uint8_t*>(original.data()), original.size());
    auto decoded = crypto::decode64(encoded);
    std::string dec_str(decoded.begin(), decoded.end());
    EXPECT_EQ(original, dec_str);
}

TEST(CryptoTest, RandomBytes) {
    auto r1 = crypto::random_bytes(32);
    auto r2 = crypto::random_bytes(32);
    EXPECT_EQ(r1.size(), 32u);
    EXPECT_EQ(r2.size(), 32u);
    EXPECT_NE(r1, r2);
}

TEST(ProtocolTest, ResolutionEquality) {
    Resolution r1{1920, 1080};
    Resolution r2{1920, 1080};
    Resolution r3{1366, 768};
    EXPECT_EQ(r1, r2);
    EXPECT_NE(r1, r3);
}

TEST(ProtocolTest, MouseEventConstants) {
    EXPECT_EQ(MouseEvent::TYPE_MOVE, 0);
    EXPECT_EQ(MouseEvent::TYPE_DOWN, 1);
    EXPECT_EQ(MouseEvent::TYPE_UP, 2);
    EXPECT_EQ(MouseEvent::BUTTON_LEFT, 1);
    EXPECT_EQ(MouseEvent::BUTTON_RIGHT, 2);
}

TEST(UtilityTest, IsIP) {
    EXPECT_TRUE(is_ip_str("192.168.1.1"));
    EXPECT_FALSE(is_ip_str("example.com"));
    EXPECT_FALSE(is_ip_str("not-an-ip"));
}

TEST(UtilityTest, CheckPort) {
    EXPECT_EQ(check_port("example.com", 21116), "example.com:21116");
    EXPECT_EQ(check_port("example.com:8080", 21116), "example.com:8080");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
