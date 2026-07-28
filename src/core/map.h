#pragma once

#include "core/fixed.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eh {

enum class Tile : uint8_t {
    Floor = 0,
    WallBurrow = 1,
    WallPantry = 2,
    WallCellar = 3,
    WallBasket = 4
};

struct Map {
    int width = 0, height = 0;
    std::vector<Tile> tiles;
    bool in_bounds(int x, int y) const;
    Tile at(int x, int y) const;
    bool is_wall(int x, int y) const;
};

struct SpawnPoint {
    fx x, y;
};

struct MapData {
    Map map;
    SpawnPoint player;
    angle_t player_angle = 0;
    std::vector<SpawnPoint> eggs, jellybeans, carrots;
    SpawnPoint basket;
};

struct MapParseResult {
    bool ok = false;
    std::string error;
    MapData data;
};

MapParseResult parse_map(std::string_view ascii);
extern const char *const BURROW_01;

} // namespace eh
