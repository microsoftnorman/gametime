#pragma once

#include <cstdint>

namespace eh {

struct Rng {
    uint32_t state;
};

inline uint32_t next(Rng &rng) {
    uint32_t value = rng.state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    rng.state = value;
    return value;
}

inline uint32_t range(Rng &rng, uint32_t n) { return next(rng) % n; }

} // namespace eh
