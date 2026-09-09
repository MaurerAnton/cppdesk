// toml.hpp — minimal TOML subset for the config-file shapes in config.rs.
//
// confy persists Config/Config2/PeerConfig as TOML documents using only:
//   - `[table]` sections (single level: options, keys_confirmed, info)
//   - `key = value` with value := "basic string" | integer | bool | array
//     (arrays nest: key_pair, port_forwards, sizes)
//   - `#` comments
// This header implements exactly that subset — no floats, datetimes, dotted
// keys, or `[[array-of-tables]]`. Anything outside it is a loud parse error,
// so a future upstream format change fails visibly instead of corrupting.
// Cosmetic differences from confy output (key order, spacing) are
// TOML-semantics-neutral and documented in the step README.

#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace hbb_common::toml {

struct Value {
    using Array = std::vector<Value>;
    std::variant<std::string, int64_t, bool, Array> data;

    Value() : data(std::string()) {}
    Value(const char* s) : data(std::string(s)) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(int64_t i) : data(i) {}
    Value(int32_t i) : data(static_cast<int64_t>(i)) {}
    Value(bool b) : data(b) {}
    Value(Array a) : data(std::move(a)) {}

    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_int() const { return std::holds_alternative<int64_t>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_array() const { return std::holds_alternative<Array>(data); }
};

using Table = std::map<std::string, Value>;

// A document: root key-values plus named single-level tables.
struct Document {
    Table root;
    std::map<std::string, Table> tables;
};

// Throws TomlError (a std::runtime_error) on malformed input.
struct TomlError : std::runtime_error {
    explicit TomlError(const std::string& msg) : std::runtime_error(msg) {}
};

Document parse(const std::string& text);
std::string emit(const Document& doc);

}  // namespace hbb_common::toml
