#include "core/audio.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

constexpr int kSampleRate = eh::AUDIO_SAMPLE_RATE;
constexpr std::array<eh::EventType, 7> kEventTypes{
    eh::EventType::Shot,   eh::EventType::EggHit,     eh::EventType::EggDeath,
    eh::EventType::Pickup, eh::EventType::PlayerHurt, eh::EventType::Win,
    eh::EventType::Lose,
};

constexpr std::size_t samples_for_ms(int milliseconds) {
    return static_cast<std::size_t>(kSampleRate * milliseconds / 1000);
}

int amplitude(int16_t sample) {
    const int promoted = sample;
    return promoted < 0 ? -promoted : promoted;
}

// FNV-1a over explicitly little-endian sample bytes, so Windows and Linux must
// produce the same number rather than merely each being self-consistent.
std::uint64_t pcm_digest(const std::vector<int16_t> &samples) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const int16_t sample : samples) {
        const std::uint16_t bits = static_cast<std::uint16_t>(sample);
        for (int shift = 0; shift < 16; shift += 8) {
            hash ^= static_cast<std::uint8_t>(bits >> shift);
            hash *= 0x100000001b3ull;
        }
    }
    return hash;
}

const bool preinit_lookup_was_safe = [] {
    const eh::Pcm *const empty = &eh::sound_for(kEventTypes.front());
    if (!empty->samples.empty()) {
        return false;
    }
    for (const eh::EventType type : kEventTypes) {
        if (&eh::sound_for(type) != empty || !eh::sound_for(type).samples.empty()) {
            return false;
        }
    }
    return eh::sound_for(static_cast<eh::EventType>(255)).samples.empty();
}();

struct DurationExpectation {
    eh::EventType type;
    int minimum_ms;
    int maximum_ms;
};

constexpr std::array<DurationExpectation, 7> kDurationExpectations{{
    {eh::EventType::Shot, 100, 140},
    {eh::EventType::EggHit, 35, 70},
    {eh::EventType::EggDeath, 250, 350},
    {eh::EventType::Pickup, 200, 300},
    {eh::EventType::PlayerHurt, 150, 250},
    {eh::EventType::Win, 250, 500},
    {eh::EventType::Lose, 300, 500},
}};

// Zero-crossing rate is a cheap dominant-pitch proxy for these square-wave tones:
// the faster the waveform flips sign, the higher it sounds.
double zero_crossing_rate(const std::vector<int16_t> &samples, std::size_t begin, std::size_t end) {
    if (end <= begin + 1) {
        return 0.0;
    }
    std::size_t crossings = 0;
    for (std::size_t i = begin + 1; i < end; ++i) {
        if ((samples[i - 1] >= 0) != (samples[i] >= 0)) {
            ++crossings;
        }
    }
    return static_cast<double>(crossings) / static_cast<double>(end - begin);
}

// Ratio of ending pitch to starting pitch. Above 1 the sound rises, below 1 it falls.
double pitch_direction(eh::EventType type) {
    const std::vector<int16_t> &samples = eh::sound_for(type).samples;
    const std::size_t quarter = samples.size() / 4;
    const double opening = zero_crossing_rate(samples, 0, quarter);
    const double closing = zero_crossing_rate(samples, samples.size() - quarter, samples.size());
    return opening > 0.0 ? closing / opening : 0.0;
}

// mvp.md's sound design: good news rises, bad news falls.
constexpr std::array<eh::EventType, 2> kRisingEvents{eh::EventType::Pickup, eh::EventType::Win};
constexpr std::array<eh::EventType, 5> kFallingEvents{
    eh::EventType::Shot,       eh::EventType::EggHit, eh::EventType::EggDeath,
    eh::EventType::PlayerHurt, eh::EventType::Lose,
};

// The multi-note fanfares, and how many notes each is built from.
struct NoteSequence {
    eh::EventType type;
    std::size_t notes;
};

constexpr std::array<NoteSequence, 2> kNoteSequences{{
    {eh::EventType::Pickup, 3},
    {eh::EventType::Win, 4},
}};

double mean_magnitude(const std::vector<int16_t> &samples, std::size_t begin, std::size_t end) {
    if (end <= begin) {
        return 0.0;
    }
    double total = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        total += amplitude(samples[i]);
    }
    return total / static_cast<double>(end - begin);
}

int peak_magnitude(const std::vector<int16_t> &samples, std::size_t begin, std::size_t end) {
    int peak = 0;
    for (std::size_t i = begin; i < end; ++i) {
        peak = std::max(peak, amplitude(samples[i]));
    }
    return peak;
}

} // namespace

TEST_CASE("audio: sound lookup is safe before initialization") { REQUIRE(preinit_lookup_was_safe); }

TEST_CASE("audio: every event maps to its own distinct sound") {
    eh::init_audio_bank();

    // A duplicated arm in sound_for() makes two events share one buffer: the game plays
    // the wrong sound with every other audio property still perfectly satisfied.
    for (std::size_t i = 0; i < kEventTypes.size(); ++i) {
        for (std::size_t j = i + 1; j < kEventTypes.size(); ++j) {
            CAPTURE(static_cast<int>(kEventTypes[i]), static_cast<int>(kEventTypes[j]));
            REQUIRE(&eh::sound_for(kEventTypes[i]) != &eh::sound_for(kEventTypes[j]));
            REQUIRE(eh::sound_for(kEventTypes[i]).samples != eh::sound_for(kEventTypes[j]).samples);
        }
    }
}

TEST_CASE("audio: good news rises in pitch and bad news falls") {
    eh::init_audio_bank();

    // Duration bounds cannot separate Win from Lose - their ranges overlap - so swapping
    // them plays defeat over the victory screen with every other test still green.
    double quietest_rise = std::numeric_limits<double>::max();
    for (const eh::EventType type : kRisingEvents) {
        const double ratio = pitch_direction(type);
        CAPTURE(static_cast<int>(type), ratio);
        REQUIRE(ratio > 1.2);
        quietest_rise = std::min(quietest_rise, ratio);
    }

    double steepest_fall = 0.0;
    for (const eh::EventType type : kFallingEvents) {
        const double ratio = pitch_direction(type);
        CAPTURE(static_cast<int>(type), ratio);
        REQUIRE(ratio < 0.85);
        steepest_fall = std::max(steepest_fall, ratio);
    }

    // Relational form as well as absolute, so the two families cannot drift together.
    REQUIRE(quietest_rise > steepest_fall);
}

TEST_CASE("audio: every note in a fanfare is separately articulated") {
    eh::init_audio_bank();

    // Whole-sound measures cannot see this. Enveloping the whole buffer once instead of
    // once per note leaves the crest factor unchanged (2.23 vs 2.22) and still ends
    // quiet, so both the crest guard and the decay test pass while Pickup and Win
    // degrade into one sustained square-wave blare.
    for (const NoteSequence &sequence : kNoteSequences) {
        const std::vector<int16_t> &samples = eh::sound_for(sequence.type).samples;
        CAPTURE(static_cast<int>(sequence.type), sequence.notes, samples.size());

        int loudest_note = 0;
        for (std::size_t note = 0; note < sequence.notes; ++note) {
            const std::size_t begin = note * samples.size() / sequence.notes;
            const std::size_t end = (note + 1) * samples.size() / sequence.notes;
            loudest_note = std::max(loudest_note, peak_magnitude(samples, begin, end));
        }
        REQUIRE(loudest_note > 0);

        for (std::size_t note = 0; note < sequence.notes; ++note) {
            const std::size_t begin = note * samples.size() / sequence.notes;
            const std::size_t end = (note + 1) * samples.size() / sequence.notes;
            const std::size_t length = end - begin;
            CAPTURE(note);

            // Every note is struck at full force. Under one shared envelope the later
            // notes fade out instead: Win's last note peaked at 1068 against 17000.
            const int note_peak = peak_magnitude(samples, begin, end);
            CAPTURE(note_peak, loudest_note);
            REQUIRE(note_peak * 2 > loudest_note);

            // Every note releases to near silence before the next, which is what makes
            // the sequence read as separate notes. Measured 0.002 here, 0.59 sustained.
            const double body = mean_magnitude(samples, begin + length / 4, begin + length / 2);
            const double tail = mean_magnitude(samples, end - length / 20, end);
            CAPTURE(body, tail);
            REQUIRE(body > 0.0);
            REQUIRE(tail < body / 10.0);
        }
    }
}

TEST_CASE("audio: every event has synthesized PCM") {
    eh::init_audio_bank();

    for (const eh::EventType type : kEventTypes) {
        CAPTURE(static_cast<int>(type));
        const eh::Pcm &sound = eh::sound_for(type);
        REQUIRE_FALSE(sound.samples.empty());
        REQUIRE(&eh::sound_for(type) == &sound);
    }
}

// Named for what it actually proves. The pointer-identity assertion below is the
// real content: a second init_audio_bank() must not regenerate or reallocate the
// bank. The memcmp that follows is therefore comparing the bank against a
// snapshot of itself and cannot fail -- reproducibility is proved by the golden
// digests further down, not here.
TEST_CASE("audio: initialization is idempotent and does not reallocate") {
    eh::init_audio_bank();

    std::array<std::vector<int16_t>, kEventTypes.size()> snapshots;
    std::array<const int16_t *, kEventTypes.size()> allocations{};
    for (std::size_t i = 0; i < kEventTypes.size(); ++i) {
        const auto &samples = eh::sound_for(kEventTypes[i]).samples;
        snapshots[i] = samples;
        allocations[i] = samples.data();
    }

    eh::init_audio_bank();

    for (std::size_t i = 0; i < kEventTypes.size(); ++i) {
        CAPTURE(i);
        const auto &samples = eh::sound_for(kEventTypes[i]).samples;
        REQUIRE(samples.data() == allocations[i]);
        REQUIRE(samples.size() == snapshots[i].size());
        REQUIRE(std::memcmp(samples.data(), snapshots[i].data(),
                            samples.size() * sizeof(int16_t)) == 0);
    }
}

// The memcmp in the test above proves idempotence, not reproducibility, and the
// distinction is not academic. Because init_audio_bank() is guarded, the second
// call is a no-op and that memcmp compares the bank against a snapshot of
// itself -- it cannot fail for any generator, deterministic or not. Replacing
// the noise seed with a completely different constant, which changes the actual
// waveform of every noise-based sound, was verified to pass the entire suite
// before these digests existed.
//
// These goldens are what make "byte deterministic" true: a fixed expected value
// that must hold in every process, on every platform, so CI's Linux leg turns
// the cross-platform determinism claim into an assertion. A failure here after a
// deliberate audio change is expected and correct -- re-derive and update.
TEST_CASE("audio: the synthesized bank is byte-for-byte reproducible") {
    eh::init_audio_bank();

    constexpr std::array<std::uint64_t, kEventTypes.size()> golden{
        0x184674438b3daf45ull, // Shot
        0xf6657813fe14fd56ull, // EggHit
        0x2d887fe771ec2705ull, // EggDeath
        0x387aeb30a0742241ull, // Pickup
        0xa057e37ea460bdd8ull, // PlayerHurt
        0x7f1da51e1d91213cull, // Win
        0x4d888bd07d05d192ull, // Lose
    };

    for (std::size_t i = 0; i < kEventTypes.size(); ++i) {
        CAPTURE(i);
        CHECK(pcm_digest(eh::sound_for(kEventTypes[i]).samples) == golden[i]);
    }
}

TEST_CASE("audio: sounds have bounded duration and headroom") {
    eh::init_audio_bank();

    for (const DurationExpectation expectation : kDurationExpectations) {
        CAPTURE(static_cast<int>(expectation.type));
        const auto &samples = eh::sound_for(expectation.type).samples;
        REQUIRE(samples.size() >= samples_for_ms(expectation.minimum_ms));
        REQUIRE(samples.size() <= samples_for_ms(expectation.maximum_ms));

        int peak = 0;
        for (const int16_t sample : samples) {
            const int promoted = sample;
            REQUIRE(promoted > std::numeric_limits<int16_t>::min());
            REQUIRE(promoted < std::numeric_limits<int16_t>::max());
            peak = std::max(peak, amplitude(sample));
        }
        REQUIRE(peak >= 15000);
        REQUIRE(peak <= 20000);
    }
}

TEST_CASE("audio: every sound has meaningful dynamic shape") {
    eh::init_audio_bank();

    for (const eh::EventType type : kEventTypes) {
        CAPTURE(static_cast<int>(type));
        const auto &samples = eh::sound_for(type).samples;
        int peak = 0;
        int64_t squared_sum = 0;
        for (const int16_t sample : samples) {
            const int magnitude = amplitude(sample);
            peak = std::max(peak, magnitude);
            squared_sum += static_cast<int64_t>(magnitude) * magnitude;
        }

        const int64_t scaled_peak_energy =
            4 * static_cast<int64_t>(peak) * peak * static_cast<int64_t>(samples.size());
        REQUIRE(scaled_peak_energy >= 9 * squared_sum);
    }
}

TEST_CASE("audio: envelopes keep every start and end quiet") {
    eh::init_audio_bank();

    for (const eh::EventType type : kEventTypes) {
        CAPTURE(static_cast<int>(type));
        const auto &samples = eh::sound_for(type).samples;
        REQUIRE(samples.front() == 0);
        REQUIRE(samples.back() == 0);

        const std::size_t edge_length = std::min<std::size_t>(16, samples.size());
        int leading_peak = 0;
        int trailing_peak = 0;
        for (std::size_t i = 0; i < edge_length; ++i) {
            leading_peak = std::max(leading_peak, amplitude(samples[i]));
            trailing_peak = std::max(trailing_peak, amplitude(samples[samples.size() - 1 - i]));
        }
        REQUIRE(leading_peak < 8000);
        REQUIRE(trailing_peak < 8000);
    }
}

TEST_CASE("audio: every sound materially decays across its duration") {
    eh::init_audio_bank();

    for (const eh::EventType type : kEventTypes) {
        CAPTURE(static_cast<int>(type));
        const auto &samples = eh::sound_for(type).samples;
        const std::size_t window = samples.size() / 10;
        int64_t starting_amplitude = 0;
        int64_t ending_amplitude = 0;
        for (std::size_t i = 0; i < window; ++i) {
            starting_amplitude += amplitude(samples[i]);
            ending_amplitude += amplitude(samples[samples.size() - window + i]);
        }

        REQUIRE(starting_amplitude > 0);
        REQUIRE(ending_amplitude * 10 < starting_amplitude);
    }
}

TEST_CASE("audio: every sound is centered around zero") {
    eh::init_audio_bank();

    for (const eh::EventType type : kEventTypes) {
        CAPTURE(static_cast<int>(type));
        const auto &samples = eh::sound_for(type).samples;
        int64_t sum = 0;
        for (const int16_t sample : samples) {
            sum += sample;
        }
        const int64_t absolute_sum = sum < 0 ? -sum : sum;
        REQUIRE(absolute_sum < static_cast<int64_t>(samples.size()) * 32);
    }
}
