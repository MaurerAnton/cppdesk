// input_service.hpp — translation of src/server/input_service.rs, part 1.
// MouseCursorSub (cursor-id cache) is pure message logic and lands here in
// full; the capture/inject loops (enigo, platform cursor pos) and the key map
// arrive with the input step.

#pragma once

#include <rustdesk/service.hpp>

#include <cstdint>
#include <unordered_map>

namespace rustdesk::input_service {

inline constexpr char kNameCursor[] = "mouse_cursor";
inline constexpr char kNamePos[] = "mouse_pos";

// MouseCursorSub parity: forwards cursor_data once in full, then id-only
// (lets the client cache cursors by id).
struct MouseCursorSub {
    ConnInner inner;
    std::unordered_map<uint64_t, MessagePtr> cached;

    explicit MouseCursorSub(const ConnInner& conn) : inner(conn) {}
    int get_id() const { return inner.get_id(); }
    void send(const MessagePtr& msg);
};

using MouseCursorService = ServiceTmpl<MouseCursorSub>;

MouseCursorService new_cursor();
GenericService new_pos();

}  // namespace rustdesk::input_service
