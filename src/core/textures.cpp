#include "core/textures.h"

#include "core/framebuffer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace eh {

namespace {

constexpr int TEXTURE_SIZE = 64;
constexpr int TEXEL_COUNT = TEXTURE_SIZE * TEXTURE_SIZE;
using Texture = std::array<uint32_t, TEXEL_COUNT>;

std::array<Texture, 4> wall_textures{};
std::once_flag texture_init_flag;
std::atomic_bool textures_ready{false};

uint8_t channel(int value) { return static_cast<uint8_t>(std::clamp(value, 0, 255)); }

uint32_t coordinate_hash(int x, int y, uint32_t seed) {
    uint32_t value = static_cast<uint32_t>(x) * 0x9e3779b9u;
    value ^= static_cast<uint32_t>(y) * 0x85ebca6bu;
    value ^= seed;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

std::size_t texel_index(int x, int y) { return static_cast<std::size_t>(y * TEXTURE_SIZE + x); }

uint32_t fallback_color(Tile tile) {
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

int texture_index(Tile tile) {
    switch (tile) {
    case Tile::WallBurrow:
        return 0;
    case Tile::WallPantry:
        return 1;
    case Tile::WallCellar:
        return 2;
    case Tile::WallBasket:
        return 3;
    case Tile::Floor:
        return -1;
    }
    return -1;
}

int wrap_coordinate(int value) {
    int wrapped = value % TEXTURE_SIZE;
    if (wrapped < 0) {
        wrapped += TEXTURE_SIZE;
    }
    return wrapped;
}

void generate_burrow(Texture &texture) {
    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            const uint32_t noise = coordinate_hash(x, y, 0x19a7c35du);
            const int variation = static_cast<int>(noise & 31u) - 15;
            int red = 112 + variation;
            int green = 70 + variation * 2 / 3;
            int blue = 40 + variation / 3;

            if ((noise >> 8) % 53u == 0u) {
                red -= 42;
                green -= 32;
                blue -= 18;
            } else if ((noise >> 16) % 79u == 0u) {
                red += 34;
                green += 26;
                blue += 14;
            }

            texture[texel_index(x, y)] = rgba(channel(red), channel(green), channel(blue));
        }
    }
}

void generate_pantry(Texture &texture) {
    constexpr int TILE_SPAN = 16;
    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            const bool grout = x % TILE_SPAN < 2 || y % TILE_SPAN < 2;
            if (grout) {
                const int variation = static_cast<int>(coordinate_hash(x, y, 0x48f1a2c3u) & 7u);
                texture[texel_index(x, y)] = rgba(
                    channel(101 + variation), channel(108 + variation), channel(108 + variation));
                continue;
            }

            const int variation =
                static_cast<int>(coordinate_hash(x / TILE_SPAN, y / TILE_SPAN, 0xb5297a4du) & 15u) -
                7;
            const int edge_light = (x % TILE_SPAN == 2 || y % TILE_SPAN == 2) ? 9 : 0;
            texture[texel_index(x, y)] =
                rgba(channel(207 + variation + edge_light), channel(201 + variation + edge_light),
                     channel(169 + variation + edge_light));
        }
    }
}

void generate_cellar(Texture &texture) {
    constexpr int BLOCK_WIDTH = 16;
    constexpr int BLOCK_HEIGHT = 12;
    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        const int block_row = y / BLOCK_HEIGHT;
        const int row_offset = (block_row & 1) * (BLOCK_WIDTH / 2);
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            const int local_x = (x + row_offset) % BLOCK_WIDTH;
            const bool mortar = local_x < 2 || y % BLOCK_HEIGHT < 2;
            if (mortar) {
                texture[texel_index(x, y)] = rgba(47, 52, 58);
                continue;
            }

            const int block_column = (x + row_offset) / BLOCK_WIDTH;
            const int block_tone =
                static_cast<int>(coordinate_hash(block_column, block_row, 0x6d2b79f5u) & 31u) - 15;
            const int grain = static_cast<int>(coordinate_hash(x, y, 0x27d4eb2fu) & 7u) - 3;
            texture[texel_index(x, y)] =
                rgba(channel(94 + block_tone + grain), channel(101 + block_tone + grain),
                     channel(108 + block_tone + grain));
        }
    }
}

void generate_basket(Texture &texture) {
    constexpr std::array<int, 4> STRAND_PROFILE{-18, 18, 9, -11};
    constexpr int STRAND_WIDTH = 4;

    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            const bool vertical_over = ((x / STRAND_WIDTH) + (y / STRAND_WIDTH)) % 2 == 0;
            const int strand_position = vertical_over ? x % STRAND_WIDTH : y % STRAND_WIDTH;
            const int grain = static_cast<int>(coordinate_hash(x, y, 0xa24baed4u) & 7u) - 3;
            int light = STRAND_PROFILE[static_cast<std::size_t>(strand_position)] + grain;
            if (!vertical_over) {
                light -= 8;
            }

            texture[texel_index(x, y)] =
                rgba(channel(174 + light), channel(111 + light * 2 / 3), channel(47 + light / 3));
        }
    }
}

} // namespace

void init_textures() {
    std::call_once(texture_init_flag, [] {
        generate_burrow(wall_textures[0]);
        generate_pantry(wall_textures[1]);
        generate_cellar(wall_textures[2]);
        generate_basket(wall_textures[3]);
        textures_ready.store(true, std::memory_order_release);
    });
}

uint32_t sample_wall(Tile tile, int tx, int ty) {
    const int index = texture_index(tile);
    if (index < 0 || !textures_ready.load(std::memory_order_acquire)) {
        return fallback_color(tile);
    }

    const int x = wrap_coordinate(tx);
    const int y = wrap_coordinate(ty);
    return wall_textures[static_cast<std::size_t>(index)][texel_index(x, y)];
}

} // namespace eh
