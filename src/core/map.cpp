#include "core/map.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace eh {

const char *const BURROW_01 = R"(########################
#.......####...........#
#.......####...E.....J.#
#..P...................#
#.......####...........#
#.....J.####.C......E..#
#.......####...........#
####.#############.#####
####.#############.#####
####.#############.#####
#.........####.........#
#.J.......####.........#
#.........####...E.....#
#.................X....#
#..E......####.........#
#.......C.####......E..#
#.........####.........#
########################)";

namespace {

MapParseResult failure(size_t row, size_t column, const std::string &message) {
    MapParseResult result;
    result.error =
        "row " + std::to_string(row) + ", col " + std::to_string(column) + ": " + message;
    return result;
}

SpawnPoint tile_center(int x, int y) {
    return {fx_from_int(x) + FX_ONE / 2, fx_from_int(y) + FX_ONE / 2};
}

Tile wall_tile_for(int x, int y, int width, int height) {
    const bool east = x >= width / 2;
    const bool south = y >= height / 2;
    if (east && south) {
        return Tile::WallBasket;
    }
    if (east) {
        return Tile::WallPantry;
    }
    if (south) {
        return Tile::WallCellar;
    }
    return Tile::WallBurrow;
}

bool valid_map_character(char value) {
    switch (value) {
    case '#':
    case '.':
    case 'P':
    case 'E':
    case 'J':
    case 'C':
    case 'X':
        return true;
    default:
        return false;
    }
}

} // namespace

bool Map::in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

Tile Map::at(int x, int y) const {
    if (!in_bounds(x, y)) {
        return Tile::WallBurrow;
    }
    return tiles[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

bool Map::is_wall(int x, int y) const { return at(x, y) != Tile::Floor; }

MapParseResult parse_map(std::string_view ascii) {
    if (ascii.empty()) {
        return failure(1, 1, "map is empty");
    }

    std::vector<std::string_view> rows;
    size_t line_start = 0;
    while (line_start < ascii.size()) {
        const size_t newline = ascii.find('\n', line_start);
        const size_t line_end = newline == std::string_view::npos ? ascii.size() : newline;
        std::string_view row = ascii.substr(line_start, line_end - line_start);
        if (!row.empty() && row.back() == '\r') {
            row.remove_suffix(1);
        }
        rows.push_back(row);
        if (newline == std::string_view::npos) {
            break;
        }
        line_start = newline + 1;
    }

    if (rows.empty() || rows.front().empty()) {
        return failure(1, 1, "map row is empty");
    }

    const size_t width = rows.front().size();
    for (size_t y = 0; y < rows.size(); ++y) {
        if (rows[y].size() != width) {
            const size_t column = std::min(rows[y].size(), width) + 1;
            return failure(y + 1, column,
                           "map is not rectangular; expected " + std::to_string(width) +
                               " columns");
        }
    }

    MapData data;
    data.map.width = static_cast<int>(width);
    data.map.height = static_cast<int>(rows.size());
    data.map.tiles.resize(width * rows.size(), Tile::Floor);

    int player_count = 0;
    int egg_count = 0;
    int basket_count = 0;
    int player_x = 0;
    int player_y = 0;

    for (size_t y = 0; y < rows.size(); ++y) {
        for (size_t x = 0; x < width; ++x) {
            const char cell = rows[y][x];
            if (!valid_map_character(cell)) {
                return failure(y + 1, x + 1,
                               std::string("unsupported map character '") + cell + "'");
            }

            const bool border = x == 0 || y == 0 || x + 1 == width || y + 1 == rows.size();
            if (border && cell != '#') {
                return failure(y + 1, x + 1, "map border must be fully wall-enclosed");
            }

            const size_t index = y * width + x;
            if (cell == '#') {
                data.map.tiles[index] = wall_tile_for(static_cast<int>(x), static_cast<int>(y),
                                                      data.map.width, data.map.height);
                continue;
            }

            const SpawnPoint spawn = tile_center(static_cast<int>(x), static_cast<int>(y));
            switch (cell) {
            case 'P':
                ++player_count;
                if (player_count > 1) {
                    return failure(y + 1, x + 1, "duplicate player spawn 'P'");
                }
                player_x = static_cast<int>(x);
                player_y = static_cast<int>(y);
                data.player = spawn;
                data.player_angle = 0;
                break;
            case 'E':
                ++egg_count;
                data.eggs.push_back(spawn);
                break;
            case 'J':
                data.jellybeans.push_back(spawn);
                break;
            case 'C':
                data.carrots.push_back(spawn);
                break;
            case 'X':
                ++basket_count;
                if (basket_count > 1) {
                    return failure(y + 1, x + 1, "duplicate Basket spawn 'X'");
                }
                data.basket = spawn;
                break;
            default:
                break;
            }
        }
    }

    if (player_count != 1) {
        return failure(1, 1, "expected exactly one player spawn 'P'");
    }
    if (egg_count == 0) {
        return failure(1, 1, "expected at least one Egg spawn 'E'");
    }
    if (basket_count != 1) {
        return failure(1, 1, "expected exactly one Basket spawn 'X'");
    }

    std::vector<uint8_t> reachable(width * rows.size(), 0);
    std::vector<std::pair<int, int>> pending;
    pending.emplace_back(player_x, player_y);
    reachable[static_cast<size_t>(player_y) * width + static_cast<size_t>(player_x)] = 1;

    constexpr int DIRECTIONS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!pending.empty()) {
        const auto [x, y] = pending.back();
        pending.pop_back();
        for (const auto &direction : DIRECTIONS) {
            const int next_x = x + direction[0];
            const int next_y = y + direction[1];
            if (!data.map.in_bounds(next_x, next_y) || data.map.is_wall(next_x, next_y)) {
                continue;
            }

            const size_t index = static_cast<size_t>(next_y) * width + static_cast<size_t>(next_x);
            if (reachable[index] != 0) {
                continue;
            }
            reachable[index] = 1;
            pending.emplace_back(next_x, next_y);
        }
    }

    for (size_t y = 0; y < rows.size(); ++y) {
        for (size_t x = 0; x < width; ++x) {
            const size_t index = y * width + x;
            if (data.map.tiles[index] == Tile::Floor && reachable[index] == 0) {
                return failure(y + 1, x + 1, "open cell is unreachable from player spawn");
            }
        }
    }

    MapParseResult result;
    result.ok = true;
    result.data = std::move(data);
    return result;
}

} // namespace eh
