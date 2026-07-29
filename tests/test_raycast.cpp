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
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

namespace {

constexpr float PI = 3.14159265358979323846f;
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
    const float camera_plane_scale = std::tan(eh::FOV_RADIANS * 0.5f);
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

TEST_CASE("raycast: wall textures carry visible internal contrast") {
    // Deliberately does NOT call init_textures() first: sampling must produce a real
    // texture on its own. The distinctness check above compares the four tile types
    // against each other, which passes even when every texture is a single flat
    // colour -- exactly what rendered when the lone init call in main.cpp (a file no
    // test target links) was missing.
    const std::array<eh::Tile, 4> tiles{eh::Tile::WallBurrow, eh::Tile::WallPantry,
                                        eh::Tile::WallCellar, eh::Tile::WallBasket};

    for (const eh::Tile tile : tiles) {
        float min_luma = 1.0e9f;
        float max_luma = -1.0e9f;
        std::vector<uint32_t> texels;
        for (int ty = 0; ty < 64; ++ty) {
            for (int tx = 0; tx < 64; ++tx) {
                const uint32_t texel = eh::sample_wall(tile, tx, ty);
                texels.push_back(texel);
                const float luma = 0.299f * static_cast<float>(texel & 0xffu) +
                                   0.587f * static_cast<float>((texel >> 8) & 0xffu) +
                                   0.114f * static_cast<float>((texel >> 16) & 0xffu);
                min_luma = std::min(min_luma, luma);
                max_luma = std::max(max_luma, luma);
            }
        }
        std::sort(texels.begin(), texels.end());
        texels.erase(std::unique(texels.begin(), texels.end()), texels.end());

        CAPTURE(static_cast<int>(tile));
        CAPTURE(texels.size());
        CAPTURE(max_luma - min_luma);
        REQUIRE(texels.size() > 16);
        REQUIRE(max_luma - min_luma > 12.0f);
    }
}

TEST_CASE("raycast: rendered wall columns vary vertically") {
    // A single wall column is lit uniformly, so any vertical variation within one
    // column is texture reaching actual pixels. This is the end-to-end companion to
    // the texel test above, and it also covers sampling without an explicit init.
    eh::GameState game;
    eh::reset(game, 0x1234567u);

    TestFramebuffer target;
    eh::render_walls(game, target.framebuffer);

    // The closest wall spans the most rows, giving the largest safe sample.
    int nearest = 0;
    for (int column = 1; column < eh::Framebuffer::W; ++column) {
        if (target.depth[static_cast<std::size_t>(column)] <
            target.depth[static_cast<std::size_t>(nearest)]) {
            nearest = column;
        }
    }

    constexpr int MIDDLE = eh::Framebuffer::H / 2;
    const float span =
        static_cast<float>(eh::Framebuffer::H) / target.depth[static_cast<std::size_t>(nearest)];
    const int reach = std::clamp(static_cast<int>(span * 0.25f), 2, MIDDLE - 1);

    std::vector<uint32_t> column_pixels;
    for (int y = MIDDLE - reach; y <= MIDDLE + reach; ++y) {
        column_pixels.push_back(
            target.pixels[static_cast<std::size_t>(y) * eh::Framebuffer::W + nearest]);
    }
    std::sort(column_pixels.begin(), column_pixels.end());
    column_pixels.erase(std::unique(column_pixels.begin(), column_pixels.end()),
                        column_pixels.end());

    CAPTURE(nearest);
    CAPTURE(target.depth[static_cast<std::size_t>(nearest)]);
    CAPTURE(column_pixels.size());
    REQUIRE(column_pixels.size() > 3);
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

TEST_CASE("raycast: walls facing along Y are shaded darker than walls facing along X") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    eh::init_textures();
    TestFramebuffer target;

    // From the spawn at (3.5, 3.5) the west border wall and the north border
    // wall are both exactly 2.5 tiles away. Distance shading is therefore
    // identical for the two views and wall height is identical, so the only
    // variable left is which axis the wall face is perpendicular to. Both views
    // also sweep the same 3.2-tile span of wall across the FOV, so they sample a
    // comparable spread of texture columns.
    constexpr int BAND = 40;
    constexpr int FIRST_ROW = 150;
    constexpr int LAST_ROW = 210;

    auto mean_wall_luma = [&](double degrees) {
        game.player.angle = eh::angle_from_deg(degrees);
        eh::render_walls(game, target.framebuffer);
        double total = 0.0;
        int samples = 0;
        for (int column = eh::Framebuffer::W / 2 - BAND; column < eh::Framebuffer::W / 2 + BAND;
             ++column) {
            REQUIRE(std::abs(target.depth[static_cast<std::size_t>(column)] - 2.5f) < 0.001f);
            for (int row = FIRST_ROW; row < LAST_ROW; ++row) {
                const uint32_t pixel =
                    target.pixels[static_cast<std::size_t>(row) * eh::Framebuffer::W +
                                  static_cast<std::size_t>(column)];
                total += static_cast<double>((pixel >> 16) & 0xffu) +
                         static_cast<double>((pixel >> 8) & 0xffu) +
                         static_cast<double>(pixel & 0xffu);
                ++samples;
            }
        }
        return total / static_cast<double>(samples);
    };

    const double x_face = mean_wall_luma(180.0); // west border, crosses an X grid line
    const double y_face = mean_wall_luma(270.0); // north border, crosses a Y grid line

    // Both faces are lit, but the Y face carries the 0.7 side factor. Asserted as
    // a ratio rather than absolute values so it survives any texture retouch.
    REQUIRE(x_face > 0.0);
    REQUIRE(y_face < x_face);
    const double ratio = y_face / x_face;
    REQUIRE(ratio > 0.6);
    REQUIRE(ratio < 0.8);
}

TEST_CASE("raycast: opposite wall faces preserve texture handedness") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    eh::init_textures();

    constexpr int ROOM_SIZE = 7;
    game.level.map.width = ROOM_SIZE;
    game.level.map.height = ROOM_SIZE;
    game.level.map.tiles.assign(ROOM_SIZE * ROOM_SIZE, eh::Tile::Floor);
    for (int cell = 0; cell < ROOM_SIZE; ++cell) {
        game.level.map.tiles[static_cast<std::size_t>(cell)] = eh::Tile::WallBurrow;
        game.level.map.tiles[static_cast<std::size_t>((ROOM_SIZE - 1) * ROOM_SIZE + cell)] =
            eh::Tile::WallBurrow;
        game.level.map.tiles[static_cast<std::size_t>(cell * ROOM_SIZE)] =
            eh::Tile::WallBurrow;
        game.level.map.tiles[static_cast<std::size_t>(cell * ROOM_SIZE + ROOM_SIZE - 1)] =
            eh::Tile::WallBurrow;
    }
    game.player.x = eh::fx_from_int(3) + eh::FX_ONE / 2;
    game.player.y = eh::fx_from_int(3) + eh::FX_ONE / 2;

    TestFramebuffer target;
    auto render_scanline = [&](double degrees) {
        game.player.angle = eh::angle_from_deg(degrees);
        eh::render_walls(game, target.framebuffer);

        std::array<uint32_t, eh::Framebuffer::W> scanline{};
        float max_depth_error = 0.0f;
        for (int column = 0; column < eh::Framebuffer::W; ++column) {
            max_depth_error =
                std::max(max_depth_error,
                         std::abs(target.depth[static_cast<std::size_t>(column)] - 2.5f));
            // Opposite cardinal rays can quantize to 143- versus 144-pixel wall
            // columns. One row above centre samples the same texture Y in both.
            scanline[static_cast<std::size_t>(column)] =
                target.pixels[static_cast<std::size_t>(eh::Framebuffer::H / 2 - 1) *
                                  eh::Framebuffer::W +
                              static_cast<std::size_t>(column)];
        }
        CAPTURE(degrees, max_depth_error);
        REQUIRE(max_depth_error < 0.001f);
        return scanline;
    };

    const auto east = render_scanline(0.0);
    const auto west = render_scanline(180.0);
    const auto south = render_scanline(90.0);
    const auto north = render_scanline(270.0);

    const auto count_matches = [](const auto &lhs, const auto &rhs) {
        return static_cast<int>(std::inner_product(
            lhs.begin(), lhs.end(), rhs.begin(), 0, std::plus<>(), std::equal_to<>()));
    };
    const auto count_reverse_differences = [](const auto &pixels) {
        return static_cast<int>(std::inner_product(
            pixels.begin(), pixels.end(), pixels.rbegin(), 0, std::plus<>(),
            std::not_equal_to<>()));
    };

    // First prove the sampled art is asymmetric enough to make orientation observable.
    REQUIRE(count_reverse_differences(east) > eh::Framebuffer::W / 2);
    REQUIRE(count_reverse_differences(north) > eh::Framebuffer::W / 2);

    // Opposite faces reverse the world-space wall coordinate. The renderer's face
    // correction must compensate so the texture retains one screen-space handedness.
    const int east_west_matches = count_matches(east, west);
    const int north_south_matches = count_matches(north, south);
    CAPTURE(east_west_matches, north_south_matches);
    REQUIRE(east_west_matches > eh::Framebuffer::W * 95 / 100);
    REQUIRE(north_south_matches > eh::Framebuffer::W * 95 / 100);
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
