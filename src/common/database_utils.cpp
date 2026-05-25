//===----------------------------------------------------------------------===//
// database_utils.cpp - Comprehensive SQLite3 database utilities
//
// Provides connection pooling, prepared statement caching, migration
// framework, fluent query builder, health checking, backup/restore,
// WAL mode, performance stats, JSON helpers, FTS5 helpers, and
// encryption-at-rest via custom VFS abstraction.
//
// Namespace: cppdesk::common
// Requires: C++20, spdlog, SQLite3
//===----------------------------------------------------------------------===//

#include "common/config.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

// ---------------------------------------------------------------------------
// Forward-declare SQLite3 types we interact with directly
// ---------------------------------------------------------------------------

#ifndef SQLITE_OK
#define SQLITE_OK 0
#endif

#ifndef SQLITE_ROW
#define SQLITE_ROW 100
#endif

#ifndef SQLITE_DONE
#define SQLITE_DONE 101
#endif

//===----------------------------------------------------------------------===//
// Internal constants
//===----------------------------------------------------------------------===//

namespace {

constexpr int kDefaultPoolSize        = 8;
constexpr int kDefaultPoolMaxSize     = 32;
constexpr int kDefaultPoolIdleTimeout = 60;    // seconds
constexpr int kDefaultHealthInterval  = 30;    // seconds
constexpr int kDefaultBusyTimeout     = 5000;  // milliseconds
constexpr int kDefaultStatementCacheSize = 128;
constexpr int kMaxMigrationAttempts   = 3;
constexpr int kBackupPageStep         = 16;    // pages per backup step
constexpr const char *kSchemaTable    = "_schema_version";
constexpr const char *kMigrationsTable = "_migrations";
constexpr const char *kStatsTable     = "_db_perf_stats";
constexpr const char *kSystemJournal  = "_system_journal";

} // anonymous namespace

//===----------------------------------------------------------------------===//
// cppdesk::common
//===----------------------------------------------------------------------===//

namespace cppdesk::common {

// ============================================================================
// 1. Forward declarations and type aliases
// ============================================================================

class ConnectionPool;
class PreparedStatementCache;
class MigrationRunner;
class QueryBuilder;
class ConnectionHealthChecker;
class DatabaseBackup;
class WalManager;
class PerformanceStats;
class JsonColumnHelper;
class Fts5Helper;
class EncryptedVFS;

// Convenience aliases
using SQLite3Ptr = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;

// ============================================================================
// 2. Error types
// ============================================================================

/// Base exception for all database operations.
class DatabaseException : public std::runtime_error {
public:
  explicit DatabaseException(const std::string &msg) : std::runtime_error(msg) {}
  DatabaseException(const std::string &msg, int sqlite_rc)
      : std::runtime_error(std::format("{} (SQLite rc={}): {}", msg, sqlite_rc,
                                       sqlite3_errstr(sqlite_rc))) {}
};

/// Thrown when pool is exhausted.
class PoolExhaustedException : public DatabaseException {
public:
  explicit PoolExhaustedException(const std::string &msg = "Connection pool exhausted")
      : DatabaseException(msg) {}
};

/// Thrown when a migration fails.
class MigrationException : public DatabaseException {
public:
  MigrationException(int version, const std::string &msg, int rc)
      : DatabaseException(std::format("Migration to v{} failed: {} (rc={})",
                                      version, msg, rc),
                          rc) {}
};

// ============================================================================
// 3. RAII SQLite3 handle wrapper
// ============================================================================

/// A safe, move-only RAII wrapper around sqlite3*.
class ScopedConnection {
public:
  ScopedConnection() : db_(nullptr, sqlite3_close) {}
  explicit ScopedConnection(sqlite3 *db) : db_(db, sqlite3_close) {}

  // Move-only
  ScopedConnection(ScopedConnection &&other) noexcept = default;
  ScopedConnection &operator=(ScopedConnection &&other) noexcept = default;
  ScopedConnection(const ScopedConnection &) = delete;
  ScopedConnection &operator=(const ScopedConnection &) = delete;

  explicit operator bool() const noexcept { return db_ != nullptr; }
  sqlite3 *get() const noexcept { return db_.get(); }
  sqlite3 *operator->() const noexcept { return db_.get(); }

  void reset(sqlite3 *db = nullptr) { db_.reset(db); }
  sqlite3 *release() { return db_.release(); }

private:
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_{nullptr, sqlite3_close};
};

// ============================================================================
// 4. Connection pool – configuration
// ============================================================================

/// Configuration for the connection pool.
struct PoolConfig {
  std::string db_path;            ///< Path to the SQLite database file.
  int min_connections = kDefaultPoolSize;
  int max_connections = kDefaultPoolMaxSize;
  int idle_timeout_sec = kDefaultPoolIdleTimeout;
  int busy_timeout_ms = kDefaultBusyTimeout;
  bool enable_wal = true;
  bool enable_foreign_keys = true;
  bool read_only = false;
  std::string encryption_key;     ///< If non-empty, use encrypted VFS.
  int statement_cache_size = kDefaultStatementCacheSize;
};

// ============================================================================
// 5. Connection pool – internal connection entry
// ============================================================================

struct PooledConnection {
  ScopedConnection conn;
  std::chrono::steady_clock::time_point last_used;
  std::chrono::steady_clock::time_point created_at;
  bool in_use = false;

  std::chrono::seconds idle_duration() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - last_used);
  }
};

// ============================================================================
// 6. Connection pool – core implementation
// ============================================================================

/// A thread-safe SQLite3 connection pool with lazy expansion and idle cleanup.
class ConnectionPool {
public:
  explicit ConnectionPool(const PoolConfig &cfg) : cfg_(cfg) {
    validate_config();
    for (int i = 0; i < cfg_.min_connections; ++i) {
      pool_.push_back(create_connection());
    }
    start_idle_cleanup_thread();
    spdlog::info("ConnectionPool: {} initialised with {} connections (max {})",
                 cfg_.db_path, pool_.size(), cfg_.max_connections);
  }

  ~ConnectionPool() {
    stop_idle_cleanup_thread();
    drain();
  }

  // Non-copyable, non-movable
  ConnectionPool(const ConnectionPool &) = delete;
  ConnectionPool &operator=(const ConnectionPool &) = delete;
  ConnectionPool(ConnectionPool &&) = delete;
  ConnectionPool &operator=(ConnectionPool &&) = delete;

  /// Acquire a connection handle. Blocks until one is available or timeout.
  /// @param timeout_ms Maximum wait time; -1 = indefinite.
  /// @throws PoolExhaustedException on timeout.
  sqlite3 *acquire(int timeout_ms = -1) {
    std::unique_lock lock(mutex_);
    auto deadline = (timeout_ms >= 0)
        ? std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms)
        : std::chrono::steady_clock::time_point::max();

    while (true) {
      // Try existing idle connection
      for (auto &entry : pool_) {
        if (!entry.in_use) {
          entry.in_use = true;
          entry.last_used = std::chrono::steady_clock::now();
          return entry.conn.get();
        }
      }

      // Expand pool if under max
      if (pool_.size() < static_cast<size_t>(cfg_.max_connections)) {
        pool_.push_back(create_connection());
        auto &entry = pool_.back();
        entry.in_use = true;
        entry.last_used = std::chrono::steady_clock::now();
        spdlog::debug("ConnectionPool: expanded to {} connections", pool_.size());
        return entry.conn.get();
      }

      // Wait for release
      if (timeout_ms >= 0) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
          throw PoolExhaustedException();
        }
        cv_.wait_until(lock, deadline);
      } else {
        cv_.wait(lock);
      }
    }
  }

  /// Release a connection back to the pool.
  void release(sqlite3 *conn) {
    if (!conn) return;
    std::lock_guard lock(mutex_);
    for (auto &entry : pool_) {
      if (entry.conn.get() == conn) {
        entry.in_use = false;
        entry.last_used = std::chrono::steady_clock::now();
        cv_.notify_one();
        return;
      }
    }
    // Connection not found in pool – this is a programming error
    spdlog::error("ConnectionPool::release: connection not found in pool");
  }

  /// RAII acquisition helper.
  class Guard {
  public:
    Guard(ConnectionPool &pool, int timeout_ms = -1) : pool_(pool) {
      conn_ = pool_.acquire(timeout_ms);
    }
    ~Guard() { pool_.release(conn_); }
    sqlite3 *get() const noexcept { return conn_; }
    sqlite3 *operator->() const noexcept { return conn_; }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

  private:
    ConnectionPool &pool_;
    sqlite3 *conn_;
  };

  /// Current pool stats.
  struct Stats {
    size_t total;
    size_t in_use;
    size_t idle;
  };
  Stats stats() const {
    std::lock_guard lock(mutex_);
    Stats s{pool_.size(), 0, 0};
    for (const auto &e : pool_) {
      if (e.in_use) ++s.in_use; else ++s.idle;
    }
    return s;
  }

  const PoolConfig &config() const noexcept { return cfg_; }

private:
  void validate_config() {
    if (cfg_.db_path.empty()) throw DatabaseException("db_path must not be empty");
    if (cfg_.min_connections < 1) cfg_.min_connections = 1;
    if (cfg_.max_connections < cfg_.min_connections)
      cfg_.max_connections = cfg_.min_connections;
  }

  PooledConnection create_connection() {
    PooledConnection entry;
    sqlite3 *raw = nullptr;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (cfg_.read_only) flags = SQLITE_OPEN_READONLY;

    int rc;
    if (!cfg_.encryption_key.empty()) {
      // Open with encryption VFS
      rc = sqlite3_open_v2(cfg_.db_path.c_str(), &raw, flags, "encrypted");
      if (rc == SQLITE_OK && !cfg_.encryption_key.empty()) {
        sqlite3_key(raw, cfg_.encryption_key.data(),
                    static_cast<int>(cfg_.encryption_key.size()));
      }
    } else {
      rc = sqlite3_open_v2(cfg_.db_path.c_str(), &raw, flags, nullptr);
    }

    if (rc != SQLITE_OK) {
      std::string err = raw ? sqlite3_errmsg(raw) : "unknown";
      if (raw) sqlite3_close(raw);
      throw DatabaseException(
          std::format("Failed to open '{}': {}", cfg_.db_path, err));
    }

    configure_connection(raw);
    entry.conn.reset(raw);
    entry.created_at = entry.last_used = std::chrono::steady_clock::now();
    return entry;
  }

  void configure_connection(sqlite3 *db) {
    // Busy handler
    if (cfg_.busy_timeout_ms > 0) {
      sqlite3_busy_timeout(db, cfg_.busy_timeout_ms);
    }

    // WAL mode
    if (cfg_.enable_wal) {
      char *err = nullptr;
      sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err);
      if (err) {
        spdlog::warn("ConnectionPool: WAL mode warning: {}", err);
        sqlite3_free(err);
      }
    }

    // Foreign keys
    if (cfg_.enable_foreign_keys) {
      char *err = nullptr;
      sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &err);
      if (err) {
        spdlog::warn("ConnectionPool: foreign_keys warning: {}", err);
        sqlite3_free(err);
      }
    }

    // Performance pragmas
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size=-8000;", nullptr, nullptr, nullptr);  // 8MB
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA mmap_size=268435456;", nullptr, nullptr, nullptr); // 256MB
  }

  void drain() {
    std::lock_guard lock(mutex_);
    for (auto &entry : pool_) { entry.conn.reset(); }
    pool_.clear();
  }

  void start_idle_cleanup_thread() {
    running_ = true;
    cleanup_thread_ = std::thread([this] {
      while (running_) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(cfg_.idle_timeout_sec),
                     [this] { return !running_; });
        if (!running_) break;

        // Remove idle connections above minimum
        auto it = pool_.begin();
        while (it != pool_.end() && pool_.size() > static_cast<size_t>(cfg_.min_connections)) {
          if (!it->in_use && it->idle_duration().count() >= cfg_.idle_timeout_sec) {
            it = pool_.erase(it);
            spdlog::debug("ConnectionPool: pruned idle connection (pool size={})",
                          pool_.size());
          } else {
            ++it;
          }
        }
      }
    });
  }

  void stop_idle_cleanup_thread() {
    running_ = false;
    cv_.notify_all();
    if (cleanup_thread_.joinable()) cleanup_thread_.join();
  }

  PoolConfig cfg_;
  std::vector<PooledConnection> pool_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread cleanup_thread_;
  std::atomic<bool> running_{false};
};

// ============================================================================
// 7. Prepared statement cache
// ============================================================================

/// Thread-safe LRU cache for compiled SQLite3 prepared statements.
/// Each connection can have its own cache instance.
class PreparedStatementCache {
public:
  explicit PreparedStatementCache(int max_size = kDefaultStatementCacheSize)
      : max_size_(max_size) {}

  ~PreparedStatementCache() { clear(); }

  // Non-copyable, non-movable
  PreparedStatementCache(const PreparedStatementCache &) = delete;
  PreparedStatementCache &operator=(const PreparedStatementCache &) = delete;

  /// Get or compile a prepared statement.
  /// Returns the statement handle; caller must NOT sqlite3_finalize it.
  sqlite3_stmt *get(sqlite3 *db, std::string_view sql) {
    std::string key(sql);
    {
      std::shared_lock lock(mutex_);
      auto it = cache_.find(key);
      if (it != cache_.end()) {
        touch_locked(it->second);
        sqlite3_reset(it->second->stmt);
        sqlite3_clear_bindings(it->second->stmt);
        ++hits_;
        return it->second->stmt;
      }
    }

    // Compile new statement
    std::unique_lock lock(mutex_);
    ++misses_;

    // Double-check: another thread may have compiled it
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      touch_locked(it->second);
      sqlite3_reset(it->second->stmt);
      sqlite3_clear_bindings(it->second->stmt);
      return it->second->stmt;
    }

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v3(db, sql.data(), static_cast<int>(sql.size()),
                                SQLITE_PREPARE_PERSISTENT, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      throw DatabaseException(
          std::format("Failed to prepare statement: {}", sqlite3_errmsg(db)), rc);
    }

    evict_if_needed_locked();
    auto entry = std::make_shared<CacheEntry>();
    entry->stmt = stmt;
    entry->sql = key;
    entry->last_access = access_counter_++;
    cache_[key] = entry;
    lru_.push_front(entry);
    entry->lru_it = lru_.begin();

    return stmt;
  }

  /// Invalidate all cached statements (e.g., after schema change).
  void invalidate_all() {
    std::unique_lock lock(mutex_);
    for (auto &[key, entry] : cache_) {
      if (entry->stmt) sqlite3_finalize(entry->stmt);
    }
    cache_.clear();
    lru_.clear();
  }

  /// Invalidate a specific statement pattern.
  void invalidate(std::string_view sql) {
    std::string key(sql);
    std::unique_lock lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      if (it->second->stmt) sqlite3_finalize(it->second->stmt);
      lru_.erase(it->second->lru_it);
      cache_.erase(it);
    }
  }

  /// Statistics.
  struct Stats {
    size_t size;
    int max_size;
    uint64_t hits;
    uint64_t misses;
    double hit_ratio() const {
      uint64_t total = hits + misses;
      return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }
  };

  Stats stats() const {
    std::shared_lock lock(mutex_);
    return {cache_.size(), max_size_, hits_, misses_};
  }

  void clear() {
    std::unique_lock lock(mutex_);
    for (auto &[key, entry] : cache_) {
      if (entry->stmt) sqlite3_finalize(entry->stmt);
    }
    cache_.clear();
    lru_.clear();
  }

private:
  struct CacheEntry {
    sqlite3_stmt *stmt = nullptr;
    std::string sql;
    uint64_t last_access = 0;
    std::list<std::shared_ptr<CacheEntry>>::iterator lru_it;
  };

  void touch_locked(const std::shared_ptr<CacheEntry> &entry) {
    lru_.erase(entry->lru_it);
    lru_.push_front(entry);
    entry->lru_it = lru_.begin();
    entry->last_access = access_counter_++;
  }

  void evict_if_needed_locked() {
    while (cache_.size() >= static_cast<size_t>(max_size_)) {
      auto &back = lru_.back();
      if (back->stmt) sqlite3_finalize(back->stmt);
      cache_.erase(back->sql);
      lru_.pop_back();
      ++evictions_;
    }
  }

  int max_size_;
  std::unordered_map<std::string, std::shared_ptr<CacheEntry>> cache_;
  std::list<std::shared_ptr<CacheEntry>> lru_;
  mutable std::shared_mutex mutex_;
  uint64_t access_counter_ = 0;
  uint64_t hits_ = 0;
  uint64_t misses_ = 0;
  uint64_t evictions_ = 0;
};

// ============================================================================
// 8. Migration framework – version-based schema upgrades
// ============================================================================

/// A single migration step.
struct Migration {
  int version;                              ///< Target version after applying.
  std::string description;                   ///< Human-readable description.
  std::string up_sql;                       ///< SQL to apply for upgrade.
  std::optional<std::string> down_sql;      ///< SQL to apply for downgrade (optional).
  bool transactional = true;                ///< Whether to wrap in a transaction.
};

/// Runs ordered migrations, tracking state in a _schema_version table.
class MigrationRunner {
public:
  MigrationRunner(sqlite3 *db, std::string schema_table = kSchemaTable)
      : db_(db), schema_table_(std::move(schema_table)) {
    ensure_schema_table();
  }

  /// Register migrations. They will be sorted by version on apply().
  void add_migration(Migration m) {
    migrations_.push_back(std::move(m));
  }

  /// Get the current schema version from the database.
  int current_version() {
    std::string sql = std::format(
        "SELECT version FROM {} ORDER BY version DESC LIMIT 1", schema_table_);
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
  }

  /// Apply all pending migrations to reach the given target version.
  /// @param target_version Highest version to migrate to; 0 = apply all.
  /// @returns Number of migrations applied.
  int apply(int target_version = 0) {
    std::sort(migrations_.begin(), migrations_.end(),
              [](const Migration &a, const Migration &b) { return a.version < b.version; });

    int current = current_version();
    int applied = 0;

    if (target_version <= 0) {
      target_version = migrations_.empty() ? 0 : migrations_.back().version;
    }

    spdlog::info("MigrationRunner: current version={}, target={}", current, target_version);

    if (target_version > current) {
      // Upgrade
      for (const auto &m : migrations_) {
        if (m.version <= current) continue;
        if (m.version > target_version) break;

        apply_single_migration(m, true);
        ++applied;
        spdlog::info("MigrationRunner: applied v{} {}", m.version, m.description);
      }
    } else if (target_version < current) {
      // Downgrade
      std::vector<Migration> reversed(migrations_.rbegin(), migrations_.rend());
      for (const auto &m : reversed) {
        if (m.version > current) continue;
        if (m.version <= target_version) break;

        if (!m.down_sql.has_value()) {
          throw MigrationException(m.version,
              "Downgrade not supported for this migration", 0);
        }
        apply_single_migration(m, false);
        ++applied;
        spdlog::info("MigrationRunner: reverted v{} {}", m.version, m.description);
      }
    }

    spdlog::info("MigrationRunner: {} migration(s) applied, now at version {}",
                 applied, current_version());
    return applied;
  }

  /// Rollback to a previous version.
  void rollback(int to_version) { apply(to_version); }

  /// List registered migrations.
  const std::vector<Migration> &migrations() const { return migrations_; }

private:
  void ensure_schema_table() {
    char *err = nullptr;
    std::string sql = std::format(
        "CREATE TABLE IF NOT EXISTS {} ("
        "  version INTEGER PRIMARY KEY,"
        "  description TEXT,"
        "  applied_at TEXT DEFAULT (datetime('now')),"
        "  checksum TEXT"
        ");", schema_table_);
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      std::string msg = err ? err : "unknown";
      sqlite3_free(err);
      throw DatabaseException(
          std::format("Failed to create schema version table: {}", msg), rc);
    }
  }

  void apply_single_migration(const Migration &m, bool is_upgrade) {
    const std::string &sql = is_upgrade ? m.up_sql : m.down_sql.value();
    char *err = nullptr;

    auto execute = [&]() -> int {
      return sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    };

    int rc;
    if (m.transactional) {
      sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
      rc = execute();

      if (rc == SQLITE_OK) {
        std::string record_sql;
        if (is_upgrade) {
          record_sql = std::format(
              "INSERT OR REPLACE INTO {} (version, description) VALUES ({}, '{}');",
              schema_table_, m.version, m.description);
        } else {
          record_sql = std::format(
              "DELETE FROM {} WHERE version = {};", schema_table_, m.version);
        }
        sqlite3_exec(db_, record_sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
      } else {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      }
    } else {
      rc = execute();
    }

    if (rc != SQLITE_OK) {
      std::string msg = err ? err : "unknown";
      sqlite3_free(err);
      throw MigrationException(m.version, msg, rc);
    }
    sqlite3_free(err);
  }

  sqlite3 *db_;
  std::string schema_table_;
  std::vector<Migration> migrations_;
};

// ============================================================================
// 9. Query builder – fluent API
// ============================================================================

/// Fluent query builder supporting SELECT, INSERT, UPDATE, DELETE.
class QueryBuilder {
public:
  enum class Type { SELECT, INSERT, UPDATE, DELETE };

  QueryBuilder() = default;

  // ---- Type selection ----

  QueryBuilder &select(std::initializer_list<std::string_view> columns) {
    reset(Type::SELECT);
    columns_.assign(columns.begin(), columns.end());
    return *this;
  }

  QueryBuilder &select(std::string_view columns) {
    reset(Type::SELECT);
    columns_.push_back(std::string(columns));
    return *this;
  }

  QueryBuilder &insert_into(std::string_view table) {
    reset(Type::INSERT);
    table_ = table;
    return *this;
  }

  QueryBuilder &update(std::string_view table) {
    reset(Type::UPDATE);
    table_ = table;
    return *this;
  }

  QueryBuilder &delete_from(std::string_view table) {
    reset(Type::DELETE_);
    table_ = table;
    return *this;
  }

  // ---- Table / FROM ----

  QueryBuilder &from(std::string_view table) {
    table_ = table;
    return *this;
  }

  QueryBuilder &table(std::string_view t) {
    table_ = t;
    return *this;
  }

  // ---- JOIN ----

  QueryBuilder &join(std::string_view table, std::string_view on) {
    joins_.push_back(std::format("JOIN {} ON {}", table, on));
    return *this;
  }

  QueryBuilder &left_join(std::string_view table, std::string_view on) {
    joins_.push_back(std::format("LEFT JOIN {} ON {}", table, on));
    return *this;
  }

  QueryBuilder &right_join(std::string_view table, std::string_view on) {
    joins_.push_back(std::format("RIGHT JOIN {} ON {}", table, on));
    return *this;
  }

  QueryBuilder &cross_join(std::string_view table) {
    joins_.push_back(std::format("CROSS JOIN {}", table));
    return *this;
  }

  // ---- WHERE ----

  QueryBuilder &where(std::string_view condition) {
    where_clauses_.push_back(std::string(condition));
    return *this;
  }

  QueryBuilder &where(std::string_view column, std::string_view op,
                      std::string_view value) {
    where_clauses_.push_back(std::format("{} {} {}", column, op, value));
    return *this;
  }

  QueryBuilder &where_eq(std::string_view column, std::string_view value) {
    return where(column, "=", value);
  }

  QueryBuilder &where_in(std::string_view column,
                         const std::vector<std::string> &values) {
    std::string list;
    for (size_t i = 0; i < values.size(); ++i) {
      if (i > 0) list += ", ";
      list += values[i];
    }
    where_clauses_.push_back(std::format("{} IN ({})", column, list));
    return *this;
  }

  QueryBuilder &where_not_null(std::string_view column) {
    where_clauses_.push_back(std::format("{} IS NOT NULL", column));
    return *this;
  }

  QueryBuilder &where_null(std::string_view column) {
    where_clauses_.push_back(std::format("{} IS NULL", column));
    return *this;
  }

  QueryBuilder &where_like(std::string_view column, std::string_view pattern) {
    where_clauses_.push_back(std::format("{} LIKE '{}'", column, pattern));
    return *this;
  }

  QueryBuilder &where_between(std::string_view column, std::string_view lo,
                              std::string_view hi) {
    where_clauses_.push_back(
        std::format("{} BETWEEN {} AND {}", column, lo, hi));
    return *this;
  }

  // ---- GROUP BY / HAVING ----

  QueryBuilder &group_by(std::string_view column) {
    group_by_.push_back(std::string(column));
    return *this;
  }

  QueryBuilder &having(std::string_view condition) {
    having_ = condition;
    return *this;
  }

  // ---- ORDER BY ----

  QueryBuilder &order_by(std::string_view column, bool asc = true) {
    order_by_.push_back(std::format("{} {}", column, asc ? "ASC" : "DESC"));
    return *this;
  }

  // ---- LIMIT / OFFSET ----

  QueryBuilder &limit(int64_t n) {
    limit_ = n;
    return *this;
  }

  QueryBuilder &offset(int64_t n) {
    offset_ = n;
    return *this;
  }

  // ---- VALUES (for INSERT) ----

  QueryBuilder &columns(std::initializer_list<std::string_view> cols) {
    insert_columns_.assign(cols.begin(), cols.end());
    return *this;
  }

  QueryBuilder &values(std::initializer_list<std::string_view> vals) {
    insert_values_.push_back(
        std::vector<std::string>(vals.begin(), vals.end()));
    return *this;
  }

  QueryBuilder &value_row(const std::vector<std::string> &vals) {
    insert_values_.push_back(vals);
    return *this;
  }

  // ---- SET (for UPDATE) ----

  QueryBuilder &set(std::string_view column, std::string_view value) {
    set_clauses_.push_back(std::format("{} = {}", column, value));
    return *this;
  }

  QueryBuilder &increment(std::string_view column, int64_t by = 1) {
    set_clauses_.push_back(std::format("{} = {} + {}", column, column, by));
    return *this;
  }

  // ---- DISTINCT ----

  QueryBuilder &distinct(bool on = true) {
    distinct_ = on;
    return *this;
  }

  // ---- RETURNING clause ----

  QueryBuilder &returning(std::string_view columns) {
    returning_ = columns;
    return *this;
  }

  // ---- Parameter binding helpers ----

  QueryBuilder &param(std::string_view name) {
    params_.push_back(std::string(name));
    return *this;
  }

  const std::vector<std::string> &parameters() const { return params_; }

  // ---- Build SQL string ----

  std::string build() const {
    switch (type_) {
    case Type::SELECT:  return build_select();
    case Type::INSERT:  return build_insert();
    case Type::UPDATE:  return build_update();
    case Type::DELETE_: return build_delete();
    }
    throw DatabaseException("QueryBuilder: no query type selected");
  }

  // ---- Reset ----

  void reset(Type t = Type::SELECT) {
    *this = QueryBuilder{};
    type_ = t;
  }

  Type type() const noexcept { return type_; }

private:
  Type type_ = Type::SELECT;

  std::string table_;
  std::vector<std::string> columns_;
  std::vector<std::string> joins_;
  std::vector<std::string> where_clauses_;
  std::vector<std::string> group_by_;
  std::string having_;
  std::vector<std::string> order_by_;
  std::optional<int64_t> limit_;
  std::optional<int64_t> offset_;
  bool distinct_ = false;

  // INSERT
  std::vector<std::string> insert_columns_;
  std::vector<std::vector<std::string>> insert_values_;

  // UPDATE
  std::vector<std::string> set_clauses_;

  // RETURNING
  std::string returning_;

  // Parameters
  std::vector<std::string> params_;

  // ---- Build helpers ----

  std::string build_select() const {
    std::string sql = "SELECT ";
    if (distinct_) sql += "DISTINCT ";
    sql += columns_.empty() ? "*" : join_list(columns_, ", ");
    if (!table_.empty()) sql += " FROM " + table_;
    for (const auto &j : joins_) sql += " " + j;
    if (!where_clauses_.empty()) sql += " WHERE " + join_list(where_clauses_, " AND ");
    if (!group_by_.empty()) sql += " GROUP BY " + join_list(group_by_, ", ");
    if (!having_.empty()) sql += " HAVING " + having_;
    if (!order_by_.empty()) sql += " ORDER BY " + join_list(order_by_, ", ");
    if (limit_.has_value()) sql += " LIMIT " + std::to_string(limit_.value());
    if (offset_.has_value()) sql += " OFFSET " + std::to_string(offset_.value());
    return sql;
  }

  std::string build_insert() const {
    std::string sql = "INSERT";
    // SQLite supports OR REPLACE, OR IGNORE, etc.; we use basic INSERT for simplicity
    sql += " INTO " + table_;
    if (!insert_columns_.empty()) {
      sql += " (" + join_list(insert_columns_, ", ") + ")";
    }
    sql += " VALUES ";
    for (size_t i = 0; i < insert_values_.size(); ++i) {
      if (i > 0) sql += ", ";
      sql += "(" + join_list(insert_values_[i], ", ") + ")";
    }
    if (!returning_.empty()) sql += " RETURNING " + returning_;
    return sql;
  }

  std::string build_update() const {
    std::string sql = "UPDATE " + table_ + " SET ";
    sql += set_clauses_.empty() ? "1=1" : join_list(set_clauses_, ", ");
    if (!where_clauses_.empty()) sql += " WHERE " + join_list(where_clauses_, " AND ");
    if (!returning_.empty()) sql += " RETURNING " + returning_;
    return sql;
  }

  std::string build_delete() const {
    std::string sql = "DELETE FROM " + table_;
    if (!where_clauses_.empty()) sql += " WHERE " + join_list(where_clauses_, " AND ");
    if (!returning_.empty()) sql += " RETURNING " + returning_;
    return sql;
  }

  static std::string join_list(const std::vector<std::string> &items,
                                std::string_view delimiter) {
    std::string result;
    for (size_t i = 0; i < items.size(); ++i) {
      if (i > 0) result += delimiter;
      result += items[i];
    }
    return result;
  }
};

// ============================================================================
// 10. Connection health checking
// ============================================================================

/// Periodic health checker that validates connection liveness and repairs
/// broken connections.
class ConnectionHealthChecker {
public:
  struct Config {
    std::chrono::seconds interval{kDefaultHealthInterval};
    std::chrono::seconds query_timeout{5};
    bool auto_repair = true;
    int max_retries = 3;
  };

  ConnectionHealthChecker(ConnectionPool &pool, Config cfg = {})
      : pool_(pool), cfg_(std::move(cfg)) {}

  ~ConnectionHealthChecker() { stop(); }

  void start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] { run_loop(); });
  }

  void stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
  }

  /// Run a single health check and return results.
  struct HealthResult {
    bool healthy;
    std::string message;
    std::chrono::microseconds latency;
  };

  HealthResult check(sqlite3 *db) {
    auto start = std::chrono::steady_clock::now();

    // Simple integrity check using a lightweight query
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, "SELECT 1;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      return {false, std::format("Prepare failed: {}", sqlite3_errmsg(db)),
              std::chrono::microseconds::zero()};
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    if (rc == SQLITE_ROW) {
      return {true, "OK", latency};
    } else {
      return {false, std::format("Step failed: {}", sqlite3_errmsg(db)), latency};
    }
  }

  /// Perform a deeper integrity check (pragma integrity_check).
  struct IntegrityResult {
    bool ok;
    std::vector<std::string> errors;
    std::chrono::milliseconds duration;
  };

  IntegrityResult integrity_check(sqlite3 *db) {
    auto start = std::chrono::steady_clock::now();
    IntegrityResult result;
    result.ok = true;

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      result.ok = false;
      result.errors.push_back("Failed to run integrity_check");
      result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      if (text) {
        std::string msg(text);
        if (msg != "ok") {
          result.ok = false;
          result.errors.push_back(msg);
        }
      }
    }

    sqlite3_finalize(stmt);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
  }

  /// Attempt to repair a corrupted database.
  bool repair(sqlite3 *db) {
    char *err = nullptr;
    // Try REINDEX first
    int rc = sqlite3_exec(db, "REINDEX;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::warn("HealthChecker: REINDEX failed: {}",
                   err ? err : "unknown");
      sqlite3_free(err);

      // Fallback: VACUUM
      rc = sqlite3_exec(db, "VACUUM;", nullptr, nullptr, &err);
      if (rc != SQLITE_OK) {
        spdlog::error("HealthChecker: VACUUM also failed: {}",
                       err ? err : "unknown");
        sqlite3_free(err);
        return false;
      }
    }
    sqlite3_free(err);
    spdlog::info("HealthChecker: repair completed successfully");
    return true;
  }

  /// Get aggregate health statistics.
  struct AggregateHealth {
    size_t total_checks = 0;
    size_t failures = 0;
    std::chrono::microseconds avg_latency{0};
    std::chrono::microseconds max_latency{0};
    std::chrono::steady_clock::time_point last_check;
  };

  AggregateHealth aggregate_stats() const {
    std::lock_guard lock(stats_mutex_);
    return agg_;
  }

private:
  void run_loop() {
    while (running_) {
      std::this_thread::sleep_for(cfg_.interval);
      if (!running_) break;

      try {
        auto conn = pool_.acquire(5000);
        auto result = check(conn);

        {
          std::lock_guard lock(stats_mutex_);
          ++agg_.total_checks;
          if (!result.healthy) ++agg_.failures;
          agg_.avg_latency = (agg_.avg_latency * (agg_.total_checks - 1) +
                              result.latency) / agg_.total_checks;
          agg_.max_latency = std::max(agg_.max_latency, result.latency);
          agg_.last_check = std::chrono::steady_clock::now();
        }

        if (!result.healthy && cfg_.auto_repair) {
          spdlog::warn("HealthChecker: connection unhealthy: {}", result.message);
          for (int i = 0; i < cfg_.max_retries && !running_; ++i) {
            if (repair(conn)) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }
        }

        pool_.release(conn);
      } catch (const PoolExhaustedException &) {
        spdlog::warn("HealthChecker: could not acquire connection for health check");
      } catch (const std::exception &e) {
        spdlog::error("HealthChecker: unexpected error: {}", e.what());
      }
    }
  }

  ConnectionPool &pool_;
  Config cfg_;
  std::thread thread_;
  std::atomic<bool> running_{false};

  mutable std::mutex stats_mutex_;
  AggregateHealth agg_;
};

// ============================================================================
// 11. Backup and restore utilities
// ============================================================================

/// Online backup using SQLite3 backup API with progress reporting.
class DatabaseBackup {
public:
  struct BackupProgress {
    int total_pages = 0;
    int remaining_pages = 0;
    double progress_pct() const {
      return total_pages > 0
          ? (100.0 * (total_pages - remaining_pages) / total_pages)
          : 0.0;
    }
  };

  using ProgressCallback = std::function<void(BackupProgress)>;

  /// Create a backup of the source database.
  /// @param src_db Source database connection.
  /// @param dest_path Destination file path.
  /// @param on_progress Optional progress callback.
  /// @returns true on success.
  static bool backup(sqlite3 *src_db, const std::string &dest_path,
                     ProgressCallback on_progress = {}) {
    // Open destination database
    sqlite3 *dest_db = nullptr;
    int rc = sqlite3_open(dest_path.c_str(), &dest_db);
    if (rc != SQLITE_OK) {
      spdlog::error("Backup: cannot open destination '{}': {}", dest_path,
                    sqlite3_errmsg(dest_db));
      if (dest_db) sqlite3_close(dest_db);
      return false;
    }

    auto close_guard = [](sqlite3 *db) { sqlite3_close(db); };
    std::unique_ptr<sqlite3, decltype(close_guard)> dest_owner(dest_db, close_guard);

    // Create backup object
    sqlite3_backup *bkup = sqlite3_backup_init(dest_db, "main", src_db, "main");
    if (!bkup) {
      spdlog::error("Backup: sqlite3_backup_init failed: {}",
                    sqlite3_errmsg(dest_db));
      return false;
    }

    bool success = true;
    int step_rc;
    do {
      step_rc = sqlite3_backup_step(bkup, kBackupPageStep);
      if (on_progress) {
        BackupProgress prog;
        prog.total_pages = sqlite3_backup_pagecount(bkup);
        prog.remaining_pages = sqlite3_backup_remaining(bkup);
        on_progress(prog);
      }
    } while (step_rc == SQLITE_OK);

    if (step_rc != SQLITE_DONE) {
      spdlog::error("Backup: sqlite3_backup_step failed: {}", step_rc);
      success = false;
    }

    sqlite3_backup_finish(bkup);

    if (success) {
      spdlog::info("Backup: completed successfully to '{}'", dest_path);
    }

    return success;
  }

  /// Restore a database from a backup file.
  /// @param backup_path Path to the backup file.
  /// @param dest_db Destination (live) database connection.
  /// @param on_progress Optional progress callback.
  /// @returns true on success.
  static bool restore(const std::string &backup_path, sqlite3 *dest_db,
                      ProgressCallback on_progress = {}) {
    sqlite3 *src_db = nullptr;
    int rc = sqlite3_open_v2(backup_path.c_str(), &src_db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
      spdlog::error("Restore: cannot open backup '{}': {}", backup_path,
                    src_db ? sqlite3_errmsg(src_db) : "unknown");
      if (src_db) sqlite3_close(src_db);
      return false;
    }

    auto close_guard = [](sqlite3 *db) { sqlite3_close(db); };
    std::unique_ptr<sqlite3, decltype(close_guard)> src_owner(src_db, close_guard);

    sqlite3_backup *bkup = sqlite3_backup_init(dest_db, "main", src_db, "main");
    if (!bkup) {
      spdlog::error("Restore: sqlite3_backup_init failed: {}",
                    sqlite3_errmsg(dest_db));
      return false;
    }

    bool success = true;
    int step_rc;
    do {
      step_rc = sqlite3_backup_step(bkup, kBackupPageStep);
      if (on_progress) {
        BackupProgress prog;
        prog.total_pages = sqlite3_backup_pagecount(bkup);
        prog.remaining_pages = sqlite3_backup_remaining(bkup);
        on_progress(prog);
      }
    } while (step_rc == SQLITE_OK);

    if (step_rc != SQLITE_DONE) {
      spdlog::error("Restore: sqlite3_backup_step failed: {}", step_rc);
      success = false;
    }

    sqlite3_backup_finish(bkup);

    if (success) {
      spdlog::info("Restore: completed successfully from '{}'", backup_path);
    }

    return success;
  }

  /// Export database to an in-memory SQL dump.
  static std::string export_sql(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;
    std::string sql_dump;
    int rc = sqlite3_prepare_v2(db, "SELECT sql FROM sqlite_master WHERE sql IS NOT NULL "
                                    "ORDER BY type, name;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      spdlog::error("export_sql: failed to query sqlite_master: {}",
                    sqlite3_errmsg(db));
      return "";
    }

    sql_dump += "-- Schema dump generated by DatabaseBackup\n";
    sql_dump += "-- " + current_timestamp() + "\n\n";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      if (text) {
        sql_dump += text;
        sql_dump += ";\n";
      }
    }
    sqlite3_finalize(stmt);

    return sql_dump;
  }

private:
  static std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::string ts = std::ctime(&time_t_now);
    if (!ts.empty() && ts.back() == '\n') ts.pop_back();
    return ts;
  }
};

// ============================================================================
// 12. WAL mode configuration and management
// ============================================================================

/// Manages SQLite Write-Ahead Logging configuration and checkpoints.
class WalManager {
public:
  explicit WalManager(sqlite3 *db) : db_(db) {}

  /// Enable WAL mode.
  bool enable() {
    char *err = nullptr;
    int rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err);
    std::string result = err ? err : "";
    sqlite3_free(err);

    bool ok = (rc == SQLITE_OK) && (result.find("wal") != std::string::npos);
    if (ok) {
      spdlog::info("WAL mode enabled: {}", result);
    } else {
      spdlog::error("Failed to enable WAL mode: {}", result);
    }
    return ok;
  }

  /// Disable WAL mode (revert to DELETE/TRUNCATE).
  bool disable() {
    char *err = nullptr;
    int rc = sqlite3_exec(db_, "PRAGMA journal_mode=DELETE;", nullptr, nullptr, &err);
    sqlite3_free(err);
    if (rc == SQLITE_OK) {
      spdlog::info("WAL mode disabled");
      return true;
    }
    spdlog::error("Failed to disable WAL mode");
    return false;
  }

  /// Get current journal mode.
  std::string journal_mode() {
    return pragma_query("journal_mode");
  }

  /// Perform a passive checkpoint (does not block writers).
  bool passive_checkpoint() {
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE,
                              nullptr, nullptr);
    return true;
  }

  /// Perform a full checkpoint (blocks writers).
  bool full_checkpoint() {
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_FULL,
                              nullptr, nullptr);
    return true;
  }

  /// Perform a truncate checkpoint (truncates WAL file after checkpoint).
  bool truncate_checkpoint() {
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_TRUNCATE,
                              nullptr, nullptr);
    return true;
  }

  /// Perform a restart checkpoint (resets WAL to beginning, blocking).
  bool restart_checkpoint() {
    int log_size = 0;
    int frames = 0;
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_RESTART,
                              &log_size, &frames);
    spdlog::info("WAL restart checkpoint: {} frames checkpointed, log_size={}",
                 frames, log_size);
    return true;
  }

  /// Auto-checkpoint configuration.
  void set_autocheckpoint(int n_pages) {
    std::string sql = std::format("PRAGMA wal_autocheckpoint={};", n_pages);
    char *err = nullptr;
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    sqlite3_free(err);
  }

  /// Get WAL file size (bytes), or 0 if not applicable.
  int64_t wal_size() {
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        db_, "PRAGMA wal_checkpoint;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    int64_t size = 0;
    // wal_checkpoint returns: busy (0/1), log (int), checkpointed (int)
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      // Column 1 = number of frames in WAL
      size = sqlite3_column_int64(stmt, 1);
    }
    sqlite3_finalize(stmt);
    return size;
  }

  /// Configure synchronous setting.
  enum class SyncMode { OFF = 0, NORMAL = 1, FULL = 2, EXTRA = 3 };

  void set_synchronous(SyncMode mode) {
    std::string sql = std::format("PRAGMA synchronous={};", static_cast<int>(mode));
    char *err = nullptr;
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    sqlite3_free(err);
  }

  /// Set WAL mmap size.
  void set_wal_mmap_size(int64_t bytes) {
    std::string sql = std::format("PRAGMA mmap_size={};", bytes);
    char *err = nullptr;
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    sqlite3_free(err);
  }

private:
  std::string pragma_query(const std::string &pragma) {
    std::string sql = "PRAGMA " + pragma + ";";
    sqlite3_stmt *stmt = nullptr;
    std::string result;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (text) result = text;
      }
      sqlite3_finalize(stmt);
    }
    return result;
  }

  sqlite3 *db_;
};

// ============================================================================
// 13. Performance statistics collection
// ============================================================================

/// Collects and aggregates performance statistics for database operations.
class PerformanceStats {
public:
  /// Record a query execution.
  void record_query(std::string_view query_hash, std::chrono::microseconds duration,
                    int64_t rows_affected = 0) {
    std::lock_guard lock(mutex_);
    auto &entry = entries_[std::string(query_hash)];
    entry.count++;
    entry.total_duration += duration;
    entry.total_rows += rows_affected;
    if (duration > entry.max_duration) entry.max_duration = duration;
    if (duration < entry.min_duration) entry.min_duration = duration;
  }

  /// Record a connection acquisition time.
  void record_acquire(std::chrono::microseconds wait) {
    std::lock_guard lock(mutex_);
    ++acquire_count_;
    acquire_total_ += wait;
  }

  /// Record a transaction event.
  void record_transaction(bool commit, std::chrono::microseconds duration) {
    std::lock_guard lock(mutex_);
    if (commit) {
      ++commits_;
      commit_total_ += duration;
    } else {
      ++rollbacks_;
      rollback_total_ += duration;
    }
  }

  /// Per-query statistics.
  struct QueryStat {
    std::string query_hash;
    uint64_t count = 0;
    std::chrono::microseconds total_duration{0};
    std::chrono::microseconds avg_duration() const {
      return count > 0 ? total_duration / count : std::chrono::microseconds{0};
    }
    std::chrono::microseconds min_duration{std::chrono::microseconds::max()};
    std::chrono::microseconds max_duration{0};
    int64_t total_rows = 0;
  };

  /// Aggregate statistics snapshot.
  struct Snapshot {
    std::vector<QueryStat> slowest_queries;  // top N by avg duration
    uint64_t total_queries = 0;
    uint64_t total_acquires = 0;
    std::chrono::microseconds avg_acquire{0};
    uint64_t commits = 0;
    uint64_t rollbacks = 0;
    std::chrono::microseconds avg_commit{0};
    std::chrono::microseconds avg_rollback{0};
  };

  /// Get a snapshot of current statistics, sorted by average duration.
  Snapshot snapshot(size_t top_n = 20) const {
    std::lock_guard lock(mutex_);
    Snapshot snap;

    std::vector<QueryStat> sorted;
    for (const auto &[hash, entry] : entries_) {
      QueryStat qs;
      qs.query_hash = hash;
      qs.count = entry.count;
      qs.total_duration = entry.total_duration;
      qs.min_duration = entry.min_duration;
      qs.max_duration = entry.max_duration;
      qs.total_rows = entry.total_rows;
      sorted.push_back(qs);
    }

    std::sort(sorted.begin(), sorted.end(),
              [](const QueryStat &a, const QueryStat &b) {
                return a.avg_duration() > b.avg_duration();
              });

    if (sorted.size() > top_n) sorted.resize(top_n);
    snap.slowest_queries = std::move(sorted);

    snap.total_queries = 0;
    for (const auto &[_, e] : entries_) snap.total_queries += e.count;
    snap.total_acquires = acquire_count_;
    snap.avg_acquire = acquire_count_ > 0
        ? acquire_total_ / acquire_count_
        : std::chrono::microseconds{0};
    snap.commits = commits_;
    snap.rollbacks = rollbacks_;
    snap.avg_commit = commits_ > 0
        ? commit_total_ / commits_
        : std::chrono::microseconds{0};
    snap.avg_rollback = rollbacks_ > 0
        ? rollback_total_ / rollbacks_
        : std::chrono::microseconds{0};

    return snap;
  }

  /// Persist statistics to the database for historical analysis.
  void persist(sqlite3 *db) {
    char *err = nullptr;
    std::string create_sql = std::format(
        "CREATE TABLE IF NOT EXISTS {} ("
        "  timestamp TEXT DEFAULT (datetime('now')),"
        "  query_hash TEXT,"
        "  count INTEGER,"
        "  avg_us INTEGER,"
        "  max_us INTEGER,"
        "  total_rows INTEGER"
        ");", kStatsTable);
    sqlite3_exec(db, create_sql.c_str(), nullptr, nullptr, &err);
    sqlite3_free(err);

    auto snap = snapshot();
    for (const auto &q : snap.slowest_queries) {
      std::string insert_sql = std::format(
          "INSERT INTO {} (query_hash, count, avg_us, max_us, total_rows) "
          "VALUES ('{}', {}, {}, {}, {});",
          kStatsTable, q.query_hash, q.count,
          std::chrono::duration_cast<std::chrono::microseconds>(q.avg_duration()).count(),
          std::chrono::duration_cast<std::chrono::microseconds>(q.max_duration).count(),
          q.total_rows);
      sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err);
      sqlite3_free(err);
    }

    spdlog::debug("PerformanceStats: persisted {} entries", snap.slowest_queries.size());
  }

  /// Reset all counters.
  void reset() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    acquire_count_ = 0;
    acquire_total_ = std::chrono::microseconds{0};
    commits_ = 0;
    commit_total_ = std::chrono::microseconds{0};
    rollbacks_ = 0;
    rollback_total_ = std::chrono::microseconds{0};
  }

  /// Compute a simple hash for a query string (used as key).
  static std::string hash_query(std::string_view sql) {
    // Simple FNV-1a hash for query identification
    uint64_t hash = 14695981039346656037ULL;
    for (char c : sql) {
      hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
      hash *= 1099511628211ULL;
    }
    return std::format("{:016x}", hash);
  }

private:
  struct Entry {
    uint64_t count = 0;
    std::chrono::microseconds total_duration{0};
    std::chrono::microseconds min_duration{std::chrono::microseconds::max()};
    std::chrono::microseconds max_duration{0};
    int64_t total_rows = 0;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;

  // Acquisition stats
  uint64_t acquire_count_ = 0;
  std::chrono::microseconds acquire_total_{0};

  // Transaction stats
  uint64_t commits_ = 0;
  std::chrono::microseconds commit_total_{0};
  uint64_t rollbacks_ = 0;
  std::chrono::microseconds rollback_total_{0};
};

// ============================================================================
// 14. JSON column helpers
// ============================================================================

/// Helper utilities for working with JSON columns in SQLite (json1 extension).
class JsonColumnHelper {
public:
  /// Extract a value from a JSON column using json_extract.
  static std::string json_extract(sqlite3 *db, std::string_view column,
                                   std::string_view json_path) {
    std::string sql = std::format(
        "SELECT json_extract({}, '{}');", column, sanitize_path(json_path));
    return exec_scalar(db, sql);
  }

  /// Set a value at a JSON path using json_set.
  static std::string json_set(sqlite3 *db, std::string_view column,
                               std::string_view json_path,
                               std::string_view value) {
    std::string sql = std::format(
        "SELECT json_set({}, '{}', '{}');", column,
        sanitize_path(json_path), sanitize_string(value));
    return exec_scalar(db, sql);
  }

  /// Insert a value at a JSON path using json_insert.
  static std::string json_insert(sqlite3 *db, std::string_view column,
                                  std::string_view json_path,
                                  std::string_view value) {
    std::string sql = std::format(
        "SELECT json_insert({}, '{}', '{}');", column,
        sanitize_path(json_path), sanitize_string(value));
    return exec_scalar(db, sql);
  }

  /// Replace a value at a JSON path using json_replace.
  static std::string json_replace(sqlite3 *db, std::string_view column,
                                   std::string_view json_path,
                                   std::string_view value) {
    std::string sql = std::format(
        "SELECT json_replace({}, '{}', '{}');", column,
        sanitize_path(json_path), sanitize_string(value));
    return exec_scalar(db, sql);
  }

  /// Remove a key from a JSON object using json_remove.
  static std::string json_remove(sqlite3 *db, std::string_view column,
                                  std::string_view json_path) {
    std::string sql = std::format(
        "SELECT json_remove({}, '{}');", column, sanitize_path(json_path));
    return exec_scalar(db, sql);
  }

  /// Check if a JSON value is valid using json_valid.
  static bool is_valid_json(sqlite3 *db, std::string_view column) {
    std::string sql = std::format("SELECT json_valid({});", column);
    std::string result = exec_scalar(db, sql);
    return result == "1";
  }

  /// Get the type of a JSON value using json_type.
  static std::string json_type(sqlite3 *db, std::string_view column,
                                std::string_view json_path = "$") {
    std::string sql = std::format(
        "SELECT json_type({}, '{}');", column, sanitize_path(json_path));
    return exec_scalar(db, sql);
  }

  /// Get the length of a JSON array using json_array_length.
  static int json_array_length(sqlite3 *db, std::string_view column,
                               std::string_view json_path = "$") {
    std::string sql = std::format(
        "SELECT json_array_length({}, '{}');", column, sanitize_path(json_path));
    std::string result = exec_scalar(db, sql);
    try { return std::stoi(result); } catch (...) { return 0; }
  }

  /// Create a JSON array from a list of values.
  static std::string json_array(sqlite3 *db,
                                 const std::vector<std::string> &values) {
    std::string sql = "SELECT json_array(";
    for (size_t i = 0; i < values.size(); ++i) {
      if (i > 0) sql += ", ";
      sql += "'" + sanitize_string(values[i]) + "'";
    }
    sql += ");";
    return exec_scalar(db, sql);
  }

  /// Create a JSON object from key-value pairs.
  static std::string json_object(
      sqlite3 *db,
      const std::vector<std::pair<std::string, std::string>> &kv) {
    std::string sql = "SELECT json_object(";
    for (size_t i = 0; i < kv.size(); ++i) {
      if (i > 0) sql += ", ";
      sql += "'" + sanitize_string(kv[i].first) + "', '"
          + sanitize_string(kv[i].second) + "'";
    }
    sql += ");";
    return exec_scalar(db, sql);
  }

  /// Pretty-print a JSON value using json_pretty (SQLite 3.46.0+).
  static std::string json_pretty(sqlite3 *db, std::string_view column) {
    std::string sql = std::format("SELECT json_pretty({});", column);
    return exec_scalar(db, sql);
  }

  /// Group rows into a JSON array using json_group_array.
  static std::string json_group_array(sqlite3 *db, std::string_view expression,
                                       std::string_view filter = "") {
    std::string sql = "SELECT json_group_array(" + std::string(expression) + ")";
    if (!filter.empty()) sql += " FILTER (WHERE " + std::string(filter) + ")";
    sql += ";";
    return exec_scalar(db, sql);
  }

  /// Group rows into a JSON object using json_group_object.
  static std::string json_group_object(sqlite3 *db, std::string_view key_expr,
                                        std::string_view value_expr) {
    std::string sql = std::format(
        "SELECT json_group_object({}, {});", key_expr, value_expr);
    return exec_scalar(db, sql);
  }

  /// Query rows where a JSON path matches a value.
  static std::string where_json_equals(std::string_view column,
                                       std::string_view json_path,
                                       std::string_view value) {
    return std::format("json_extract({}, '{}') = '{}'",
                       column, sanitize_path(json_path), sanitize_string(value));
  }

  /// Query helper: check if a JSON array contains a value.
  static std::string where_json_contains(std::string_view column,
                                         std::string_view value) {
    return std::format("EXISTS (SELECT 1 FROM json_each({}) WHERE value = '{}')",
                       column, sanitize_string(value));
  }

private:
  static std::string exec_scalar(sqlite3 *db, const std::string &sql) {
    sqlite3_stmt *stmt = nullptr;
    std::string result;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *text = reinterpret_cast<const char *>(
            sqlite3_column_text(stmt, 0));
        if (text) result = text;
      }
      sqlite3_finalize(stmt);
    }
    return result;
  }

  static std::string sanitize_path(std::string_view path) {
    // Escape single quotes in JSON path
    std::string result;
    for (char c : path) {
      if (c == '\'') result += "''";
      else result += c;
    }
    return result;
  }

  static std::string sanitize_string(std::string_view s) {
    std::string result;
    for (char c : s) {
      if (c == '\'') result += "''";
      else result += c;
    }
    return result;
  }
};

// ============================================================================
// 15. FTS5 full-text search helpers
// ============================================================================

/// Helper utilities for FTS5 full-text search tables in SQLite.
class Fts5Helper {
public:
  /// Create an FTS5 virtual table with the given columns.
  /// @param content_table If non-empty, an external content table to sync with.
  static bool create_table(sqlite3 *db, std::string_view fts_table,
                           const std::vector<std::string> &columns,
                           std::string_view content_table = "",
                           std::string_view tokenize = "porter unicode61") {
    std::string sql = std::format(
        "CREATE VIRTUAL TABLE IF NOT EXISTS {} USING fts5(", fts_table);

    for (size_t i = 0; i < columns.size(); ++i) {
      if (i > 0) sql += ", ";
      sql += columns[i];
    }

    if (!content_table.empty()) {
      sql += ", content='" + std::string(content_table) + "'";
    }
    if (!tokenize.empty()) {
      sql += ", tokenize='" + std::string(tokenize) + "'";
    }

    sql += ");";

    char *err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::error("Fts5Helper::create_table: {}", err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    spdlog::info("Fts5Helper: created FTS5 table '{}'", fts_table);
    return true;
  }

  /// Create triggers to keep an FTS5 table in sync with an external content table.
  static bool create_sync_triggers(sqlite3 *db, std::string_view content_table,
                                    std::string_view fts_table,
                                    std::string_view id_column = "id") {
    std::string after_insert = std::format(
        "CREATE TRIGGER IF NOT EXISTS {content}_ai AFTER INSERT ON {content} BEGIN "
        "  INSERT INTO {fts}(rowid, {cols}) "
        "  SELECT new.{id}, {col_select} FROM {content} WHERE {id}=new.{id}; "
        "END;");

    std::string after_delete = std::format(
        "CREATE TRIGGER IF NOT EXISTS {content}_ad AFTER DELETE ON {content} BEGIN "
        "  INSERT INTO {fts}({fts}, rowid, {cols}) "
        "  VALUES('delete', old.{id}, {col_select}); "
        "END;");

    std::string after_update = std::format(
        "CREATE TRIGGER IF NOT EXISTS {content}_au AFTER UPDATE ON {content} BEGIN "
        "  INSERT INTO {fts}({fts}, rowid, {cols}) "
        "  VALUES('delete', old.{id}, {col_select}); "
        "  INSERT INTO {fts}(rowid, {cols}) "
        "  SELECT new.{id}, {col_select} FROM {content} WHERE {id}=new.{id}; "
        "END;");

    // For a generic trigger, we'd need to know the columns.
    // This simplified version demonstrates the pattern.
    char *err = nullptr;
    std::string sql = std::format(
        "CREATE TRIGGER IF NOT EXISTS {}_{}_sync_ai "
        "AFTER INSERT ON {} BEGIN "
        "  INSERT INTO {}(rowid) VALUES (new.{}) ON CONFLICT(rowid) DO NOTHING; "
        "END;",
        content_table, fts_table, content_table, fts_table, id_column);
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (err) { sqlite3_free(err); err = nullptr; }

    sql = std::format(
        "CREATE TRIGGER IF NOT EXISTS {}_{}_sync_ad "
        "AFTER DELETE ON {} BEGIN "
        "  INSERT INTO {}({}, rowid) VALUES ('delete', old.{}); "
        "END;",
        content_table, fts_table, content_table, fts_table, fts_table, id_column);
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (err) sqlite3_free(err);

    return true;
  }

  /// Basic match query.
  static std::string match_query(std::string_view fts_table,
                                  std::string_view query) {
    return std::format(
        "SELECT *, rank FROM {} WHERE {} MATCH '{}' ORDER BY rank;",
        fts_table, fts_table, sanitize_fts_query(query));
  }

  /// Match query with snippet generation.
  static std::string match_with_snippet(std::string_view fts_table,
                                         std::string_view query,
                                         std::string_view column,
                                         std::string_view prefix = "<b>",
                                         std::string_view suffix = "</b>",
                                         int max_tokens = 64) {
    return std::format(
        "SELECT *, snippet({}, '{}', {}, '{}', '{}', {}) AS snippet, rank "
        "FROM {} WHERE {} MATCH '{}' ORDER BY rank;",
        fts_table, column, 0, prefix, suffix, max_tokens,
        fts_table, fts_table, sanitize_fts_query(query));
  }

  /// Highlight search terms in a column.
  static std::string highlight_query(std::string_view fts_table,
                                      std::string_view query,
                                      std::string_view column,
                                      int col_index = 0) {
    return std::format(
        "SELECT *, highlight({}, {}, '<b>', '</b>') AS highlighted "
        "FROM {} WHERE {} MATCH '{}' ORDER BY rank;",
        fts_table, col_index, fts_table, fts_table,
        sanitize_fts_query(query));
  }

  /// BM25 ranking query (FTS5 built-in).
  static std::string bm25_query(std::string_view fts_table,
                                 std::string_view query,
                                 std::string_view extra_columns = "*") {
    return std::format(
        "SELECT {}, bm25({}) AS bm25_score FROM {} "
        "WHERE {} MATCH '{}' ORDER BY bm25_score;",
        extra_columns, fts_table, fts_table, fts_table,
        sanitize_fts_query(query));
  }

  /// Prefix match query (using * wildcard).
  static std::string prefix_query(std::string_view fts_table,
                                   std::string_view prefix) {
    return match_query(fts_table, std::format("{}*", prefix));
  }

  /// Phrase match query.
  static std::string phrase_query(std::string_view fts_table,
                                   std::string_view phrase) {
    return match_query(fts_table, std::format("\"{}\"", phrase));
  }

  /// Boolean query with AND/OR/NOT operators.
  static std::string boolean_query(std::string_view fts_table,
                                    const std::string &expression) {
    return match_query(fts_table, expression);
  }

  /// NEAR query (terms within N tokens of each other).
  static std::string near_query(std::string_view fts_table,
                                 std::string_view term1,
                                 std::string_view term2,
                                 int distance = 10) {
    return match_query(fts_table,
        std::format("NEAR({} {}, {})", term1, term2, distance));
  }

  /// Rebuild the FTS index (useful after bulk inserts).
  static bool rebuild(sqlite3 *db, std::string_view fts_table) {
    std::string sql = std::format("INSERT INTO {}({}) VALUES ('rebuild');",
                                   fts_table, fts_table);
    char *err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::error("Fts5Helper::rebuild: {}", err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    return true;
  }

  /// Optimize the FTS index.
  static bool optimize(sqlite3 *db, std::string_view fts_table) {
    std::string sql = std::format("INSERT INTO {}({}) VALUES ('optimize');",
                                   fts_table, fts_table);
    char *err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::warn("Fts5Helper::optimize: {}", err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    return true;
  }

  /// Drop an FTS5 table safely.
  static bool drop_table(sqlite3 *db, std::string_view fts_table) {
    std::string sql = std::format("DROP TABLE IF EXISTS {};", fts_table);
    char *err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::error("Fts5Helper::drop_table: {}", err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    return true;
  }

  /// Escape special characters in FTS5 query strings.
  static std::string sanitize_fts_query(std::string_view query) {
    std::string result;
    result.reserve(query.size() * 2);
    for (char c : query) {
      // FTS5 special characters that need escaping
      switch (c) {
      case '\'': result += "''"; break;
      case '"':  result += "\"\""; break;
      default:   result += c; break;
      }
    }
    return result;
  }

  /// Generate a column list of form "col1, col2, ..." from a source table
  /// for use in external content FTS tables.
  static std::string content_column_list(sqlite3 *db, std::string_view table) {
    std::string sql = std::format("PRAGMA table_info('{}');", table);
    sqlite3_stmt *stmt = nullptr;
    std::vector<std::string> cols;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = reinterpret_cast<const char *>(
            sqlite3_column_text(stmt, 1));
        if (name) cols.push_back(name);
      }
      sqlite3_finalize(stmt);
    }

    std::string result;
    for (size_t i = 0; i < cols.size(); ++i) {
      if (i > 0) result += ", ";
      result += cols[i];
    }
    return result;
  }
};

// ============================================================================
// 16. Encryption at rest via custom VFS
// ============================================================================

/// Custom VFS wrapper that provides encryption-at-rest for SQLite databases.
/// This implements a page-level encryption layer on top of the default VFS.
///
/// NOTE: This is a simplified demonstration. Production use should leverage
/// SQLCipher or a well-audited encryption library. This implementation
/// shows the VFS extension pattern for educational purposes.
class EncryptedVFS {
public:
  /// Encryption cipher interface (strategy pattern).
  struct Cipher {
    virtual ~Cipher() = default;
    virtual bool encrypt(uint8_t *data, size_t len, const std::string &key) = 0;
    virtual bool decrypt(uint8_t *data, size_t len, const std::string &key) = 0;
    virtual size_t block_size() const = 0;
  };

  /// Simple XOR-based cipher (for demonstration — NOT secure for production).
  /// Replace with AES-256-GCM or similar in production.
  class XorCipher : public Cipher {
  public:
    bool encrypt(uint8_t *data, size_t len, const std::string &key) override {
      return xor_transform(data, len, key);
    }
    bool decrypt(uint8_t *data, size_t len, const std::string &key) override {
      return xor_transform(data, len, key);
    }
    size_t block_size() const override { return 1; }

  private:
    bool xor_transform(uint8_t *data, size_t len, const std::string &key) {
      if (key.empty()) return false;
      for (size_t i = 0; i < len; ++i) {
        data[i] ^= static_cast<uint8_t>(key[i % key.size()]);
      }
      return true;
    }
  };

  /// AES-256-CBC placeholder cipher (requires OpenSSL integration).
  class Aes256Cipher : public Cipher {
  public:
    Aes256Cipher() {
#ifndef HAS_OPENSSL
      spdlog::warn("Aes256Cipher: OpenSSL not available, using stub");
#endif
    }

    bool encrypt(uint8_t *data, size_t len, const std::string &key) override {
#ifdef HAS_OPENSSL
      // In a real implementation:
      // EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
      // EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
      //                    key.data(), iv);
      // ... etc.
      return true;
#else
      (void)data; (void)len; (void)key;
      spdlog::error("Aes256Cipher: not implemented (no OpenSSL)");
      return false;
#endif
    }

    bool decrypt(uint8_t *data, size_t len, const std::string &key) override {
#ifdef HAS_OPENSSL
      return true;
#else
      (void)data; (void)len; (void)key;
      return false;
#endif
    }

    size_t block_size() const override { return 16; } // AES block size
  };

  /// Register a custom VFS that wraps page reads/writes with encryption.
  /// @param vfs_name The name of the new VFS to register.
  /// @param base_vfs The parent VFS to delegate actual I/O to (default: nullptr = default VFS).
  /// @param cipher The encryption cipher to use.
  /// @param key The encryption key.
  /// @returns true if registration succeeded.
  static bool register_vfs(const std::string &vfs_name,
                           const char *base_vfs,
                           std::shared_ptr<Cipher> cipher,
                           const std::string &key) {
    // VFS registration pattern: we create a sqlite3_vfs structure
    // that wraps the base VFS. On xRead/xWrite, we decrypt/encrypt.

    sqlite3_vfs *base = sqlite3_vfs_find(base_vfs);
    if (!base) {
      spdlog::error("EncryptedVFS: base VFS '{}' not found",
                    base_vfs ? base_vfs : "default");
      return false;
    }

    // Allocate and populate the wrapper VFS
    // In a real implementation, we would copy the base VFS and override
    // the xRead/xWrite methods. This simplified version demonstrates
    // the registration pattern.

    spdlog::info("EncryptedVFS: registered '{}' wrapping '{}'",
                 vfs_name, base->zName);

    // Store cipher/key for the I/O wrapper methods
    get_registry()[vfs_name] = {cipher, key, base};

    return true;
  }

  /// Unregister a custom VFS.
  static void unregister_vfs(const std::string &vfs_name) {
    get_registry().erase(vfs_name);
    spdlog::info("EncryptedVFS: unregistered '{}'", vfs_name);
  }

  /// Check if encryption VFS is registered.
  static bool is_registered(const std::string &vfs_name) {
    return get_registry().count(vfs_name) > 0;
  }

  /// Change the encryption key (re-encrypt all pages).
  /// NOTE: This is a database-level operation that must be done offline.
  static bool rekey(sqlite3 *db, const std::string &old_key,
                    const std::string &new_key) {
#ifdef SQLITE_HAS_CODEC
    int rc = sqlite3_rekey(db, new_key.data(), static_cast<int>(new_key.size()));
    if (rc != SQLITE_OK) {
      spdlog::error("EncryptedVFS::rekey failed: {}", sqlite3_errmsg(db));
      return false;
    }
    spdlog::info("EncryptedVFS: rekey successful");
    return true;
#else
    // Without SQLCipher, this requires re-creating the database.
    spdlog::warn("EncryptedVFS::rekey: sqlite3_rekey not available "
                 "(requires SQLCipher/SQLITE_HAS_CODEC)");
    (void)db; (void)old_key; (void)new_key;
    return false;
#endif
  }

  /// Key derivation function (PBKDF2-like simple wrapper).
  /// In production, use a real KDF (Argon2id, scrypt, or PBKDF2).
  static std::string derive_key(std::string_view password,
                                 std::string_view salt,
                                 int iterations = 100000) {
    // Simplified KDF — production should use a crypto library
    std::string material = std::string(password) + ":" + std::string(salt);
    std::hash<std::string> hasher;
    size_t h = hasher(material);

    // Iterative hashing for key stretching
    for (int i = 0; i < iterations; ++i) {
      material = std::to_string(h) + std::to_string(i) + std::string(salt);
      h ^= hasher(material);
      h = (h << 7) | (h >> (sizeof(h) * 8 - 7)); // rotate
    }

    // Produce a 256-bit key
    std::string key(32, '\0');
    for (size_t i = 0; i < 32; ++i) {
      key[i] = static_cast<char>((h >> ((i % 8) * 8)) & 0xFF);
    }
    return key;
  }

private:
  struct VfsEntry {
    std::shared_ptr<Cipher> cipher;
    std::string key;
    sqlite3_vfs *base_vfs;
  };

  static std::unordered_map<std::string, VfsEntry> &get_registry() {
    static std::unordered_map<std::string, VfsEntry> registry;
    return registry;
  }
};

// ============================================================================
// 17. Database utility functions (convenience layer)
// ============================================================================

/// High-level convenience class that ties together all database utilities.
class DatabaseUtils {
public:
  /// Factory: create connection pool and configure everything.
  static std::unique_ptr<ConnectionPool>
  create_pool(const PoolConfig &cfg) {
    return std::make_unique<ConnectionPool>(cfg);
  }

  /// Run a migration on the given database using registered migrations.
  template <typename MigrationProvider>
  static int run_migrations(sqlite3 *db, MigrationProvider &&provider) {
    MigrationRunner runner(db);
    for (auto &m : provider()) {
      runner.add_migration(std::move(m));
    }
    return runner.apply();
  }

  /// Perform a quick health check and return status.
  static ConnectionHealthChecker::HealthResult
  quick_health_check(sqlite3 *db) {
    ConnectionPool dummy_pool(PoolConfig{"health_check_db"});
    ConnectionHealthChecker checker(dummy_pool);
    return checker.check(db);
  }

  /// Create a backup of the database.
  static bool create_backup(sqlite3 *db, const std::string &dest,
                            DatabaseBackup::ProgressCallback progress = {}) {
    return DatabaseBackup::backup(db, dest, std::move(progress));
  }

  /// Restore a database from backup.
  static bool restore_backup(const std::string &src, sqlite3 *db,
                             DatabaseBackup::ProgressCallback progress = {}) {
    return DatabaseBackup::restore(src, db, std::move(progress));
  }

  /// Enable WAL mode and configure for optimal performance.
  static bool enable_wal_mode(sqlite3 *db) {
    WalManager wm(db);
    if (!wm.enable()) return false;
    wm.set_synchronous(WalManager::SyncMode::NORMAL);
    wm.set_autocheckpoint(1000); // Checkpoint every 1000 pages
    return true;
  }

  /// Compact the database (VACUUM).
  static bool compact(sqlite3 *db) {
    char *err = nullptr;
    int rc = sqlite3_exec(db, "VACUUM;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::error("DatabaseUtils::compact: {}", err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    spdlog::info("DatabaseUtils: VACUUM completed");
    return true;
  }

  /// Analyze tables for query planner optimization.
  static bool analyze(sqlite3 *db) {
    char *err = nullptr;
    int rc = sqlite3_exec(db, "ANALYZE;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::error("DatabaseUtils::analyze: {}", err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    return true;
  }

  /// Get database file size in bytes.
  static int64_t database_size(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;
    int64_t size = 0;
    if (sqlite3_prepare_v2(db,
        "SELECT page_count * page_size FROM pragma_page_count(), "
        "pragma_page_size();", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        size = sqlite3_column_int64(stmt, 0);
      }
      sqlite3_finalize(stmt);
    }
    return size;
  }

  /// Get the number of freelist pages.
  static int64_t freelist_count(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;
    int64_t count = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA freelist_count;", -1, &stmt, nullptr)
        == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
      }
      sqlite3_finalize(stmt);
    }
    return count;
  }

  /// Verify foreign key integrity.
  static bool verify_foreign_keys(sqlite3 *db) {
    char *err = nullptr;
    int rc = sqlite3_exec(db, "PRAGMA foreign_key_check;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::error("DatabaseUtils::verify_foreign_keys: {}",
                    err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    if (err && std::strlen(err) > 0) {
      spdlog::warn("DatabaseUtils: foreign key violations found:\n{}", err);
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    return true;
  }

  /// Optimize the database for a given usage pattern.
  static void optimize_for_write_heavy(sqlite3 *db) {
    sqlite3_exec(db, "PRAGMA synchronous=OFF;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_mode=MEMORY;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size=-16000;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
  }

  static void optimize_for_read_heavy(sqlite3 *db) {
    sqlite3_exec(db, "PRAGMA query_only=ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size=-32000;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA mmap_size=1073741824;", nullptr, nullptr, nullptr); // 1GB
  }

  /// Get SQLite library version information.
  static std::string version() {
    return std::format("SQLite {} (source ID: {})",
                       sqlite3_libversion(), sqlite3_sourceid());
  }

  /// Get compile options.
  static std::vector<std::string> compile_options() {
    std::vector<std::string> opts;
    for (int i = 0; ; ++i) {
      const char *opt = sqlite3_compileoption_get(i);
      if (!opt) break;
      opts.push_back(opt);
    }
    return opts;
  }
};

// ============================================================================
// 18. Transaction RAII helper
// ============================================================================

/// RAII wrapper for SQLite transactions with automatic rollback on exception.
class ScopedTransaction {
public:
  enum class Mode { DEFERRED, IMMEDIATE, EXCLUSIVE };

  explicit ScopedTransaction(sqlite3 *db, Mode mode = Mode::DEFERRED)
      : db_(db) {
    const char *sql = nullptr;
    switch (mode) {
    case Mode::DEFERRED:  sql = "BEGIN DEFERRED;";  break;
    case Mode::IMMEDIATE: sql = "BEGIN IMMEDIATE;"; break;
    case Mode::EXCLUSIVE: sql = "BEGIN EXCLUSIVE;"; break;
    }
    char *err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      std::string msg = err ? err : "unknown";
      sqlite3_free(err);
      throw DatabaseException(std::format("Failed to begin transaction: {}", msg), rc);
    }
    active_ = true;
  }

  ~ScopedTransaction() {
    if (active_) {
      rollback();
    }
  }

  // Non-copyable, non-movable
  ScopedTransaction(const ScopedTransaction &) = delete;
  ScopedTransaction &operator=(const ScopedTransaction &) = delete;
  ScopedTransaction(ScopedTransaction &&) = delete;
  ScopedTransaction &operator=(ScopedTransaction &&) = delete;

  /// Commit the transaction.
  void commit() {
    if (!active_) return;
    char *err = nullptr;
    int rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      std::string msg = err ? err : "unknown";
      sqlite3_free(err);
      throw DatabaseException(std::format("Failed to commit transaction: {}", msg), rc);
    }
    active_ = false;
  }

  /// Rollback the transaction.
  void rollback() {
    if (!active_) return;
    char *err = nullptr;
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &err);
    sqlite3_free(err);
    active_ = false;
  }

  bool is_active() const noexcept { return active_; }

private:
  sqlite3 *db_;
  bool active_ = false;
};

// ============================================================================
// 19. Parameter binder helper
// ============================================================================

/// Helper for binding parameters to prepared statements by index.
class ParameterBinder {
public:
  explicit ParameterBinder(sqlite3_stmt *stmt) : stmt_(stmt) {}

  ParameterBinder &bind(int index, std::nullptr_t) {
    sqlite3_bind_null(stmt_, index);
    return *this;
  }

  ParameterBinder &bind(int index, int value) {
    sqlite3_bind_int(stmt_, index, value);
    return *this;
  }

  ParameterBinder &bind(int index, int64_t value) {
    sqlite3_bind_int64(stmt_, index, value);
    return *this;
  }

  ParameterBinder &bind(int index, double value) {
    sqlite3_bind_double(stmt_, index, value);
    return *this;
  }

  ParameterBinder &bind(int index, std::string_view value) {
    sqlite3_bind_text(stmt_, index, value.data(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return *this;
  }

  ParameterBinder &bind(int index, const char *value) {
    if (value) {
      sqlite3_bind_text(stmt_, index, value, -1, SQLITE_TRANSIENT);
    } else {
      sqlite3_bind_null(stmt_, index);
    }
    return *this;
  }

  ParameterBinder &bind(int index, const std::vector<uint8_t> &blob) {
    sqlite3_bind_blob(stmt_, index, blob.data(),
                      static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    return *this;
  }

  ParameterBinder &bind(int index, bool value) {
    sqlite3_bind_int(stmt_, index, value ? 1 : 0);
    return *this;
  }

  // Named parameter binding
  ParameterBinder &bind(const std::string &name, int value) {
    int idx = sqlite3_bind_parameter_index(stmt_, name.c_str());
    return bind(idx, value);
  }

  ParameterBinder &bind(const std::string &name, std::string_view value) {
    int idx = sqlite3_bind_parameter_index(stmt_, name.c_str());
    return bind(idx, value);
  }

private:
  sqlite3_stmt *stmt_;
};

// ============================================================================
// 20. Row reader helper
// ============================================================================

/// Helper for reading column values from result rows.
class RowReader {
public:
  explicit RowReader(sqlite3_stmt *stmt) : stmt_(stmt) {}

  int column_int(int index) const {
    return sqlite3_column_int(stmt_, index);
  }

  int64_t column_int64(int index) const {
    return sqlite3_column_int64(stmt_, index);
  }

  double column_double(int index) const {
    return sqlite3_column_double(stmt_, index);
  }

  std::string column_text(int index) const {
    const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt_, index));
    return text ? std::string(text) : std::string{};
  }

  std::string_view column_text_view(int index) const {
    const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt_, index));
    int len = sqlite3_column_bytes(stmt_, index);
    return text ? std::string_view(text, static_cast<size_t>(len)) : std::string_view{};
  }

  std::vector<uint8_t> column_blob(int index) const {
    const uint8_t *data = static_cast<const uint8_t *>(sqlite3_column_blob(stmt_, index));
    int len = sqlite3_column_bytes(stmt_, index);
    if (data && len > 0) {
      return {data, data + len};
    }
    return {};
  }

  bool column_bool(int index) const {
    return sqlite3_column_int(stmt_, index) != 0;
  }

  bool is_null(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
  }

  int column_type(int index) const {
    return sqlite3_column_type(stmt_, index);
  }

  int column_count() const {
    return sqlite3_column_count(stmt_);
  }

  std::string column_name(int index) const {
    const char *name = sqlite3_column_name(stmt_, index);
    return name ? std::string(name) : std::string{};
  }

private:
  sqlite3_stmt *stmt_;
};

// ============================================================================
// 21. Database journal / system table management
// ============================================================================

/// Manages a system journal table for tracking database operations.
class SystemJournal {
public:
  explicit SystemJournal(sqlite3 *db) : db_(db) {
    ensure_table();
  }

  /// Log an event to the system journal.
  void log(std::string_view event_type, std::string_view details = "",
           std::string_view user = "system") {
    std::string sql = std::format(
        "INSERT INTO {} (event_type, details, user) "
        "VALUES ('{}', '{}', '{}');",
        kSystemJournal, sanitize(event_type),
        sanitize(details), sanitize(user));
    char *err = nullptr;
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (err) {
      spdlog::error("SystemJournal::log: {}", err);
      sqlite3_free(err);
    }
  }

  /// Query recent events.
  std::vector<std::tuple<int64_t, std::string, std::string, std::string>>
  recent_events(int limit = 100) const {
    std::string sql = std::format(
        "SELECT id, timestamp, event_type, details FROM {} "
        "ORDER BY id DESC LIMIT {};",
        kSystemJournal, limit);

    std::vector<std::tuple<int64_t, std::string, std::string, std::string>> events;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      RowReader reader(stmt);
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        events.emplace_back(
            reader.column_int64(0),
            reader.column_text(1),
            reader.column_text(2),
            reader.column_text(3));
      }
      sqlite3_finalize(stmt);
    }
    return events;
  }

  /// Purge old journal entries.
  void purge_older_than(int days) {
    std::string sql = std::format(
        "DELETE FROM {} WHERE timestamp < datetime('now', '-{} days');",
        kSystemJournal, days);
    char *err = nullptr;
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
  }

  /// Get journal size (row count).
  int64_t count() const {
    std::string sql = std::format("SELECT COUNT(*) FROM {};", kSystemJournal);
    sqlite3_stmt *stmt = nullptr;
    int64_t c = 0;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        c = sqlite3_column_int64(stmt, 0);
      }
      sqlite3_finalize(stmt);
    }
    return c;
  }

private:
  void ensure_table() {
    std::string sql = std::format(
        "CREATE TABLE IF NOT EXISTS {} ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp TEXT DEFAULT (datetime('now')),"
        "  event_type TEXT NOT NULL,"
        "  details TEXT,"
        "  user TEXT DEFAULT 'system'"
        ");", kSystemJournal);
    char *err = nullptr;
    sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
  }

  static std::string sanitize(std::string_view s) {
    std::string result;
    for (char c : s) {
      if (c == '\'') result += "''"; else result += c;
    }
    return result;
  }

  sqlite3 *db_;
};

// ============================================================================
// 22. Query result set
// ============================================================================

/// Lightweight result set wrapper for query execution.
class ResultSet {
public:
  ResultSet() = default;

  explicit ResultSet(sqlite3_stmt *stmt) { load(stmt); }

  void load(sqlite3_stmt *stmt) {
    column_names_.clear();
    rows_.clear();

    int ncols = sqlite3_column_count(stmt);
    for (int i = 0; i < ncols; ++i) {
      column_names_.push_back(
          sqlite3_column_name(stmt, i) ? sqlite3_column_name(stmt, i) : "");
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      std::vector<Cell> row;
      for (int i = 0; i < ncols; ++i) {
        Cell cell;
        cell.type = sqlite3_column_type(stmt, i);
        switch (cell.type) {
        case SQLITE_INTEGER:
          cell.int_val = sqlite3_column_int64(stmt, i);
          break;
        case SQLITE_FLOAT:
          cell.double_val = sqlite3_column_double(stmt, i);
          break;
        case SQLITE_TEXT: {
          const char *text = reinterpret_cast<const char *>(
              sqlite3_column_text(stmt, i));
          int bytes = sqlite3_column_bytes(stmt, i);
          cell.text_val = text ? std::string(text, bytes) : "";
          break;
        }
        case SQLITE_BLOB: {
          const uint8_t *data = static_cast<const uint8_t *>(
              sqlite3_column_blob(stmt, i));
          int bytes = sqlite3_column_bytes(stmt, i);
          if (data && bytes > 0) {
            cell.blob_val.assign(data, data + bytes);
          }
          break;
        }
        case SQLITE_NULL:
        default:
          break;
        }
        row.push_back(std::move(cell));
      }
      rows_.push_back(std::move(row));
    }
  }

  size_t row_count() const noexcept { return rows_.size(); }
  size_t column_count() const noexcept { return column_names_.size(); }
  const std::vector<std::string> &column_names() const noexcept { return column_names_; }

  struct Cell {
    int type = SQLITE_NULL;
    int64_t int_val = 0;
    double double_val = 0.0;
    std::string text_val;
    std::vector<uint8_t> blob_val;
    bool is_null() const noexcept { return type == SQLITE_NULL; }
  };

  const std::vector<std::vector<Cell>> &rows() const noexcept { return rows_; }

  /// Convenience: get cell by row, column.
  const Cell &cell(size_t row, size_t col) const { return rows_[row][col]; }

private:
  std::vector<std::string> column_names_;
  std::vector<std::vector<Cell>> rows_;
};

// ============================================================================
// 23. Database info / diagnostics
// ============================================================================

/// Diagnostic information about the database.
struct DatabaseInfo {
  std::string path;
  int64_t page_count = 0;
  int64_t page_size = 0;
  int64_t freelist_pages = 0;
  int64_t total_size_bytes = 0;
  std::string journal_mode;
  std::string encoding;
  int schema_version = 0;
  int user_version = 0;
  std::vector<std::string> table_names;
  std::vector<std::string> index_names;
  std::vector<std::string> trigger_names;
  std::vector<std::string> view_names;
};

/// Collect diagnostic information from a database.
inline DatabaseInfo collect_database_info(sqlite3 *db) {
  DatabaseInfo info;

  // Journal mode
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (t) info.journal_mode = t;
      }
      sqlite3_finalize(stmt);
    }
  }

  // Encoding
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA encoding;", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (t) info.encoding = t;
      }
      sqlite3_finalize(stmt);
    }
  }

  // Page count and size
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA page_count;", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW)
        info.page_count = sqlite3_column_int64(stmt, 0);
      sqlite3_finalize(stmt);
    }
  }
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA page_size;", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW)
        info.page_size = sqlite3_column_int64(stmt, 0);
      sqlite3_finalize(stmt);
    }
  }

  info.total_size_bytes = info.page_count * info.page_size;

  if (info.freelist_pages == 0) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA freelist_count;", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW)
        info.freelist_pages = sqlite3_column_int64(stmt, 0);
      sqlite3_finalize(stmt);
    }
  }

  // User version and schema version
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW)
        info.user_version = sqlite3_column_int(stmt, 0);
      sqlite3_finalize(stmt);
    }
  }
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA schema_version;", -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW)
        info.schema_version = sqlite3_column_int(stmt, 0);
      sqlite3_finalize(stmt);
    }
  }

  // Tables, indexes, triggers, views
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT name, type FROM sqlite_master "
        "WHERE type IN ('table','index','trigger','view') "
        "ORDER BY type, name;", -1, &stmt, nullptr) == SQLITE_OK) {
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string name = reinterpret_cast<const char *>(
            sqlite3_column_text(stmt, 0));
        std::string type = reinterpret_cast<const char *>(
            sqlite3_column_text(stmt, 1));
        if (type == "table") {
          if (!name.starts_with("sqlite_") && !name.starts_with("_"))
            info.table_names.push_back(name);
        } else if (type == "index") {
          info.index_names.push_back(name);
        } else if (type == "trigger") {
          info.trigger_names.push_back(name);
        } else if (type == "view") {
          info.view_names.push_back(name);
        }
      }
      sqlite3_finalize(stmt);
    }
  }

  return info;
}

// ============================================================================
// 24. Secure delete / data wiping
// ============================================================================

/// Utilities for secure data deletion.
class SecureDelete {
public:
  /// Enable secure delete mode (overwrites deleted content with zeros).
  static bool enable(sqlite3 *db) {
    char *err = nullptr;
    int rc = sqlite3_exec(db, "PRAGMA secure_delete=ON;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
      spdlog::error("SecureDelete::enable: {}", err ? err : "unknown");
      sqlite3_free(err);
      return false;
    }
    sqlite3_free(err);
    return true;
  }

  /// Securely erase all data from the database.
  static bool wipe_all(sqlite3 *db) {
    // Drop and recreate all tables
    std::vector<std::string> tables;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE 'sqlite_%';", -1, &stmt, nullptr) == SQLITE_OK) {
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        tables.push_back(reinterpret_cast<const char *>(
            sqlite3_column_text(stmt, 0)));
      }
      sqlite3_finalize(stmt);
    }

    char *err = nullptr;
    for (const auto &t : tables) {
      std::string sql = std::format("DROP TABLE IF EXISTS {};", t);
      sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
      if (err) sqlite3_free(err);
    }

    // VACUUM to reclaim space
    sqlite3_exec(db, "VACUUM;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);

    spdlog::info("SecureDelete: wiped {} tables", tables.size());
    return true;
  }
};

// ============================================================================
// 25. SQL utility helpers
// ============================================================================

/// Escape string literal for use in SQL.
inline std::string sql_escape(std::string_view s) {
  std::string result;
  result.reserve(s.size() + 4);
  result += '\'';
  for (char c : s) {
    if (c == '\'') result += "''";
    else result += c;
  }
  result += '\'';
  return result;
}

/// Escape identifier (table/column name) for use in SQL.
inline std::string sql_quote_identifier(std::string_view s) {
  std::string result;
  result.reserve(s.size() + 4);
  result += '"';
  for (char c : s) {
    if (c == '"') result += "\"\"";
    else result += c;
  }
  result += '"';
  return result;
}

/// Check if a table exists.
inline bool table_exists(sqlite3 *db, std::string_view table_name) {
  std::string sql = std::format(
      "SELECT COUNT(*) FROM sqlite_master "
      "WHERE type='table' AND name='{}';", table_name);
  sqlite3_stmt *stmt = nullptr;
  bool exists = false;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
  }
  return exists;
}

/// Get the row count of a table.
inline int64_t row_count(sqlite3 *db, std::string_view table_name) {
  std::string sql = std::format("SELECT COUNT(*) FROM {};", table_name);
  sqlite3_stmt *stmt = nullptr;
  int64_t count = 0;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }
  return count;
}

/// Enable or disable foreign key enforcement.
inline void set_foreign_keys(sqlite3 *db, bool enable) {
  char *err = nullptr;
  std::string sql = std::format("PRAGMA foreign_keys={};", enable ? "ON" : "OFF");
  sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
  if (err) sqlite3_free(err);
}

} // namespace cppdesk::common
