// video_service.hpp — translation of src/server/video_service.rs, part 1.
// Shell only (NAME + constructor). Capture + vpx-encode loop (scrap) and the
// latency/quality maps arrive with the video step.

#pragma once

#include <rustdesk/service.hpp>

namespace rustdesk::video_service {

inline constexpr char kName[] = "video";

GenericService create();

}  // namespace rustdesk::video_service
