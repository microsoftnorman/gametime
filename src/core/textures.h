#pragma once

#include "core/map.h"

#include <cstdint>

namespace eh {

void init_textures();
uint32_t sample_wall(Tile, int tx, int ty);

} // namespace eh
