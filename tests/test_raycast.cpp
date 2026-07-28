#include "core/fixed.h"
#include "core/framebuffer.h"
#include "core/raycast.h"
#include "core/state.h"
#include "core/textures.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float FOV_RADIANS = 66.0f * PI / 180.0f;
constexpr std::size_t PIXEL_COUNT =
    static_cast<std::size_t>(eh::Framebuffer::W) * static_cast<std::size_t>(eh::Framebuffer::H);

struct TestFramebuffer {
    std::vector<uint32_t> pixels = std::vector<uint32_t>(PIXEL_COUNT);
    std::array<float, eh::Framebuffer::W> depth{};
    eh::Framebuffer framebuffer{pixels.data(), depth.data()};
};

struct Direction {
    float x;
    float y;
};

Direction ray_direction(eh::angle_t player_angle, int column) {
    const float angle =
        static_cast<float>(player_angle) * 2.0f * PI / static_cast<float>(UINT16_MAX + 1u);
    const float direction_x = std::cos(angle);
    const float direction_y = std::sin(angle);
    const float camera_plane_scale = std::tan(FOV_RADIANS * 0.5f);
    const float plane_x = -direction_y * camera_plane_scale;
    const float plane_y = direction_x * camera_plane_scale;
    const float camera_x =
        2.0f * (static_cast<float>(column) + 0.5f) / static_cast<float>(eh::Framebuffer::W) - 1.0f;
    return {direction_x + plane_x * camera_x, direction_y + plane_y * camera_x};
}

} // namespace

TEST_CASE("raycast: wall textures are distinct and coordinates wrap safely") {
    const uint32_t preinit_fallback = eh::sample_wall(
        eh::Tile::WallBurrow, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    REQUIRE((preinit_fallback >> 24) == 0xffu);

    eh::init_textures();

    std::array<uint32_t, 4> colors{
        eh::sample_wall(eh::Tile::WallBurrow, 13, 27),
        eh::sample_wall(eh::Tile::WallPantry, 13, 27),
        eh::sample_wall(eh::Tile::WallCellar, 13, 27),
        eh::sample_wall(eh::Tile::WallBasket, 13, 27),
    };
    std::sort(colors.begin(), colors.end());
    REQUIRE(std::adjacent_find(colors.begin(), colors.end()) == colors.end());

    REQUIRE(eh::sample_wall(eh::Tile::WallBurrow, -1, -1) ==
            eh::sample_wall(eh::Tile::WallBurrow, 63, 63));
    REQUIRE(eh::sample_wall(eh::Tile::WallPantry, 64'000'007, -63'999'991) ==
            eh::sample_wall(eh::Tile::WallPantry, 7, 9));
    REQUIRE((eh::sample_wall(eh::Tile::WallBasket, std::numeric_limits<int>::min(),
                             std::numeric_limits<int>::max()) >>
             24) == 0xffu);
}

TEST_CASE("raycast: DDA terminates at the known east wall in BURROW_01") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    game.player.angle = eh::angle_from_deg(0.0);
    eh::init_textures();

    TestFramebuffer target;
    eh::render_walls(game, target.framebuffer);

    constexpr int COLUMN = eh::Framebuffer::W / 2;
    const float distance = target.depth[COLUMN];
    REQUIRE(std::isfinite(distance));
    REQUIRE(distance > 0.0f);
    REQUIRE(distance < 100.0f);

    const Direction ray = ray_direction(game.player.angle, COLUMN);
    constexpr float INTO_WALL = 0.001f;
    const int hit_x = static_cast<int>(
        std::floor(eh::fx_to_float(game.player.x) + ray.x * (distance + INTO_WALL)));
    const int hit_y = static_cast<int>(
        std::floor(eh::fx_to_float(game.player.y) + ray.y * (distance + INTO_WALL)));

    REQUIRE(hit_x == 23);
    REQUIRE(hit_y == 3);
    REQUIRE(game.level.map.is_wall(hit_x, hit_y));
}

TEST_CASE("raycast: every column stores positive finite perpendicular depth") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    eh::init_textures();

    TestFramebuffer target;
    eh::render_walls(game, target.framebuffer);

    for (int column = 0; column < eh::Framebuffer::W; ++column) {
        REQUIRE(std::isfinite(target.depth[static_cast<std::size_t>(column)]));
        REQUIRE(target.depth[static_cast<std::size_t>(column)] > 0.0f);
    }
}

TEST_CASE("raycast: depth remains perpendicular across a planar wall") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    game.player.angle = eh::angle_from_deg(0.0);
    eh::init_textures();

    TestFramebuffer target;
    eh::render_walls(game, target.framebuffer);

    // These separated rays both reach the same east-facing wall plane. Raw ray length would
    // report roughly 19.53 at the edges instead of the perpendicular distance of 19.5.
    constexpr std::array<int, 3> COLUMNS{292, eh::Framebuffer::W / 2, 348};
    for (const int column : COLUMNS) {
        REQUIRE(std::abs(target.depth[static_cast<std::size_t>(column)] - 19.5f) < 0.001f);
    }
}

TEST_CASE("raycast: open east corridor is deeper than the nearby north wall") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    eh::init_textures();
    TestFramebuffer target;
    constexpr int COLUMN = eh::Framebuffer::W / 2;

    game.player.angle = eh::angle_from_deg(0.0);
    eh::render_walls(game, target.framebuffer);
    const float corridor_distance = target.depth[COLUMN];

    game.player.angle = eh::angle_from_deg(270.0);
    eh::render_walls(game, target.framebuffer);
    const float near_wall_distance = target.depth[COLUMN];

    REQUIRE(corridor_distance > 15.0f);
    REQUIRE(near_wall_distance < 4.0f);
    REQUIRE(corridor_distance > near_wall_distance);
}

TEST_CASE("raycast: rendering preserves pixel and depth guards") {
    constexpr std::size_t GUARD_SIZE = 37;
    constexpr uint32_t PIXEL_GUARD = 0x5ac39e71u;
    constexpr float DEPTH_GUARD = -12345.25f;

    std::vector<uint32_t> guarded_pixels(PIXEL_COUNT + 2 * GUARD_SIZE, PIXEL_GUARD);
    std::vector<float> guarded_depth(static_cast<std::size_t>(eh::Framebuffer::W) + 2 * GUARD_SIZE,
                                     DEPTH_GUARD);
    eh::Framebuffer framebuffer{guarded_pixels.data() + GUARD_SIZE,
                                guarded_depth.data() + GUARD_SIZE};

    eh::GameState game;
    eh::reset(game, 0x1234567u);
    eh::init_textures();
    eh::render_walls(game, framebuffer);

    REQUIRE(std::all_of(guarded_pixels.begin(), guarded_pixels.begin() + GUARD_SIZE,
                        [](uint32_t value) { return value == PIXEL_GUARD; }));
    REQUIRE(std::all_of(guarded_pixels.end() - GUARD_SIZE, guarded_pixels.end(),
                        [](uint32_t value) { return value == PIXEL_GUARD; }));
    REQUIRE(std::all_of(guarded_depth.begin(), guarded_depth.begin() + GUARD_SIZE,
                        [](float value) { return value == DEPTH_GUARD; }));
    REQUIRE(std::all_of(guarded_depth.end() - GUARD_SIZE, guarded_depth.end(),
                        [](float value) { return value == DEPTH_GUARD; }));
}
