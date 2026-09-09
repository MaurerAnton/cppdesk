// input_service.cpp — see input_service.hpp (part 1: cursor cache + shells).
#include <rustdesk/input_service.hpp>

namespace rustdesk::input_service {

void MouseCursorSub::send(const MessagePtr& msg) {
    if (msg->union_case() == hbb::Message::kCursorData) {
        const uint64_t id = msg->cursor_data().id();
        const auto it = cached.find(id);
        if (it != cached.end()) {
            inner.send(it->second);
        } else {
            inner.send(msg);
            hbb::Message tmp;
            // Only send id out; require client side cache also (upstream comment).
            tmp.set_cursor_id(id);
            cached.emplace(id, std::make_shared<hbb::Message>(std::move(tmp)));
        }
    } else {
        inner.send(msg);
    }
}

MouseCursorService new_cursor() {
    // need_snapshot=true parity; repeat(StateCursor, run_cursor) deferred
    // (needs platform cursor capture from the input step).
    return MouseCursorService(kNameCursor, true);
}

GenericService new_pos() {
    // need_snapshot=false parity; repeat(StatePos, run_pos) deferred.
    return GenericService(kNamePos, false);
}

}  // namespace rustdesk::input_service
