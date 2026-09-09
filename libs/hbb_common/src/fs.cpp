// fs.cpp — see fs.hpp.
#include <hbb_common/fs.hpp>

#include <hbb_common/compress.hpp>
#include <hbb_common/config.hpp>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <algorithm>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#endif
#ifdef _WIN32
#include <windows.h>  // UNTESTED: guarded out on Linux (see below)
#endif

namespace hbb_common {
namespace {

namespace fs = std::filesystem;

constexpr size_t kReadBufSize = 128 * 1024;  // BUF_SIZE parity

uint64_t mtime_secs(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) {
        return 0;
    }
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const auto dur = sys.time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(dur).count());
}

// filetime::set_file_mtime parity (mtime only; atime preserved via UTIME_OMIT).
void set_file_mtime(const fs::path& p, uint64_t secs) {
#if defined(__unix__) || defined(__APPLE__)
    const struct timespec ts[2] = {{0, UTIME_OMIT},
                                   {static_cast<time_t>(secs), 0}};
    ::utimensat(AT_FDCWD, p.c_str(), ts, 0);
#else
    (void)p;
    (void)secs;  // Windows branch deferred to the platform step
#endif
}

std::string file_name_of(const fs::path& p) {
    return p.filename().string();  // to_str().unwrap_or("") parity (UTF-8 lossy)
}

bool is_hidden_name(const std::string& name) {
    return !name.empty() && name[0] == '.';  // name.find('.') == Some(0) parity
}

std::string get_ext(const std::string& name) {
    const size_t pos = name.rfind('.');
    return (pos == std::string::npos) ? "" : name.substr(pos + 1);
}

bool is_compressed_file(const std::string& name) {
    const std::string ext = get_ext(name);
    return ext == "xz" || ext == "gz" || ext == "zip" || ext == "7z" || ext == "rar" ||
           ext == "bz2" || ext == "tgz" || ext == "png" || ext == "jpg";
}

void append_dir_entries(const fs::path& dir, bool include_hidden, hbb::FileDirectory& out) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        throw std::system_error(ec, "read_dir");  // path.read_dir()? parity
    }
    for (const auto& entry : it) {
        const fs::path p = entry.path();
        const std::string name = file_name_of(p);
        if (name.empty()) {
            continue;
        }
        std::error_code st_ec;
        const fs::file_status st = fs::symlink_status(p, st_ec);
        if (st_ec) {
            continue;  // symlink_metadata failure parity
        }
        const bool symlink = fs::is_symlink(st);
#ifdef _WIN32
        // UNTESTED (no Windows compiler here): FILE_ATTRIBUTE_HIDDEN parity.
        const DWORD attrs = ::GetFileAttributesA(p.string().c_str());
        const bool hidden = (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_HIDDEN);
#else
        const bool hidden = is_hidden_name(name);
#endif
        if (hidden && !include_hidden) {
            continue;
        }
        hbb::FileEntry e;
        if (fs::is_directory(p)) {  // follows symlinks (Path::is_dir parity)
            e.set_entry_type(symlink ? hbb::DirLink : hbb::Dir);
            e.set_size(0);
        } else {
            e.set_entry_type(symlink ? hbb::FileLink : hbb::File);
            if (!symlink) {
                std::error_code sz_ec;
                e.set_size(static_cast<uint64_t>(fs::file_size(p, sz_ec)));
            } else {
                e.set_size(0);
            }
        }
        e.set_name(name);
        e.set_is_hidden(hidden);
        e.set_modified_time(mtime_secs(p));
        *out.add_entries() = e;
    }
}

std::vector<hbb::FileEntry> read_dir_recursive(const fs::path& path, const fs::path& prefix,
                                               bool include_hidden) {
    std::vector<hbb::FileEntry> files;
    std::error_code ec;
    if (fs::is_directory(path, ec) && !ec) {
        // to-do (upstream): symlink handling — cp the link, not the content.
        // to-do (upstream): file mode, for unix.
        const hbb::FileDirectory fd = read_dir(path.string(), include_hidden);
        for (const hbb::FileEntry& entry : fd.entries()) {
            if (entry.entry_type() == hbb::File) {
                hbb::FileEntry prefixed = entry;
                prefixed.set_name((prefix / entry.name()).string());
                files.push_back(prefixed);
            } else if (entry.entry_type() == hbb::Dir) {
                auto sub =
                    read_dir_recursive(path / entry.name(), prefix / entry.name(), include_hidden);
                files.insert(files.end(), sub.begin(), sub.end());
            }
            // DirLink/FileLink/DirDrive skipped (upstream `_ => {}` parity).
        }
        return files;
    }
    if (fs::is_regular_file(path, ec) && !ec) {
        hbb::FileEntry e;
        e.set_entry_type(hbb::File);
        e.set_size(static_cast<uint64_t>(fs::file_size(path, ec)));
        e.set_modified_time(mtime_secs(path));
        files.push_back(e);
        return files;
    }
    throw std::runtime_error("Not exists");  // bail! parity
}

}  // namespace

hbb::FileDirectory read_dir(const std::string& path, bool include_hidden) {
    hbb::FileDirectory dir;
    dir.set_path(path);
#ifdef _WIN32
    // UNTESTED: drive-list parity (GetLogicalDrives; A:..Z: for set bits).
    if (path == "/") {
        const DWORD drives = ::GetLogicalDrives();
        for (int i = 0; i < 32; ++i) {
            if (drives & (1u << i)) {
                hbb::FileEntry e;
                e.set_name(std::string(1, static_cast<char>('A' + i)) + ":");
                e.set_entry_type(hbb::DirDrive);
                *dir.add_entries() = e;
            }
        }
        return dir;
    }
#endif
    append_dir_entries(fs::path(path), include_hidden, dir);
    return dir;
}

std::string get_file_name(const std::string& path) {
    return file_name_of(fs::path(path));
}

std::string path_to_string(const std::string& path) {
    return path;  // PathBuf::to_str parity (inputs are already UTF-8 strings)
}

std::string make_path(const std::string& path) {
    return path;
}

std::string get_home_as_string() {
    return get_home().string();
}

std::vector<hbb::FileEntry> get_recursive_files(const std::string& path, bool include_hidden) {
    return read_dir_recursive(fs::path(path), fs::path(), include_hidden);
}

TransferJob TransferJob::new_write(int32_t id, const std::string& path,
                                   std::vector<hbb::FileEntry> files) {
    TransferJob job;
    uint64_t total = 0;
    for (const auto& f : files) {
        total += f.size();
    }
    job.id_ = id;
    job.path_ = path;
    job.files_ = std::move(files);
    job.total_size_ = total;
    return job;
}

TransferJob TransferJob::new_read(int32_t id, const std::string& path, bool include_hidden) {
    // Mirrors upstream exactly (files enumerated, total summed).
    return new_write(id, path, get_recursive_files(path, include_hidden));
}

const std::vector<hbb::FileEntry>& TransferJob::files() const {
    return files_;
}

void TransferJob::set_files(std::vector<hbb::FileEntry> files) {
    files_ = std::move(files);
}

int32_t TransferJob::id() const {
    return id_;
}

uint64_t TransferJob::total_size() const {
    return total_size_;
}

uint64_t TransferJob::finished_size() const {
    return finished_size_;
}

uint64_t TransferJob::transfered() const {
    return transfered_;
}

int32_t TransferJob::file_num() const {
    return file_num_;
}

std::string TransferJob::join(const std::string& name) const {
    if (name.empty()) {
        return path_;
    }
    return (fs::path(path_) / name).string();
}

void TransferJob::modify_time() const {
    const auto idx = static_cast<size_t>(file_num_);
    if (idx >= files_.size()) {
        return;
    }
    const std::string path = join(files_[idx].name());
    const std::string download = path + ".download";
    std::error_code ec;
    fs::rename(download, path, ec);
    set_file_mtime(fs::path(path), files_[idx].modified_time());
}

void TransferJob::remove_download_file() const {
    const auto idx = static_cast<size_t>(file_num_);
    if (idx >= files_.size()) {
        return;
    }
    std::error_code ec;
    fs::remove(join(files_[idx].name()) + ".download", ec);
}

void TransferJob::write(const hbb::FileTransferBlock& block) {
    if (block.id() != id_) {
        throw std::runtime_error("Wrong id");  // bail! parity
    }
    const auto num = static_cast<size_t>(block.file_num());
    if (num >= files_.size()) {
        throw std::runtime_error("Wrong file number");  // bail! parity
    }
    if (num != static_cast<size_t>(file_num_) || !file_.has_value()) {
        // Switching files: finalize the previous one first.
        modify_time();
        if (file_) {
            file_->flush();
            if (file_->fail()) {
                throw std::system_error(errno, std::generic_category(), "flush");
            }
        }
        file_num_ = block.file_num();
        const std::string path = join(files_[num].name());
        if (const fs::path parent = fs::path(path).parent_path(); !parent.empty()) {
            std::error_code ec;
            fs::create_directories(parent, ec);
        }
        file_.emplace(path + ".download", std::ios::binary | std::ios::out | std::ios::trunc);
        if (!*file_) {
            throw std::system_error(errno, std::generic_category(), "open .download");
        }
    }
    if (block.compressed()) {
        const std::vector<uint8_t> tmp =
            decompress(reinterpret_cast<const uint8_t*>(block.data().data()),
                       block.data().size());
        file_->write(reinterpret_cast<const char*>(tmp.data()), static_cast<std::streamsize>(tmp.size()));
        if (file_->fail()) {
            throw std::system_error(errno, std::generic_category(), "write");
        }
        finished_size_ += tmp.size();
    } else {
        file_->write(block.data().data(), static_cast<std::streamsize>(block.data().size()));
        if (file_->fail()) {
            throw std::system_error(errno, std::generic_category(), "write");
        }
        finished_size_ += block.data().size();
    }
    transfered_ += block.data().size();
}

std::optional<hbb::FileTransferBlock> TransferJob::read() {
    const int32_t start_num = file_num_;  // block carries the pre-increment number
    const auto num = static_cast<size_t>(start_num);
    if (num >= files_.size()) {
        file_.reset();
        return std::nullopt;
    }
    const std::string& name = files_[num].name();
    if (!file_.has_value()) {
        // NOTE: std::ios::in must be explicit — a bare binary mode opens
        // nothing on libstdc++ (File::open is read-only upstream).
        file_.emplace(join(name), std::ios::binary | std::ios::in);
        if (!*file_) {
            const int err = errno;
            ++file_num_;
            throw std::system_error(err, std::generic_category(), "open " + join(name));
        }
    }
    std::vector<uint8_t> buf(kReadBufSize);
    size_t offset = 0;
    while (true) {
        file_->read(reinterpret_cast<char*>(buf.data() + offset), static_cast<std::streamsize>(kReadBufSize - offset));
        const size_t n = static_cast<size_t>(file_->gcount());
        if (file_->bad()) {
            const int err = errno;
            ++file_num_;
            file_.reset();
            throw std::system_error(err, std::generic_category(), "read");
        }
        offset += n;
        if (n == 0 || offset == kReadBufSize) {
            break;  // EOF or full buffer (short reads loop, like upstream)
        }
        if (file_->eof()) {
            break;
        }
    }
    buf.resize(offset);
    bool compressed = false;
    if (offset == 0) {
        ++file_num_;
        file_.reset();
    } else {
        finished_size_ += offset;
        if (!is_compressed_file(name)) {
            const std::vector<uint8_t> tmp = compress(buf.data(), buf.size(), kCompressLevel);
            if (tmp.size() < buf.size()) {
                buf = tmp;
                compressed = true;
            }
        }
        transfered_ += buf.size();
    }
    // NOTE: at EOF this still returns Some(empty) — upstream quirk preserved
    // (the block carries start_num, captured before the increment above).
    hbb::FileTransferBlock block;
    block.set_id(id_);
    block.set_file_num(start_num);
    block.set_data(buf.data(), static_cast<int>(buf.size()));
    block.set_compressed(compressed);
    return block;
}

hbb::Message new_error(int32_t id, const std::string& err, int32_t file_num) {
    hbb::FileTransferError e;
    e.set_id(id);
    e.set_error(err);
    e.set_file_num(file_num);
    hbb::FileResponse resp;
    *resp.mutable_error() = e;
    hbb::Message msg;
    *msg.mutable_file_response() = resp;
    return msg;
}

hbb::Message new_dir(int32_t id, std::vector<hbb::FileEntry> files) {
    hbb::FileDirectory dir;
    dir.set_id(id);
    for (auto& f : files) {
        *dir.add_entries() = std::move(f);
    }
    hbb::FileResponse resp;
    *resp.mutable_dir() = dir;
    hbb::Message msg;
    *msg.mutable_file_response() = resp;
    return msg;
}

hbb::Message new_block(const hbb::FileTransferBlock& block) {
    hbb::FileResponse resp;
    *resp.mutable_block() = block;
    hbb::Message msg;
    *msg.mutable_file_response() = resp;
    return msg;
}

hbb::Message new_receive(int32_t id, const std::string& path, std::vector<hbb::FileEntry> files) {
    hbb::FileTransferReceiveRequest req;
    req.set_id(id);
    req.set_path(path);
    for (auto& f : files) {
        *req.add_files() = std::move(f);
    }
    hbb::FileAction action;
    *action.mutable_receive() = req;
    hbb::Message msg;
    *msg.mutable_file_action() = action;
    return msg;
}

hbb::Message new_send(int32_t id, const std::string& path, bool include_hidden) {
    hbb::FileTransferSendRequest req;
    req.set_id(id);
    req.set_path(path);
    req.set_include_hidden(include_hidden);
    hbb::FileAction action;
    *action.mutable_send() = req;
    hbb::Message msg;
    *msg.mutable_file_action() = action;
    return msg;
}

hbb::Message new_done(int32_t id, int32_t file_num) {
    hbb::FileTransferDone done;
    done.set_id(id);
    done.set_file_num(file_num);
    hbb::FileResponse resp;
    *resp.mutable_done() = done;
    hbb::Message msg;
    *msg.mutable_file_response() = resp;
    return msg;
}

void remove_job(int32_t id, std::vector<TransferJob>& jobs) {
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                              [id](const TransferJob& j) { return j.id() == id; }),
               jobs.end());
}

TransferJob* get_job(int32_t id, std::vector<TransferJob>& jobs) {
    for (auto& job : jobs) {
        if (job.id() == id) {
            return &job;
        }
    }
    return nullptr;  // None parity
}

void handle_read_jobs(std::vector<TransferJob>& jobs, TcpFramedStream& stream) {
    std::vector<int32_t> finished;
    for (auto& job : jobs) {
        try {
            if (auto block = job.read()) {
                if (!stream.send(new_block(*block))) {
                    throw std::runtime_error("send block failed");  // `.await?` parity
                }
            } else {
                finished.push_back(job.id());
                if (!stream.send(new_done(job.id(), job.file_num()))) {
                    throw std::runtime_error("send done failed");  // `.await?` parity
                }
            }
        } catch (const std::exception& e) {
            // `stream.send(&new_error(..)).await?` parity: single error-send,
            // failures propagate and abort the pump.
            if (!stream.send(new_error(job.id(), e.what(), job.file_num()))) {
                throw std::runtime_error("send error failed");
            }
        }
    }
    for (int32_t id : finished) {
        remove_job(id, jobs);
    }
}

void remove_all_empty_dir(const std::string& path) {
    const hbb::FileDirectory fd = read_dir(path, true);
    for (const hbb::FileEntry& entry : fd.entries()) {
        if (entry.entry_type() == hbb::Dir) {
            remove_all_empty_dir((fs::path(path) / entry.name()).string());
        } else if (entry.entry_type() == hbb::DirLink || entry.entry_type() == hbb::FileLink) {
            std::error_code ec;
            fs::remove(fs::path(path) / entry.name(), ec);
        }
    }
    std::error_code ec;
    fs::remove(path, ec);  // remove_dir().ok() parity
}

void remove_file(const std::string& file) {
    std::error_code ec;
    if (!fs::remove(fs::path(file), ec) || ec) {
        throw std::system_error(ec ? ec.value() : ENOENT, std::generic_category(), "remove_file");
    }
}

void create_dir(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    if (ec) {
        throw std::system_error(ec, "create_dir");
    }
}

}  // namespace hbb_common
