// video_service.cpp — see video_service.hpp (shell; capture loop deferred).
#include <rustdesk/video_service.hpp>

namespace rustdesk::video_service {

GenericService create() {
    // need_snapshot=true parity; run() body (scrap capture + vpx) deferred.
    return GenericService(kName, true);
}

}  // namespace rustdesk::video_service
