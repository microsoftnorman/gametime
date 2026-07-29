#pragma once

#include "core/map.h"

#include <cstdint>

namespace eh {

// Edge length of every wall texture, shared by the generator that fills them
// (textures.cpp) and the raycaster that samples them (raycast.cpp). These were
// two independent literals in two translation units with nothing tying them
// together: pointing the consumer at 128 while the producer stayed at 64 was
// verified to pass the entire test suite, and so was 32. `sample_wall` wraps its
// coordinates, so a divergence is memory-safe and therefore completely silent --
// it just renders every wall tile as a 2x2 tiling of its own texture, or as one
// quadrant stretched across the whole face.
inline constexpr int WALL_TEXTURE_SIZE = 64;

void init_textures();
uint32_t sample_wall(Tile, int tx, int ty);

} // namespace eh
