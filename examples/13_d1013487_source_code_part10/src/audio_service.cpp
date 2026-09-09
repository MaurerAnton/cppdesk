// audio_service.cpp — see audio_service.hpp (shell; capture loop deferred).
#include <rustdesk/audio_service.hpp>

namespace rustdesk::audio_service {

GenericService create() {
    // need_snapshot=true parity (non-Linux cpal path uses repeat(); the Linux
    // pa_impl path uses run() — both bodies deferred to the audio step).
    // No worker started until then.
    return GenericService(kName, true);
}

}  // namespace rustdesk::audio_service
