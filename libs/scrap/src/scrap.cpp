#include "scrap/scrap.hpp"
#include <cstring>
namespace scrap {
bool is_x11() {
#ifdef __linux__
    const char* display = getenv("DISPLAY");
    return display != nullptr && getenv("WAYLAND_DISPLAY") == nullptr;
#else
    return false;
#endif
}
}