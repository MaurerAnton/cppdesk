// test_fs.cpp — tests for hbb_common::fs (read_dir, recursive enumeration,
// TransferJob write→read block round-trip, message builders, job list).
// Hermetic: everything runs under a per-test temp dir. Upstream has no fs
// tests; these lock the contracts later steps (server file service) rely on.

#include <hbb_common/fs.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

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

// Fresh temp dir per test (removed at the end).
fs::path make_temp(const char* name) {
    fs::path dir = fs::temp_directory_path() / ("cppdesk_test_fs_" + std::string(name));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
}

void test_read_dir() {
    const fs::path dir = make_temp("ls");
    write_file(dir / "a.txt", "hello");
    write_file(dir / ".hidden", "shh");
    fs::create_directories(dir / "sub");

    // Hidden excluded by default.
    hbb::FileDirectory fd = hbb_common::read_dir(dir.string(), false);
    CHECK(fd.path() == dir.string());
    bool saw_file = false, saw_dir = false, saw_hidden = false;
    for (const auto& e : fd.entries()) {
        if (e.name() == "a.txt") {
            saw_file = true;
            CHECK(e.entry_type() == hbb::File);
            CHECK(e.size() == 5);
            CHECK(!e.is_hidden());
        } else if (e.name() == "sub") {
            saw_dir = true;
            CHECK(e.entry_type() == hbb::Dir);
            CHECK(e.size() == 0);
        } else if (e.name() == ".hidden") {
            saw_hidden = true;
        }
    }
    CHECK(saw_file);
    CHECK(saw_dir);
    CHECK(!saw_hidden);

    // Included on request.
    const hbb::FileDirectory fd_all = hbb_common::read_dir(dir.string(), true);
    bool saw_hidden2 = false;
    for (const auto& e : fd_all.entries()) {
        if (e.name() == ".hidden") {
            saw_hidden2 = true;
            CHECK(e.is_hidden());
        }
    }
    CHECK(saw_hidden2);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_recursive_files() {
    const fs::path dir = make_temp("recur");
    write_file(dir / "top.bin", std::string(100, 'x'));
    write_file(dir / "sub" / "nested.txt", "nested");
    write_file(dir / "sub" / ".h", "hidden");

    const auto files = hbb_common::get_recursive_files(dir.string(), false);
    CHECK(files.size() == 2);
    bool saw_top = false, saw_nested = false;
    for (const auto& f : files) {
        CHECK(f.entry_type() == hbb::File);  // dirs never listed
        if (f.name() == "top.bin") {
            saw_top = true;
            CHECK(f.size() == 100);
        } else if (f.name() == (fs::path("sub") / "nested.txt").string()) {
            saw_nested = true;  // prefix-joined name parity
        }
    }
    CHECK(saw_top);
    CHECK(saw_nested);

    bool threw = false;
    try {
        hbb_common::get_recursive_files((dir / "missing").string(), false);
    } catch (const std::runtime_error&) {
        threw = true;  // bail!("Not exists") parity
    }
    CHECK(threw);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void test_transfer_roundtrip() {
    const fs::path src = make_temp("src");
    const fs::path dst = make_temp("dst");
    // One compressible file (exercises the zstd path) + one tiny file.
    std::string big;
    for (int i = 0; i < 5000; ++i) {
        big += "rustdesk file transfer payload line\n";
    }
    write_file(src / "big.txt", big);
    write_file(src / "tiny.txt", "tiny");

    // Reader side: enumerate + emit blocks.
    hbb_common::TransferJob reader =
        hbb_common::TransferJob::new_read(42, src.string(), false);
    CHECK(reader.id() == 42);
    CHECK(reader.files().size() == 2);
    CHECK(reader.total_size() == big.size() + 4);

    // Writer side: same file list, different root (names are relative).
    hbb_common::TransferJob writer =
        hbb_common::TransferJob::new_write(42, dst.string(), reader.files());

    int blocks = 0;
    while (auto block = reader.read()) {
        CHECK(block->id() == 42);
        writer.write(*block);
        ++blocks;
        if (blocks > 100) {
            break;  // safety; must finish long before
        }
    }
    CHECK(blocks >= 2);  // at least one block per file
    CHECK(reader.finished_size() == big.size() + 4);
    CHECK(writer.finished_size() == big.size() + 4);

    // Content identical (writer renamed .download into place? No — rename
    // happens on file SWITCH/finish via modify_time; force it for the last).
    writer.modify_time();
    std::ifstream a(src / "big.txt", std::ios::binary);
    std::ifstream b(dst / "big.txt", std::ios::binary);
    CHECK(a.good() && b.good());
    CHECK(std::string(std::istreambuf_iterator<char>(a), {}) ==
          std::string(std::istreambuf_iterator<char>(b), {}));
    std::ifstream c(src / "tiny.txt", std::ios::binary);
    std::ifstream d(dst / "tiny.txt", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(c), {}) ==
          std::string(std::istreambuf_iterator<char>(d), {}));

    // remove_download_file on a finished job is a harmless no-op.
    writer.remove_download_file();

    std::error_code ec;
    fs::remove_all(src, ec);
    fs::remove_all(dst, ec);
}

void test_builders_and_jobs() {
    hbb::FileEntry e;
    e.set_name("f");
    e.set_entry_type(hbb::File);

    const hbb::Message dir = hbb_common::new_dir(1, {e});
    CHECK(dir.has_file_response() && dir.file_response().has_dir());
    CHECK(dir.file_response().dir().id() == 1);
    CHECK(dir.file_response().dir().entries_size() == 1);

    hbb::FileTransferBlock block;
    block.set_id(2);
    const hbb::Message blk = hbb_common::new_block(block);
    CHECK(blk.file_response().has_block());

    const hbb::Message err = hbb_common::new_error(3, "boom", 0);
    CHECK(err.file_response().has_error());
    CHECK(err.file_response().error().error() == "boom");

    const hbb::Message recv = hbb_common::new_receive(4, "/tmp/x", {e});
    CHECK(recv.has_file_action() && recv.file_action().has_receive());
    CHECK(recv.file_action().receive().path() == "/tmp/x");

    const hbb::Message snd = hbb_common::new_send(5, "/tmp/y", true);
    CHECK(snd.file_action().has_send());
    CHECK(snd.file_action().send().include_hidden());

    const hbb::Message done = hbb_common::new_done(6, 1);
    CHECK(done.file_response().has_done());
    CHECK(done.file_response().done().file_num() == 1);

    std::vector<hbb_common::TransferJob> jobs;
    jobs.push_back(hbb_common::TransferJob::new_write(1, "/tmp/a", {}));
    jobs.push_back(hbb_common::TransferJob::new_write(2, "/tmp/b", {}));
    CHECK(hbb_common::get_job(1, jobs) != nullptr);
    CHECK(hbb_common::get_job(1, jobs)->id() == 1);
    CHECK(hbb_common::get_job(9, jobs) == nullptr);
    hbb_common::remove_job(1, jobs);
    CHECK(jobs.size() == 1);
    CHECK(hbb_common::get_job(1, jobs) == nullptr);

    hbb_common::create_dir((fs::temp_directory_path() / "cppdesk_test_fs_mk").string());
    CHECK(fs::is_directory(fs::temp_directory_path() / "cppdesk_test_fs_mk"));
    hbb_common::remove_all_empty_dir(
        (fs::temp_directory_path() / "cppdesk_test_fs_mk").string());
    CHECK(!fs::exists(fs::temp_directory_path() / "cppdesk_test_fs_mk"));
    bool threw = false;
    try {
        hbb_common::remove_file(
            (fs::temp_directory_path() / "cppdesk_test_fs_nope").string());
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "cppdesk_test_fs_mk", ec);
}

}  // namespace

int main() {
    test_read_dir();
    test_recursive_files();
    test_transfer_roundtrip();
    test_builders_and_jobs();
    if (g_failures == 0) {
        std::puts("test_fs: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
