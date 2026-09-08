// test_common.cpp — tests for the pure helpers in rustdesk::common
// (valid_for_numlock/capslock, is_control_key/is_modifier, get_time,
// check_port, get_version_number, test_if_valid_server).
// Network/config-file paths are NOT tested here (hermetic unit tests only).

#include <rustdesk/common.hpp>

#include <cstdio>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

hbb::KeyEvent control_key_event(hbb::ControlKey key) {
    hbb::KeyEvent e;
    e.set_control_key(key);
    return e;
}

hbb::KeyEvent chr_event(uint32_t chr) {
    hbb::KeyEvent e;
    e.set_chr(chr);
    return e;
}

void test_numlock() {
    CHECK(rustdesk::valid_for_numlock(control_key_event(hbb::Numpad0)));
    CHECK(rustdesk::valid_for_numlock(control_key_event(hbb::Numpad5)));
    CHECK(rustdesk::valid_for_numlock(control_key_event(hbb::Numpad9)));
    CHECK(rustdesk::valid_for_numlock(control_key_event(hbb::Decimal)));
    CHECK(!rustdesk::valid_for_numlock(control_key_event(hbb::F1)));
    CHECK(!rustdesk::valid_for_numlock(control_key_event(hbb::Alt)));
    CHECK(!rustdesk::valid_for_numlock(chr_event('5')));  // chr arm, not control_key
    CHECK(!rustdesk::valid_for_numlock(hbb::KeyEvent()));  // empty union
}

void test_capslock() {
    CHECK(rustdesk::valid_for_capslock(chr_event('a')));
    CHECK(rustdesk::valid_for_capslock(chr_event('z')));
    CHECK(!rustdesk::valid_for_capslock(chr_event('A')));  // uppercase excluded
    CHECK(!rustdesk::valid_for_capslock(chr_event('0')));
    CHECK(!rustdesk::valid_for_capslock(control_key_event(hbb::Shift)));
    CHECK(!rustdesk::valid_for_capslock(hbb::KeyEvent()));
}

void test_control_and_modifier() {
    CHECK(rustdesk::is_control_key(control_key_event(hbb::Alt), hbb::Alt));
    CHECK(!rustdesk::is_control_key(control_key_event(hbb::Alt), hbb::Shift));
    CHECK(!rustdesk::is_control_key(chr_event('a'), hbb::Alt));
    CHECK(rustdesk::is_modifier(control_key_event(hbb::Alt)));
    CHECK(rustdesk::is_modifier(control_key_event(hbb::Shift)));
    CHECK(rustdesk::is_modifier(control_key_event(hbb::Control)));
    CHECK(rustdesk::is_modifier(control_key_event(hbb::Meta)));
    CHECK(!rustdesk::is_modifier(control_key_event(hbb::F1)));
    CHECK(!rustdesk::is_modifier(control_key_event(hbb::Numpad0)));
    CHECK(!rustdesk::is_modifier(chr_event('a')));
}

void test_time_and_port() {
    const int64_t t1 = rustdesk::get_time();
    CHECK(t1 > 0);
    CHECK(rustdesk::get_time() >= t1);  // monotonic non-decreasing
    CHECK(rustdesk::check_port("example.com", 21116) == "example.com:21116");
    CHECK(rustdesk::check_port("example.com:21117", 21116) == "example.com:21117");
}

void test_version_number() {
    CHECK(rustdesk::get_version_number("1.1.2") == 1001002);
    CHECK(rustdesk::get_version_number("1.1.9") > rustdesk::get_version_number("1.1.2"));
    CHECK(rustdesk::get_version_number("abc") == 0);
    CHECK(rustdesk::get_version_number("1.1") == 1001);
}

void test_valid_server() {
    // 127.0.0.1 resolves without any network access (no connect attempted).
    CHECK(rustdesk::test_if_valid_server("127.0.0.1:21116") == "");
    CHECK(rustdesk::test_if_valid_server("127.0.0.1") == "");  // ":0" appended
    // .invalid never resolves (RFC 2606) — error string is non-empty.
    CHECK(!rustdesk::test_if_valid_server("no-such-host.invalid:1").empty());
}

}  // namespace

int main() {
    test_numlock();
    test_capslock();
    test_control_and_modifier();
    test_time_and_port();
    test_version_number();
    test_valid_server();
    if (g_failures == 0) {
        std::puts("test_common: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
