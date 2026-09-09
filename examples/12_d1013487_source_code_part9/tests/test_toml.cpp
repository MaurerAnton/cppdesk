// test_toml.cpp — CTest parity for toml.hpp round-trip (not upstream, but guards
// the parser/emitter that config.rs uses via confy).

#include <hbb_common/toml.hpp>

#include <cstdio>

using hbb_common::toml::Document;
using hbb_common::toml::parse;
using hbb_common::toml::emit;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

static void test_basic_parse_emit() {
    const char* src = R"TOML(
id = "test-id"
password = "secret"
key_confirmed = true
options = [ ["a", "1"], ["b", "2"] ]
)TOML";
    const auto doc = parse(src);
    auto it = doc.root.find("id");
    CHECK(it != doc.root.end() && it->second.is_string());
    CHECK(std::get<std::string>(it->second.data) == "test-id");
    it = doc.root.find("key_confirmed");
    CHECK(it != doc.root.end() && it->second.is_bool());
    CHECK(std::get<bool>(it->second.data) == true);
    it = doc.root.find("options");
    CHECK(it != doc.root.end() && it->second.is_array());
    const auto& arr = std::get<hbb_common::toml::Value::Array>(it->second.data);
    CHECK(arr.size() == 2);
    // Round-trip
    const std::string out = emit(doc);
    CHECK(!out.empty());
}

static void test_table_section() {
    const char* src = R"TOML(
id = "root"
[keys_confirmed]
host1 = true
host2 = false
)TOML";
    const auto doc = parse(src);
    CHECK(doc.root.find("id") != doc.root.end());
    auto it = doc.tables.find("keys_confirmed");
    CHECK(it != doc.tables.end());
    CHECK(it->second.size() == 2);
    CHECK(it->second.find("host1") != it->second.end());
    CHECK(it->second.find("host2") != it->second.end());
}

static void test_integer_and_array() {
    const char* src = R"TOML(
size = [ 10, 20, 800, 600 ]
serial = 42
)TOML";
    const auto doc = parse(src);
    auto it = doc.root.find("size");
    CHECK(it != doc.root.end() && it->second.is_array());
    const auto& arr = std::get<hbb_common::toml::Value::Array>(it->second.data);
    CHECK(arr.size() == 4);
    it = doc.root.find("serial");
    CHECK(it != doc.root.end() && it->second.is_int());
    CHECK(std::get<int64_t>(it->second.data) == 42);
}

int main() {
    test_basic_parse_emit();
    test_table_section();
    test_integer_and_array();
    if (g_failures == 0) {
        std::puts("test_toml: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}