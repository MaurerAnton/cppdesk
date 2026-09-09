// audio_service.hpp — translation of src/server/audio_service.rs, part 1.
// Only the service shell (NAME + constructor) lands here. The capture +
// opus-encode loop (cpal/pulseaudio, magnum-opus) arrives with the audio step;
// until then new() builds the service WITHOUT starting its worker thread.

#pragma once

#include <rustdesk/service.hpp>

namespace rustdesk::audio_service {

inline constexpr char kName[] = "audio";

GenericService create();

}  // namespace rustdesk::audio_service
