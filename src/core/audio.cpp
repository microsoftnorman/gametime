#include "core/audio.h"

namespace eh {

void init_audio_bank() {}

const Pcm &sound_for(EventType) {
    static const Pcm silence;
    return silence;
}

} // namespace eh
