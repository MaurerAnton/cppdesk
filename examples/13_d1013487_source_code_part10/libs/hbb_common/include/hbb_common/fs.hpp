// fs.hpp — translation of libs/hbb_common/src/fs.rs (file-transfer helpers).
//
// Async mapping: `tokio::fs::File` + `async fn read/write` become synchronous
// `std::fstream` + sync methods. The bodies are strictly sequential file IO
// (no select!/spawn), so behavior is identical; only the executor coupling is
// gone. `handle_read_jobs` drives jobs through `TcpFramedStream::send`, which
// works with the generated messages since step 08's SerializeAsString fix.
//
// Error mapping: `anyhow::Result` → return-or-throw (`bail!` →
// `std::runtime_error`, IO failures → `std::system_error`).
// `filetime::set_file_mtime` → POSIX `utimensat` (mtime only, atime kept);
// the Windows branch is deferred to the platform step.

#pragma once

#include <hbb_common/tcp.hpp>

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

// Generated protobuf (message.proto): FileDirectory/FileEntry/FileType,
// FileTransferBlock, FileResponse/FileAction builders, Message envelope.
#include "message.pb.h"

namespace hbb_common {

// Directory listing with entry metadata (read_dir parity).
hbb::FileDirectory read_dir(const std::string& path, bool include_hidden);

// Path helpers (all #[inline] upstream).
std::string get_file_name(const std::string& path);
std::string path_to_string(const std::string& path);  // get_string parity
std::string make_path(const std::string& path);       // get_path parity
std::string get_home_as_string();

// Recursive file enumeration, names prefixed with `prefix` (upstream joins
// `prefix`/`entry.name`; the top call passes an empty prefix).
std::vector<hbb::FileEntry> get_recursive_files(const std::string& path, bool include_hidden);

// One side of a file transfer (TransferJob parity). Movable, not copyable
// (owns the open file handle, like Option<File>).
class TransferJob {
public:
    static TransferJob new_write(int32_t id, const std::string& path,
                                 std::vector<hbb::FileEntry> files);
    // Throws on enumeration failure (get_recursive_files parity).
    static TransferJob new_read(int32_t id, const std::string& path, bool include_hidden);

    TransferJob(const TransferJob&) = delete;
    TransferJob& operator=(const TransferJob&) = delete;
    TransferJob(TransferJob&&) = default;
    TransferJob& operator=(TransferJob&&) = default;

    const std::vector<hbb::FileEntry>& files() const;
    void set_files(std::vector<hbb::FileEntry> files);
    int32_t id() const;
    uint64_t total_size() const;
    uint64_t finished_size() const;
    uint64_t transfered() const;
    int32_t file_num() const;

    // Rename current ".download" into place + restore mtime (errors ignored).
    void modify_time() const;
    void remove_download_file() const;

    // Write one received block (decompressing when flagged). Throws on
    // id/file-number mismatch or IO failure.
    void write(const hbb::FileTransferBlock& block);
    // Read the next ≤128 KiB block (compressing when worthwhile). Returns
    // nullopt when all files are done; note the upstream quirk is preserved:
    // reaching EOF yields one final empty Some(block) before None.
    std::optional<hbb::FileTransferBlock> read();

private:
    TransferJob() = default;
    std::string join(const std::string& name) const;

    int32_t id_ = 0;
    std::string path_;
    std::vector<hbb::FileEntry> files_;
    int32_t file_num_ = 0;
    std::optional<std::fstream> file_;
    uint64_t total_size_ = 0;
    uint64_t finished_size_ = 0;
    uint64_t transfered_ = 0;
};

// Message builders (new_error/new_dir/new_block/new_receive/new_send/new_done).
// new_error takes the already-stringified error (ToString parity resolved by callers).
hbb::Message new_error(int32_t id, const std::string& err, int32_t file_num);
hbb::Message new_dir(int32_t id, std::vector<hbb::FileEntry> files);
hbb::Message new_block(const hbb::FileTransferBlock& block);
hbb::Message new_receive(int32_t id, const std::string& path,
                         std::vector<hbb::FileEntry> files);
hbb::Message new_send(int32_t id, const std::string& path, bool include_hidden);
hbb::Message new_done(int32_t id, int32_t file_num);

// Job-list helpers (remove_job/get_job parity; get_job returns nullptr when absent).
void remove_job(int32_t id, std::vector<TransferJob>& jobs);
TransferJob* get_job(int32_t id, std::vector<TransferJob>& jobs);

// Pump all read jobs into the stream (handle_read_jobs parity). Finished jobs
// are removed; read errors go out as error messages, like upstream.
void handle_read_jobs(std::vector<TransferJob>& jobs, TcpFramedStream& stream);

// Recursive delete helpers.
void remove_all_empty_dir(const std::string& path);
void remove_file(const std::string& file);
void create_dir(const std::string& dir);

}  // namespace hbb_common
