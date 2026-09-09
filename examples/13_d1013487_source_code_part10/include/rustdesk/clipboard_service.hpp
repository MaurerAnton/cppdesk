// clipboard_service.hpp — translation of src/server/clipboard_service.rs, part 1.
// Shell only (NAME + constructor). The poll loop (ClipboardContext +
// check_clipboard, both clipboard-step items) arrives with the clipboard step.

#pragma once

#include <rustdesk/service.hpp>

namespace rustdesk::clipboard_service {

// NAME re-export parity (common::CLIPBOARD_NAME).
inline constexpr char kName[] = "clipboard";

GenericService create();

}  // namespace rustdesk::clipboard_service
