// clipboard_service.cpp — see clipboard_service.hpp (shell; poll loop deferred).
#include <rustdesk/clipboard_service.hpp>

namespace rustdesk::clipboard_service {

GenericService create() {
    // need_snapshot=false parity; repeat(State, run) deferred (needs
    // ClipboardContext + check_clipboard from the clipboard step).
    return GenericService(kName, false);
}

}  // namespace rustdesk::clipboard_service
