#pragma once

#include "core/events.h"

#include <cstdint>
#include <vector>

namespace eh {

struct Pcm {
    std::vector<int16_t> samples;
};

void init_audio_bank();
const Pcm &sound_for(EventType);

} // namespace eh
