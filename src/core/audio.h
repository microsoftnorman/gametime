#pragma once

#include "core/events.h"

#include <cstdint>
#include <vector>

namespace eh {

struct Pcm {
    std::vector<int16_t> samples;
};

// Every sample in the bank is synthesized at this rate, and whoever hands the buffers to an
// audio device has to declare the same number. It used to be written out three times -- here in
// the synthesizer, again in the app's Wave descriptor, and again in the tests -- with nothing
// tying them together and no test linking the app at all. Changing the app's copy alone shifted
// the whole game an octave with 95/95 green.
constexpr int AUDIO_SAMPLE_RATE = 44100;

// The other two halves of the same format contract. Exported so the code that hands these
// buffers to an audio device has no free numbers left to get wrong, and so the bit depth is
// checked against the type the samples are actually stored in rather than assumed.
constexpr int AUDIO_BITS_PER_SAMPLE = 16;
constexpr int AUDIO_CHANNELS = 1;
static_assert(AUDIO_BITS_PER_SAMPLE == sizeof(int16_t) * 8,
              "Pcm stores int16_t samples; the declared bit depth must match the storage type");

void init_audio_bank();
const Pcm &sound_for(EventType);

} // namespace eh
