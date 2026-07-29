#include "core/audio.h"
#include "core/rng.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace eh {
namespace {

constexpr int32_t kUnitGain = 32767;
constexpr std::size_t kSoundCount = 7;

using WorkingSamples = std::vector<int32_t>;

std::array<Pcm, kSoundCount> audio_bank;
bool audio_bank_initialized = false;

constexpr std::size_t samples_for_ms(int milliseconds) {
    return static_cast<std::size_t>(AUDIO_SAMPLE_RATE * milliseconds / 1000);
}

uint32_t phase_step(int frequency_hz) {
    return static_cast<uint32_t>((static_cast<uint64_t>(frequency_hz) << 32) / AUDIO_SAMPLE_RATE);
}

int interpolate(int start, int end, std::size_t index, std::size_t count) {
    if (count < 2) {
        return end;
    }
    const int64_t difference = static_cast<int64_t>(end) - start;
    return start + static_cast<int>(difference * static_cast<int64_t>(index) /
                                    static_cast<int64_t>(count - 1));
}

int32_t square_wave(uint32_t phase) { return (phase & 0x80000000u) != 0 ? kUnitGain : -kUnitGain; }

int32_t triangle_wave(uint32_t phase) {
    const uint32_t position = phase >> 16;
    if (position < 32768u) {
        return static_cast<int32_t>(position * 2u) - kUnitGain;
    }
    return 98303 - static_cast<int32_t>(position * 2u);
}

int32_t noise_sample(Rng &rng) { return static_cast<int32_t>(next(rng) >> 16) - 32768; }

int32_t scale_sample(int32_t sample, int32_t gain) {
    return static_cast<int32_t>(static_cast<int64_t>(sample) * gain / kUnitGain);
}

int32_t ratio_gain(std::size_t numerator, std::size_t denominator) {
    if (denominator == 0) {
        return kUnitGain;
    }
    return static_cast<int32_t>(static_cast<uint64_t>(numerator) * kUnitGain / denominator);
}

int32_t decay_gain(std::size_t index, std::size_t count) {
    if (count < 2) {
        return 0;
    }
    return ratio_gain(count - 1 - index, count - 1);
}

int32_t edge_gain(std::size_t index, std::size_t count, std::size_t attack, std::size_t release) {
    int32_t gain = kUnitGain;
    if (attack > 1 && index < attack) {
        gain = std::min(gain, ratio_gain(index, attack - 1));
    }

    const std::size_t remaining = count - 1 - index;
    if (release > 1 && remaining < release) {
        gain = std::min(gain, ratio_gain(remaining, release - 1));
    }
    return gain;
}

void apply_edge_envelope(WorkingSamples &samples, int attack_ms, int release_ms) {
    const std::size_t attack = std::min(samples_for_ms(attack_ms), samples.size());
    const std::size_t release = std::min(samples_for_ms(release_ms), samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = scale_sample(samples[i], edge_gain(i, samples.size(), attack, release));
    }
}

void remove_dc_offset(WorkingSamples &samples) {
    if (samples.size() <= 2) {
        return;
    }

    int64_t sum = 0;
    for (const int32_t sample : samples) {
        sum += sample;
    }
    const int32_t offset = static_cast<int32_t>(sum / static_cast<int64_t>(samples.size() - 2));
    for (std::size_t i = 1; i + 1 < samples.size(); ++i) {
        samples[i] -= offset;
    }
}

Pcm finish_sound(WorkingSamples samples, int attack_ms, int release_ms, int target_peak) {
    apply_edge_envelope(samples, attack_ms, release_ms);
    remove_dc_offset(samples);

    int64_t peak = 0;
    for (const int32_t sample : samples) {
        const int64_t magnitude =
            sample < 0 ? -static_cast<int64_t>(sample) : static_cast<int64_t>(sample);
        peak = std::max(peak, magnitude);
    }

    Pcm result;
    result.samples.resize(samples.size());
    if (peak == 0) {
        return result;
    }

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const int64_t normalized = static_cast<int64_t>(samples[i]) * target_peak / peak;
        result.samples[i] = static_cast<int16_t>(std::clamp(
            normalized, -static_cast<int64_t>(target_peak), static_cast<int64_t>(target_peak)));
    }
    return result;
}

Pcm make_shot() {
    const std::size_t count = samples_for_ms(120);
    const std::size_t noise_count = samples_for_ms(40);
    WorkingSamples samples(count);
    Rng rng{0x91e10da5u};
    uint32_t phase = 0;

    for (std::size_t i = 0; i < count; ++i) {
        const int frequency = interpolate(1050, 170, i, count);
        const int32_t chirp_gain = scale_sample(decay_gain(i, count), 22000);
        const int32_t chirp = scale_sample(square_wave(phase), chirp_gain);
        phase += phase_step(frequency);

        int32_t noise = 0;
        if (i < noise_count) {
            const int32_t burst_gain = scale_sample(decay_gain(i, noise_count), 27000);
            noise = scale_sample(noise_sample(rng), burst_gain);
        }
        samples[i] = chirp + noise;
    }

    return finish_sound(std::move(samples), 1, 8, 19000);
}

Pcm make_egg_hit() {
    const std::size_t count = samples_for_ms(50);
    WorkingSamples samples(count);
    Rng rng{0x6c8e9cf5u};
    int32_t filtered = 0;

    for (std::size_t i = 0; i < count; ++i) {
        filtered += (noise_sample(rng) - filtered) / 5;
        const int32_t linear = decay_gain(i, count);
        const int32_t fast_decay = scale_sample(linear, linear);
        samples[i] = scale_sample(filtered, fast_decay);
    }

    return finish_sound(std::move(samples), 1, 6, 17000);
}

Pcm make_egg_death() {
    const std::size_t count = samples_for_ms(300);
    WorkingSamples samples(count);
    Rng rng{0x4f1bbcdcu};
    uint32_t phase = 0;
    int32_t filtered_noise = 0;

    for (std::size_t i = 0; i < count; ++i) {
        const int frequency = interpolate(460, 85, i, count);
        filtered_noise += (noise_sample(rng) - filtered_noise) / 3;

        const int32_t tone = scale_sample(triangle_wave(phase), 25000);
        const int32_t noise = scale_sample(filtered_noise, 10500);
        samples[i] = scale_sample(tone + noise, decay_gain(i, count));
        phase += phase_step(frequency);
    }

    return finish_sound(std::move(samples), 2, 15, 18000);
}

int32_t articulated_note_gain(std::size_t index, std::size_t count, std::size_t attack) {
    attack = std::min(attack, count - 1);
    if (attack > 1 && index < attack) {
        return ratio_gain(index, attack - 1);
    }

    const int32_t linear_decay = ratio_gain(count - 1 - index, count - 1 - attack);
    return scale_sample(linear_decay, linear_decay);
}

template <std::size_t NoteCount>
Pcm make_square_sequence(int duration_ms, const std::array<int, NoteCount> &frequencies,
                         int target_peak) {
    const std::size_t count = samples_for_ms(duration_ms);
    WorkingSamples samples(count);
    uint32_t phase = 0;
    std::size_t previous_note = NoteCount;
    const std::size_t note_attack = samples_for_ms(2);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t note = std::min(i * NoteCount / count, NoteCount - 1);
        const std::size_t note_start = note * count / NoteCount;
        const std::size_t note_end = (note + 1) * count / NoteCount;
        if (note != previous_note) {
            phase = 0;
            previous_note = note;
        }

        const int32_t note_gain =
            articulated_note_gain(i - note_start, note_end - note_start, note_attack);
        samples[i] = scale_sample(square_wave(phase), note_gain);
        phase += phase_step(frequencies[note]);
    }

    return finish_sound(std::move(samples), 1, 5, target_peak);
}

Pcm make_pickup() { return make_square_sequence(250, std::array<int, 3>{523, 659, 784}, 16500); }

Pcm make_player_hurt() {
    const std::size_t count = samples_for_ms(200);
    WorkingSamples samples(count);
    uint32_t phase = 0;
    int32_t exponential_decay = kUnitGain;

    for (std::size_t i = 0; i < count; ++i) {
        const int frequency = interpolate(130, 55, i, count);
        samples[i] = scale_sample(triangle_wave(phase), exponential_decay);
        phase += phase_step(frequency);
        exponential_decay =
            static_cast<int32_t>(static_cast<int64_t>(exponential_decay) * 65500 / 65536);
    }

    return finish_sound(std::move(samples), 2, 12, 18000);
}

Pcm make_win() { return make_square_sequence(360, std::array<int, 4>{523, 659, 784, 1047}, 17000); }

Pcm make_lose() {
    const std::size_t count = samples_for_ms(400);
    WorkingSamples samples(count);
    uint32_t main_phase = 0;
    uint32_t low_phase = 0;

    for (std::size_t i = 0; i < count; ++i) {
        const int frequency = interpolate(330, 98, i, count);
        const int32_t main_tone = scale_sample(triangle_wave(main_phase), 24000);
        const int32_t low_tone = scale_sample(triangle_wave(low_phase), 8000);
        const int32_t linear = decay_gain(i, count);
        samples[i] = scale_sample(main_tone + low_tone, scale_sample(linear, linear));
        main_phase += phase_step(frequency);
        low_phase += phase_step(std::max(frequency / 2, 1));
    }

    return finish_sound(std::move(samples), 2, 20, 16000);
}

std::array<Pcm, kSoundCount> make_audio_bank() {
    return {make_shot(),        make_egg_hit(), make_egg_death(), make_pickup(),
            make_player_hurt(), make_win(),     make_lose()};
}

const Pcm &empty_pcm() {
    static const Pcm empty;
    return empty;
}

} // namespace

void init_audio_bank() {
    if (audio_bank_initialized) {
        return;
    }
    audio_bank = make_audio_bank();
    audio_bank_initialized = true;
}

const Pcm &sound_for(EventType type) {
    if (!audio_bank_initialized) {
        return empty_pcm();
    }

    switch (type) {
    case EventType::Shot:
        return audio_bank[0];
    case EventType::EggHit:
        return audio_bank[1];
    case EventType::EggDeath:
        return audio_bank[2];
    case EventType::Pickup:
        return audio_bank[3];
    case EventType::PlayerHurt:
        return audio_bank[4];
    case EventType::Win:
        return audio_bank[5];
    case EventType::Lose:
        return audio_bank[6];
    }
    return empty_pcm();
}

} // namespace eh
