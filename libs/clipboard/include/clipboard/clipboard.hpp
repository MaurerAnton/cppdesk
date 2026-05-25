#pragma once
#include <string>
#include <vector>
namespace clipboard_lib {
class Clipboard { public: std::string get_text() { return ""; } void set_text(const std::string&) {} };
}