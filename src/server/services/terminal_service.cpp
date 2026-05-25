#include "server/server.hpp"
#include "platform/platform.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <wincon.h>
#else
#include <pty.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <termios.h>
#endif

namespace cppdesk::server {

// ====== PTY Manager ======
class PtyManager {
public:
    PtyManager() = default;
    ~PtyManager() { close(); }

    bool open(int rows, int cols, const std::string& shell) {
#ifdef _WIN32
        return open_windows(rows, cols, shell);
#else
        return open_unix(rows, cols, shell);
#endif
    }

    bool write(const std::string& data) {
        if (!master_fd_valid()) return false;
#ifdef _WIN32
        DWORD written;
        return WriteFile(master_handle_, data.c_str(), 
            static_cast<DWORD>(data.size()), &written, nullptr) != 0;
#else
        ssize_t n = ::write(master_fd_, data.c_str(), data.size());
        return n > 0;
#endif
    }

    std::string read(size_t max_bytes = 4096) {
        if (!master_fd_valid()) return "";
        std::string buf(max_bytes, '\0');
#ifdef _WIN32
        DWORD avail = 0;
        if (!PeekNamedPipe(master_handle_, nullptr, 0, nullptr, &avail, nullptr)) return "";
        if (avail == 0) return "";
        DWORD got = 0;
        ReadFile(master_handle_, buf.data(), std::min(static_cast<DWORD>(max_bytes), avail), &got, nullptr);
        buf.resize(got);
#else
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(master_fd_, &fds);
        struct timeval tv = {0, 50000};
        if (select(master_fd_ + 1, &fds, nullptr, nullptr, &tv) > 0) {
            ssize_t n = ::read(master_fd_, buf.data(), max_bytes);
            if (n > 0) buf.resize(static_cast<size_t>(n));
            else return "";
        } else {
            return "";
        }
#endif
        return buf;
    }

    bool resize(int rows, int cols) {
#ifdef _WIN32
        // CONSOLE_SCREEN_BUFFER_INFOEX
        (void)rows; (void)cols;
        return false;
#else
        if (master_fd_ < 0) return false;
        struct winsize ws;
        ws.ws_row = static_cast<unsigned short>(rows);
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        return ioctl(master_fd_, TIOCSWINSZ, &ws) == 0;
#endif
    }

    void close() {
#ifdef _WIN32
        if (process_handle_) {
            TerminateProcess(process_handle_, 0);
            CloseHandle(process_handle_);
            process_handle_ = nullptr;
        }
        if (master_handle_) {
            CloseHandle(master_handle_);
            master_handle_ = nullptr;
        }
#else
        if (child_pid_ > 0) {
            kill(child_pid_, SIGTERM);
            waitpid(child_pid_, nullptr, 0);
            child_pid_ = -1;
        }
        if (master_fd_ >= 0) {
            ::close(master_fd_);
            master_fd_ = -1;
        }
#endif
    }

    bool is_open() const {
#ifdef _WIN32
        return master_handle_ != nullptr;
#else
        return master_fd_ >= 0;
#endif
    }

    pid_t child_pid() const { return child_pid_; }

private:
#ifdef _WIN32
    HANDLE master_handle_ = nullptr;
    HANDLE process_handle_ = nullptr;
#else
    int master_fd_ = -1;
    pid_t child_pid_ = -1;
#endif

    bool master_fd_valid() const {
#ifdef _WIN32
        return master_handle_ != nullptr;
#else
        return master_fd_ >= 0;
#endif
    }

#ifdef _WIN32
    bool open_windows(int rows, int cols, const std::string& shell) {
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return false;

        STARTUPINFOW si = {sizeof(si)};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;

        PROCESS_INFORMATION pi = {};
        std::wstring cmd(shell.begin(), shell.end());
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
            0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            return false;
        }

        CloseHandle(hWritePipe);
        CloseHandle(pi.hThread);

        master_handle_ = hReadPipe;
        process_handle_ = pi.hProcess;
        return true;
    }
#else
    bool open_unix(int rows, int cols, const std::string& shell) {
        struct winsize ws;
        ws.ws_row = static_cast<unsigned short>(rows);
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;

        pid_t pid = forkpty(&master_fd_, nullptr, nullptr, &ws);
        if (pid < 0) return false;

        if (pid == 0) {
            // Child
            const char* sh = shell.empty() ? "/bin/bash" : shell.c_str();
            setenv("TERM", "xterm-256color", 1);
            execl(sh, sh, nullptr);
            _exit(127);
        }

        child_pid_ = pid;
        spdlog::info("[terminal] PTY opened: pid={}, fd={}", pid, master_fd_);
        return true;
    }
#endif
};

// ====== Terminal Session ======
class TerminalSession {
public:
    TerminalSession(int32_t conn_id, int rows, int cols, const std::string& shell)
        : conn_id_(conn_id), rows_(rows), cols_(cols), shell_(shell) {}

    bool start() {
        if (!pty_.open(rows_, cols_, shell_)) {
            spdlog::error("[terminal] Failed to open PTY for conn {}", conn_id_);
            return false;
        }
        running_ = true;
        reader_ = std::thread(&TerminalSession::read_loop, this);
        spdlog::info("[terminal] Session started for conn {}", conn_id_);
        return true;
    }

    void stop() {
        running_ = false;
        pty_.close();
        if (reader_.joinable()) reader_.join();
        spdlog::info("[terminal] Session stopped for conn {}", conn_id_);
    }

    void write_input(const std::string& data) {
        if (!running_) return;
        pty_.write(data);
        bytes_written_ += data.size();
    }

    bool resize(int rows, int cols) {
        rows_ = rows;
        cols_ = cols;
        return pty_.resize(rows, cols);
    }

    // Read available output
    std::string read_output() {
        auto out = pty_.read(4096);
        if (!out.empty()) {
            bytes_read_ += out.size();
        }
        return out;
    }

    bool is_running() const { return running_; }
    int32_t conn_id() const { return conn_id_; }

    struct Stats {
        uint64_t bytes_read = 0;
        uint64_t bytes_written = 0;
        int rows = 80;
        int cols = 24;
        std::chrono::steady_clock::time_point started_at;
    };

    Stats get_stats() const {
        return {bytes_read_, bytes_written_, rows_, cols_, started_at_};
    }

private:
    int32_t conn_id_;
    int rows_ = 80, cols_ = 24;
    std::string shell_;
    PtyManager pty_;
    std::atomic<bool> running_{false};
    std::thread reader_;
    uint64_t bytes_read_ = 0;
    uint64_t bytes_written_ = 0;
    std::chrono::steady_clock::time_point started_at_ = std::chrono::steady_clock::now();

    void read_loop() {
        while (running_) {
            auto out = pty_.read(4096);
            if (!out.empty()) {
                std::lock_guard lk(output_mutex_);
                output_buffer_ += out;
                bytes_read_ += out.size();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::string output_buffer_;
    std::mutex output_mutex_;
};

// ====== Terminal Session Manager ======
class TerminalSessionManager {
public:
    TerminalSession* create_session(int32_t conn_id, int rows, int cols,
        const std::string& shell = "/bin/bash") {
        std::lock_guard lk(mutex_);
        auto session = std::make_unique<TerminalSession>(conn_id, rows, cols, shell);
        auto* ptr = session.get();
        sessions_[conn_id] = std::move(session);
        return ptr;
    }

    void destroy_session(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        auto it = sessions_.find(conn_id);
        if (it != sessions_.end()) {
            it->second->stop();
            sessions_.erase(it);
        }
    }

    TerminalSession* get_session(int32_t conn_id) {
        std::lock_guard lk(mutex_);
        auto it = sessions_.find(conn_id);
        return it != sessions_.end() ? it->second.get() : nullptr;
    }

    void stop_all() {
        std::lock_guard lk(mutex_);
        for (auto& [id, session] : sessions_) {
            session->stop();
        }
        sessions_.clear();
    }

    size_t count() const {
        std::lock_guard lk(mutex_);
        return sessions_.size();
    }

private:
    std::map<int32_t, std::unique_ptr<TerminalSession>> sessions_;
    mutable std::mutex mutex_;
};

// ====== TerminalService ======
class TerminalServiceImpl : public GenericService {
public:
    TerminalServiceImpl() : GenericService(TerminalService::NAME) {}

    void start() override {
        spdlog::info("[terminal] Terminal service started");
    }

    void stop() override {
        session_manager_.stop_all();
        spdlog::info("[terminal] Terminal service stopped");
    }

    void on_subscribe(int32_t conn_id) override {
        GenericService::on_subscribe(conn_id);
        auto* session = session_manager_.create_session(conn_id, 24, 80);
        if (session && !session->start()) {
            session_manager_.destroy_session(conn_id);
            spdlog::error("[terminal] Failed to start session for conn {}", conn_id);
        }
    }

    void on_unsubscribe(int32_t conn_id) override {
        GenericService::on_unsubscribe(conn_id);
        session_manager_.destroy_session(conn_id);
    }

    bool write_to_session(int32_t conn_id, const std::string& data) {
        auto* session = session_manager_.get_session(conn_id);
        if (!session) return false;
        session->write_input(data);
        return true;
    }

    std::string read_from_session(int32_t conn_id) {
        auto* session = session_manager_.get_session(conn_id);
        if (!session) return "";
        return session->read_output();
    }

    bool resize_session(int32_t conn_id, int rows, int cols) {
        auto* session = session_manager_.get_session(conn_id);
        if (!session) return false;
        return session->resize(rows, cols);
    }

    size_t active_sessions() const {
        return session_manager_.count();
    }

private:
    TerminalSessionManager session_manager_;
};

} // namespace cppdesk::server
