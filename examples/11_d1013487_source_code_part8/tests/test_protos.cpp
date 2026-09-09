// test_protos.cpp — round-trip tests for the protoc-generated structs
// (message.proto + rendezvous.proto). Upstream has no proto unit tests; these
// lock the wire contract later steps rely on (field numbers, zigzag sint32,
// oneof union cases, renamed enum symbols).

#include <cstdio>
#include <string>

#include "message.pb.h"
#include "rendezvous.pb.h"

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

// FileEntry: all five fields incl. uint64 size/mtime.
static void test_file_entry() {
    hbb::FileEntry e;
    e.set_entry_type(hbb::File);
    e.set_name("photo.jpg");
    e.set_is_hidden(false);
    e.set_size(1234567890123ULL);
    e.set_modified_time(1616975954ULL);
    std::string bytes;
    CHECK(e.SerializeToString(&bytes));
    hbb::FileEntry back;
    CHECK(back.ParseFromString(bytes));
    CHECK(back.entry_type() == hbb::File);
    CHECK(back.name() == "photo.jpg");
    CHECK(back.is_hidden() == false);
    CHECK(back.size() == 1234567890123ULL);
    CHECK(back.modified_time() == 1616975954ULL);
}

// FileTransferBlock: sint32 file_num zigzag (negative value must survive).
static void test_block_zigzag() {
    hbb::FileTransferBlock b;
    b.set_id(7);
    b.set_file_num(-1);
    b.set_data("payload");
    b.set_compressed(true);
    std::string bytes;
    CHECK(b.SerializeToString(&bytes));
    hbb::FileTransferBlock back;
    CHECK(back.ParseFromString(bytes));
    CHECK(back.id() == 7);
    CHECK(back.file_num() == -1);
    CHECK(back.data() == "payload");
    CHECK(back.compressed() == true);
}

// Message envelope: file_response oneof + error sub-message.
static void test_message_envelope() {
    hbb::Message msg;
    auto* resp = msg.mutable_file_response();
    auto* err = resp->mutable_error();
    err->set_id(3);
    err->set_error("Not exists");
    err->set_file_num(0);
    CHECK(msg.has_file_response());
    CHECK(!msg.has_file_action());
    std::string bytes;
    CHECK(msg.SerializeToString(&bytes));
    hbb::Message back;
    CHECK(back.ParseFromString(bytes));
    CHECK(back.has_file_response());
    CHECK(back.file_response().has_error());
    CHECK(back.file_response().error().id() == 3);
    CHECK(back.file_response().error().error() == "Not exists");
}

// Renamed enum symbols (C++ scoping fixes; wire numbers unchanged).
static void test_renamed_enums() {
    hbb::PermissionInfo pi;
    pi.set_permission(hbb::PermissionClipboard);
    pi.set_enabled(true);
    CHECK(pi.permission() == hbb::PermissionClipboard);
    hbb::OptionMessage opt;
    opt.set_image_quality(hbb::Low);
    opt.set_lock_after_session_end(hbb::Yes);
    CHECK(opt.image_quality() == hbb::Low);
    CHECK(opt.lock_after_session_end() == hbb::Yes);
}

// RendezvousMessage: register_peer union case.
static void test_rendezvous() {
    hbb::RendezvousMessage msg;
    auto* rp = msg.mutable_register_peer();
    rp->set_id("123456");
    rp->set_serial(9);
    CHECK(msg.has_register_peer());
    std::string bytes;
    CHECK(msg.SerializeToString(&bytes));
    hbb::RendezvousMessage back;
    CHECK(back.ParseFromString(bytes));
    CHECK(back.has_register_peer());
    CHECK(back.register_peer().id() == "123456");
    CHECK(back.register_peer().serial() == 9);
}

int main() {
    test_file_entry();
    test_block_zigzag();
    test_message_envelope();
    test_renamed_enums();
    test_rendezvous();
    if (g_failures == 0) {
        std::puts("test_protos: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
