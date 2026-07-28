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

constexpr int kSampleRate = 44100;
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

} // namespace

TEST_CASE("audio: sound lookup is safe before initialization") { REQUIRE(preinit_lookup_was_safe); }

TEST_CASE("audio: every event has synthesized PCM") {
    eh::init_audio_bank();

    for (const eh::EventType type : kEventTypes) {
        CAPTURE(static_cast<int>(type));
        const eh::Pcm &sound = eh::sound_for(type);
        REQUIRE_FALSE(sound.samples.empty());
        REQUIRE(&eh::sound_for(type) == &sound);
    }
}

TEST_CASE("audio: initialization is idempotent and byte deterministic") {
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
