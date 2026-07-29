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

// The colour each wall tile is meant to read as, independent of the procedural
// generator that produces its texture. Two hand-written orderings have to agree
// for a wall to draw the right artwork - the Tile-to-slot mapping in
// texture_index() and the slot-to-generator assignment in init_textures() - and
// nothing tied them together: exchanging the slots the pantry and cellar tiles
// point at was verified to pass the entire test suite, because every wall stayed
// distinct from every other wall and only their identities were exchanged. This
// states the intended appearance once, so a test can hold the generated art
// against it instead of against another piece of generated art.
uint32_t wall_swatch(Tile);

void init_textures();
uint32_t sample_wall(Tile, int tx, int ty);

} // namespace eh
