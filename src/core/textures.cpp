#include "core/textures.h"

#include "core/framebuffer.h"

namespace eh {

void init_textures() {}

uint32_t sample_wall(Tile tile, int, int) {
    switch (tile) {
    case Tile::Floor:
        return rgba(70, 52, 38);
    case Tile::WallBurrow:
        return rgba(112, 72, 44);
    case Tile::WallPantry:
        return rgba(196, 184, 148);
    case Tile::WallCellar:
        return rgba(88, 96, 104);
    case Tile::WallBasket:
        return rgba(164, 112, 52);
    }
    return rgba(255, 0, 255);
}

} // namespace eh
