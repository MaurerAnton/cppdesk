// toml.cpp — see toml.hpp.
#include <hbb_common/toml.hpp>

#include <cctype>
#include <cstdio>
#include <string>

namespace hbb_common::toml {
namespace {

std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::string strip_comment(const std::string& line) {
    bool in_str = false;
    char quote = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_str) {
            if (c == '\\') {
                ++i;
            } else if (c == quote) {
                in_str = false;
            }
        } else if (c == '"' || c == '\'') {
            in_str = true;
            quote = c;
        } else if (c == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

uint32_t hex4(const std::string& s, size_t& i) {
    if (i + 4 > s.size()) {
        throw TomlError(std::string("bad \\u escape"));
    }
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
        const char c = s[i++];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= static_cast<uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= static_cast<uint32_t>(c - 'A' + 10);
        } else {
            throw TomlError(std::string("bad \\u escape"));
        }
    }
    return v;
}

void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        ++i;
    }
}

Value parse_value(const std::string& s, size_t& i);

Value parse_string(const std::string& s, size_t& i) {
    const char quote = s[i++];
    std::string out;
    if (quote == '\'') {
        while (true) {
            if (i >= s.size()) {
                throw TomlError(std::string("unterminated literal string"));
            }
            if (s[i] == '\'') {
                ++i;
                break;
            }
            out += s[i++];
        }
        return Value(out);
    }
    while (true) {
        if (i >= s.size()) {
            throw TomlError(std::string("unterminated basic string"));
        }
        const char c = s[i++];
        if (c == '"') {
            break;
        }
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= s.size()) {
            throw TomlError(std::string("bad escape"));
        }
        switch (s[i++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': append_utf8(out, hex4(s, i)); break;
            case 'U': {
                const uint32_t hi = hex4(s, i);
                const uint32_t lo = hex4(s, i);
                append_utf8(out, (hi << 16) | lo);
                break;
            }
            default: throw TomlError(std::string("bad escape"));
        }
    }
    return Value(out);
}

Value parse_array(const std::string& s, size_t& i) {
    ++i;  // consume '['
    Value::Array items;
    while (true) {
        skip_ws(s, i);
        if (i >= s.size()) {
            throw TomlError(std::string("unterminated array"));
        }
        if (s[i] == ']') {
            ++i;
            break;
        }
        items.push_back(parse_value(s, i));
        skip_ws(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            continue;
        }
        skip_ws(s, i);
        if (i < s.size() && s[i] == ']') {
            ++i;
            break;
        }
        throw TomlError(std::string("expected ',' or ']' in array"));
    }
    return Value(std::move(items));
}

Value parse_value(const std::string& s, size_t& i) {
    skip_ws(s, i);
    if (i >= s.size()) {
        throw TomlError(std::string("expected value"));
    }
    if (s[i] == '"' || s[i] == '\'') {
        return parse_string(s, i);
    }
    if (s[i] == '[') {
        return parse_array(s, i);
    }
    size_t j = i;
    while (j < s.size() && s[j] != ',' && s[j] != ']' && s[j] != '#' && s[j] != '\n') {
        ++j;
    }
    const std::string tok = trim(s.substr(i, j - i));
    i = j;
    if (tok == "true") {
        return Value(true);
    }
    if (tok == "false") {
        return Value(false);
    }
    if (!tok.empty()) {
        size_t k = (tok[0] == '+' || tok[0] == '-') ? 1 : 0;
        bool digits = k < tok.size();
        for (; k < tok.size(); ++k) {
            if (tok[k] < '0' || tok[k] > '9') {
                digits = false;
                break;
            }
        }
        if (digits) {
            try {
                return Value(static_cast<int64_t>(std::stoll(tok)));
            } catch (...) {
                throw TomlError(std::string("integer out of range: ") + tok);
            }
        }
    }
    throw TomlError(std::string("unsupported value: ") + tok);
}

Value parse_value(const std::string& s) {
    size_t i = 0;
    return parse_value(s, i);
}

void emit_value(std::string& out, const Value& v);

void emit_string(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04X", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void emit_value(std::string& out, const Value& v) {
    if (v.is_string()) {
        emit_string(out, std::get<std::string>(v.data));
    } else if (v.is_int()) {
        out += std::to_string(std::get<int64_t>(v.data));
    } else if (v.is_bool()) {
        out += std::get<bool>(v.data) ? "true" : "false";
    } else {
        out += '[';
        bool first = true;
        for (const auto& item : std::get<Value::Array>(v.data)) {
            if (!first) {
                out += ", ";
            }
            first = false;
            emit_value(out, item);
        }
        out += ']';
    }
}

}  // namespace

Document parse(const std::string& text) {
    Document doc;
    Table* cur = &doc.root;

    std::vector<std::string> logical;
    {
        std::string acc;
        int depth = 0;
        bool in_str = false;
        char quote = 0;
        auto flush = [&] { logical.push_back(acc); acc.clear(); };
        for (size_t i = 0; i <= text.size(); ++i) {
            const char c = i < text.size() ? text[i] : '\n';
            if (c == '\n' && !in_str && depth == 0) {
                flush();
                continue;
            }
            acc += c;
            if (in_str) {
                if (c == '\\' && i + 1 < text.size()) {
                    acc += text[++i];
                } else if (c == quote) {
                    in_str = false;
                }
            } else if (c == '"' || c == '\'') {
                in_str = true;
                quote = c;
            } else if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
            }
        }
    }

    for (const std::string& raw : logical) {
        const std::string line = trim(strip_comment(raw));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            if (line.back() != ']') {
                throw TomlError(std::string("bad table header: ") + line);
            }
            const std::string name = trim(line.substr(1, line.size() - 2));
            if (name.empty() || name.find('.') != std::string::npos ||
                name.find('[') != std::string::npos) {
                throw TomlError(std::string("unsupported table (no dotted/array tables): ") + line);
            }
            cur = &doc.tables[name];
            continue;
        }
        size_t eq = std::string::npos;
        {
            bool in_str = false;
            char quote = 0;
            for (size_t i = 0; i < line.size(); ++i) {
                const char c = line[i];
                if (in_str) {
                    if (c == '\\') {
                        ++i;
                    } else if (c == quote) {
                        in_str = false;
                    }
                } else if (c == '"' || c == '\'') {
                    in_str = true;
                    quote = c;
                } else if (c == '=') {
                    eq = i;
                    break;
                }
            }
        }
        if (eq == std::string::npos) {
            throw TomlError(std::string("expected key = value: ") + line);
        }
        const std::string key = trim(line.substr(0, eq));
        if (key.empty()) {
            throw TomlError(std::string("empty key: ") + line);
        }
        size_t pos = eq + 1;
        const Value v = parse_value(line, pos);
        skip_ws(line, pos);
        if (pos != line.size()) {
            throw TomlError(std::string("trailing characters: ") + line);
        }
        if (!cur->emplace(key, v).second) {
            throw TomlError(std::string("duplicate key: ") + key);
        }
    }
    return doc;
}

std::string emit(const Document& doc) {
    std::string out;
    for (const auto& [key, v] : doc.root) {
        out += key + " = ";
        emit_value(out, v);
        out += '\n';
    }
    for (const auto& [name, table] : doc.tables) {
        if (table.empty()) {
            continue;
        }
        out += '\n';
        out += '[' + name + "]\n";
        for (const auto& [key, v] : table) {
            out += key + " = ";
            emit_value(out, v);
            out += '\n';
        }
    }
    return out;
}

}  // namespace hbb_common::toml