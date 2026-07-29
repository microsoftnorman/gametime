#include "core/fixed.h"
#include "core/framebuffer.h"
#include "core/raycast.h"
#include "core/state.h"
#include "core/textures.h"

#include <catch2/catch_approx.hpp>
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

// Free functions rather than lambdas: a non-capturing lambda nested inside a
// capturing one cannot reach an enclosing constexpr it odr-uses under GCC, which
// MSVC accepts silently.
double luma(uint32_t color) {
    const double r = static_cast<double>(color & 0xffu);
    const double g = static_cast<double>((color >> 8) & 0xffu);
    const double b = static_cast<double>((color >> 16) & 0xffu);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

// Mean luminance of one screen row across a central band of columns. The band
// matters: the outermost columns of a flat face quantise to a wall one pixel
// shorter, which mixes background into the extreme rows -- exactly the rows a
// top-versus-bottom comparison depends on.
double row_band_mean(const std::vector<uint32_t> &frame, int row) {
    double sum = 0.0;
    int counted = 0;
    for (int column = 240; column < 400; ++column) {
        sum += luma(frame[static_cast<std::size_t>(row) * eh::Framebuffer::W + column]);
        ++counted;
    }
    return sum / counted;
}

// Mean of the first or last tenth of a profile.
double decile_mean(const std::vector<double> &profile, bool from_top) {
    const std::size_t count = std::max<std::size_t>(1, profile.size() / 10);
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += from_top ? profile[i] : profile[profile.size() - 1 - i];
    }
    return sum / static_cast<double>(count);
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

TEST_CASE("raycast: opposite wall faces present the texture in the same orientation") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    eh::init_textures();
    TestFramebuffer target;

    // A symmetric room: identical wall tiles on all four borders and the player
    // exactly in the middle, so every view is the same texture at the same
    // distance under the same shading. The only variable left is which face is
    // being sampled, and therefore whether the renderer mirrors it correctly.
    //
    // The existing shading test above samples these same pixels but collapses
    // them with a mean, and a mean is invariant to reversing column order. Both
    // halves of the face-correction were verified to survive the whole suite
    // (105/105) with this asymmetry unpinned, so the comparison here is
    // deliberately sequence-by-sequence rather than aggregated.
    constexpr int ROOM = 9;
    eh::Map &map = game.level.map;
    map.width = ROOM;
    map.height = ROOM;
    map.tiles.assign(static_cast<std::size_t>(ROOM) * static_cast<std::size_t>(ROOM),
                     eh::Tile::Floor);
    for (int i = 0; i < ROOM; ++i) {
        const auto index = [](int x, int y) { return static_cast<std::size_t>(y * ROOM + x); };
        map.tiles[index(i, 0)] = eh::Tile::WallPantry;
        map.tiles[index(i, ROOM - 1)] = eh::Tile::WallPantry;
        map.tiles[index(0, i)] = eh::Tile::WallPantry;
        map.tiles[index(ROOM - 1, i)] = eh::Tile::WallPantry;
    }
    game.player.x = eh::fx_from_float(4.5f);
    game.player.y = eh::fx_from_float(4.5f);

    // Every border face is exactly 3.5 tiles from the middle, so all four views
    // project the wall to the same height and the comparison can be the whole
    // frame rather than a chosen row. Measured: 0 of 360 rows differ between
    // opposite headings. Comparing everything also sidesteps a hazard the
    // raycaster session hit on a 2.5-tile room, where opposite cardinal angles
    // quantized the wall to 143 and 144 pixels and only rows off centre matched.
    auto frame = [&](double degrees) {
        game.player.angle = eh::angle_from_deg(degrees);
        eh::render_walls(game, target.framebuffer);
        for (int column = 0; column < eh::Framebuffer::W; ++column) {
            // Guards that the view really is the one flat facing wall, not a corner.
            REQUIRE(std::abs(target.depth[static_cast<std::size_t>(column)] - 3.5f) < 0.001f);
        }
        return target.pixels;
    };

    const std::vector<uint32_t> east = frame(0.0);
    const std::vector<uint32_t> west = frame(180.0);
    const std::vector<uint32_t> north = frame(270.0);
    const std::vector<uint32_t> south = frame(90.0);

    // Non-vacuity first: the rendered wall has to be materially asymmetric across
    // the screen, or "the two views agree" would hold however the renderer
    // oriented the texture. A future symmetric texture fails here loudly instead
    // of quietly turning the orientation assertions into tautologies.
    auto mirrored = [](const std::vector<uint32_t> &source) {
        std::vector<uint32_t> flipped(source.size());
        for (int row = 0; row < eh::Framebuffer::H; ++row) {
            for (int column = 0; column < eh::Framebuffer::W; ++column) {
                flipped[static_cast<std::size_t>(row) * eh::Framebuffer::W +
                        static_cast<std::size_t>(column)] =
                    source[static_cast<std::size_t>(row) * eh::Framebuffer::W +
                           static_cast<std::size_t>(eh::Framebuffer::W - 1 - column)];
            }
        }
        return flipped;
    };
    REQUIRE(east != mirrored(east));
    REQUIRE(north != mirrored(north));

    // Both axes are checked because the correction is two independent clauses,
    // and each was confirmed to survive the suite on its own.
    REQUIRE(east == west);
    REQUIRE(north == south);
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

// mvp.md lists distance fog as a shipped feature, but nothing asserted it. The falloff
// distance and the brightness floor existed as three independent literals across two
// translation units, and all three of these mutations passed the entire 101-test suite:
//   sprites 16 -> 48 while walls kept 16   (an egg glows against the wall behind it)
//   walls   16 -> 48 while sprites kept 16 (the same disagreement, mirrored)
//   both retuned to 48 with a 0.02 floor   (fog effectively switched off)
// The expression now lives once in framebuffer.h. This pins the curve it produces.
TEST_CASE("render: distance shading follows the documented curve") {
    CHECK(eh::distance_brightness(0.0f) == Catch::Approx(1.0f));
    CHECK(eh::distance_brightness(4.0f) == Catch::Approx(0.75f));
    CHECK(eh::distance_brightness(8.0f) == Catch::Approx(0.5f));

    // The floor is reached at 12 tiles, not at the 16 tile falloff distance, because
    // 1 - 12/16 is already 0.25. Everything beyond 12 tiles is equally dark.
    CHECK(eh::distance_brightness(12.0f) == Catch::Approx(0.25f));
    CHECK(eh::distance_brightness(16.0f) == Catch::Approx(0.25f));
    CHECK(eh::distance_brightness(1000.0f) == Catch::Approx(0.25f));

    // Never brighter than unshaded, even behind the camera plane.
    CHECK(eh::distance_brightness(-5.0f) == Catch::Approx(1.0f));
}

// Proves render_walls actually consults the shared curve. Hoisting a constant that nobody
// calls would satisfy the test above while leaving the screen flat.
TEST_CASE("render: the far end of the corridor is shaded darker than the near wall") {
    eh::GameState game;
    eh::reset(game, 0x1234567u);
    eh::init_textures();
    TestFramebuffer target;

    // The same east border wall, viewed from two distances along the open corridor at
    // y = 3.5. Sampling a narrow band on the horizon keeps every sampled row inside the
    // wall at both distances, since the far view is only about 18 pixels tall.
    constexpr int BAND = 25;
    constexpr int FIRST_ROW = 176;
    constexpr int LAST_ROW = 184;

    auto mean_wall_luma = [&](int player_tile_x, float expected_depth) {
        game.player.x = eh::fx_from_int(player_tile_x) + eh::FX_ONE / 2;
        game.player.y = eh::fx_from_int(3) + eh::FX_ONE / 2;
        game.player.angle = eh::angle_from_deg(0.0);
        eh::render_walls(game, target.framebuffer);

        double total = 0.0;
        int samples = 0;
        for (int column = eh::Framebuffer::W / 2 - BAND; column < eh::Framebuffer::W / 2 + BAND;
             ++column) {
            REQUIRE(std::abs(target.depth[static_cast<std::size_t>(column)] - expected_depth) <
                    0.001f);
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

    const double near_luma = mean_wall_luma(20, 2.5f);
    const double far_luma = mean_wall_luma(3, 19.5f);

    // Asserted as a ratio because the two views sample the texture at very different
    // vertical compressions, so absolute luma is not comparable. At 2.5 tiles the factor
    // is 0.844 and at 19.5 tiles it is clamped to the 0.25 floor, a true ratio of 0.30.
    // A 48 tile falloff would raise it to 0.63 and no fog at all would put it near 1.0.
    REQUIRE(near_luma > 0.0);
    REQUIRE(far_luma < near_luma);
    REQUIRE(far_luma / near_luma < 0.45);
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

TEST_CASE("raycast: a wall tile shows exactly one full texture period") {
    // WALL_TEXTURE_SIZE was two independent literals -- one in textures.cpp
    // sizing the generated art, one in raycast.cpp sampling it -- with nothing
    // tying them together. Pointing the consumer at 128 while the producer kept
    // 64 passed 114/114, and so did 32: sample_wall wraps its coordinates, so a
    // divergence is memory-safe and therefore silent. The constant is now shared,
    // and these sections are the behavioural witness that both sides really use
    // it, since a re-added copy inside raycast.cpp's anonymous namespace would
    // shadow rather than collide.
    SECTION("the exported size is the texture's own period") {
        // Producer side, through the public sampler only: no arithmetic from
        // either module is reproduced here. Sampling one WALL_TEXTURE_SIZE past
        // any texel must return that texel again, and no smaller shift may, or
        // the exported number is not the period the renderer may assume.
        //
        // Note this is a whole-image shift, not a comparison against row 0. The
        // first version of this check asked whether row k equals row 0, which is
        // not a period test at all: several rows of the art legitimately repeat
        // (mortar bands), and it failed on the correct build at offsets 1, 12 and
        // 13. Read the expansion, fix the assumption.
        for (const eh::Tile tile : {eh::Tile::WallBurrow, eh::Tile::WallPantry,
                                    eh::Tile::WallCellar, eh::Tile::WallBasket}) {
            for (int x = 0; x < eh::WALL_TEXTURE_SIZE; ++x) {
                for (int y = 0; y < eh::WALL_TEXTURE_SIZE; ++y) {
                    REQUIRE(eh::sample_wall(tile, x + eh::WALL_TEXTURE_SIZE, y) ==
                            eh::sample_wall(tile, x, y));
                    REQUIRE(eh::sample_wall(tile, x, y + eh::WALL_TEXTURE_SIZE) ==
                            eh::sample_wall(tile, x, y));
                }
            }

            for (int shift = 1; shift < eh::WALL_TEXTURE_SIZE; ++shift) {
                bool horizontal_period = true;
                bool vertical_period = true;
                for (int x = 0; x < eh::WALL_TEXTURE_SIZE && (horizontal_period || vertical_period);
                     ++x) {
                    for (int y = 0;
                         y < eh::WALL_TEXTURE_SIZE && (horizontal_period || vertical_period); ++y) {
                        if (eh::sample_wall(tile, x + shift, y) != eh::sample_wall(tile, x, y)) {
                            horizontal_period = false;
                        }
                        if (eh::sample_wall(tile, x, y + shift) != eh::sample_wall(tile, x, y)) {
                            vertical_period = false;
                        }
                    }
                }
                CAPTURE(shift);
                CHECK_FALSE(horizontal_period);
                CHECK_FALSE(vertical_period);
            }
        }
    }

    SECTION("a rendered wall face is one period tall, not a tiling of itself") {
        eh::GameState game;
        eh::reset(game, 0x5eed1234u);
        eh::init_textures();

        constexpr int ROOM = 7;
        eh::Map &map = game.level.map;
        map.width = ROOM;
        map.height = ROOM;
        map.tiles.assign(static_cast<std::size_t>(ROOM) * static_cast<std::size_t>(ROOM),
                         eh::Tile::Floor);
        const auto index = [](int x, int y) { return static_cast<std::size_t>(y * ROOM + x); };
        for (int i = 0; i < ROOM; ++i) {
            map.tiles[index(i, 0)] = eh::Tile::WallPantry;
            map.tiles[index(i, ROOM - 1)] = eh::Tile::WallPantry;
            map.tiles[index(0, i)] = eh::Tile::WallPantry;
            map.tiles[index(ROOM - 1, i)] = eh::Tile::WallPantry;
        }
        game.player.x = eh::fx_from_float(3.5f);
        game.player.y = eh::fx_from_float(3.5f);
        game.player.angle = eh::angle_from_deg(0.0);

        TestFramebuffer target;

        // A map with no wall tiles at all: every ray misses, so this frame is the
        // untouched background gradient. Differing from it is what identifies wall
        // pixels, rather than assuming where the renderer puts them.
        eh::GameState empty = game;
        empty.level.map.tiles.assign(
            static_cast<std::size_t>(ROOM) * static_cast<std::size_t>(ROOM), eh::Tile::Floor);
        eh::render_walls(empty, target.framebuffer);
        const std::vector<uint32_t> background = target.pixels;

        eh::render_walls(game, target.framebuffer);
        const std::vector<uint32_t> wall = target.pixels;

        int top = -1;
        int bottom = -1;
        for (int y = 0; y < eh::Framebuffer::H; ++y) {
            const std::size_t at = static_cast<std::size_t>(y) * eh::Framebuffer::W + 320;
            if (wall[at] != background[at]) {
                if (top < 0) {
                    top = y;
                }
                bottom = y;
            }
        }

        // Non-vacuity: the face has to be a real wall, tall enough that halving or
        // doubling the sampling rate is resolvable at all, and flat so every column
        // shades identically.
        REQUIRE(top >= 0);
        REQUIRE(bottom - top + 1 > 2 * eh::WALL_TEXTURE_SIZE);
        for (int column = 0; column < eh::Framebuffer::W; ++column) {
            REQUIRE(std::abs(target.depth[static_cast<std::size_t>(column)] - 2.5f) < 0.001f);
        }

        // Each column steps through the texture once from the top of the wall to
        // the bottom, so its upper half and lower half are different art. If the
        // consumer's size is a multiple of the producer's period the two halves
        // become the same texels: measured 1325 of 46080 matching rows on the
        // shared constant against 43918 of 46080 with the consumer at 128.
        const int half = (bottom - top + 1) / 2;
        int matching = 0;
        for (int column = 0; column < eh::Framebuffer::W; ++column) {
            for (int i = 0; i < half; ++i) {
                const std::size_t upper =
                    static_cast<std::size_t>(top + i) * eh::Framebuffer::W + column;
                const std::size_t lower =
                    static_cast<std::size_t>(top + half + i) * eh::Framebuffer::W + column;
                if (wall[upper] == wall[lower]) {
                    ++matching;
                }
            }
        }
        const int compared = half * eh::Framebuffer::W;
        CAPTURE(matching, compared);
        CHECK(matching * 4 < compared);
    }
}

// texture_index() maps a Tile to a texture slot, and init_textures() fills each slot from a
// named generator. Those are two hand-written orderings that have to agree, with nothing tying
// them together: exchanging the slots the pantry and cellar tiles point at passed 120/120. Every
// wall stayed distinct from every other wall, which is precisely what the distinctness checks
// above assert - a swap preserves difference and exchanges only identity. wall_swatch() states
// the intended appearance separately, so the generated art can be held against what it is
// supposed to be instead of against another piece of generated art.
TEST_CASE("textures: each wall tile draws the artwork its own swatch describes") {
    constexpr std::array<eh::Tile, 4> WALLS{eh::Tile::WallBurrow, eh::Tile::WallPantry,
                                            eh::Tile::WallCellar, eh::Tile::WallBasket};

    std::array<std::array<double, 3>, 4> mean{};
    for (std::size_t i = 0; i < WALLS.size(); ++i) {
        std::array<long long, 3> sum{};
        for (int y = 0; y < eh::WALL_TEXTURE_SIZE; ++y) {
            for (int x = 0; x < eh::WALL_TEXTURE_SIZE; ++x) {
                const uint32_t texel = eh::sample_wall(WALLS[i], x, y);
                sum[0] += texel & 0xffu;
                sum[1] += (texel >> 8) & 0xffu;
                sum[2] += (texel >> 16) & 0xffu;
            }
        }
        const auto texels = static_cast<double>(eh::WALL_TEXTURE_SIZE) * eh::WALL_TEXTURE_SIZE;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            mean[i][channel] = static_cast<double>(sum[channel]) / texels;
        }
    }

    const auto channels = [](uint32_t color) {
        return std::array<double, 3>{static_cast<double>(color & 0xffu),
                                     static_cast<double>((color >> 8) & 0xffu),
                                     static_cast<double>((color >> 16) & 0xffu)};
    };
    const auto distance_squared = [](const std::array<double, 3> &a,
                                     const std::array<double, 3> &b) {
        double total = 0.0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const double delta = a[channel] - b[channel];
            total += delta * delta;
        }
        return total;
    };

    SECTION("the artwork averages to the colour its own tile declares") {
        // Measured worst case is 11.2 on one channel of the pantry; an exchanged pair misses by
        // more than 100, so this bound separates art that drifted from art that is not its own.
        for (std::size_t i = 0; i < WALLS.size(); ++i) {
            const std::array<double, 3> declared = channels(eh::wall_swatch(WALLS[i]));
            for (std::size_t channel = 0; channel < 3; ++channel) {
                CAPTURE(i, channel, mean[i][channel], declared[channel]);
                CHECK(std::abs(mean[i][channel] - declared[channel]) < 30.0);
            }
        }
    }

    SECTION("no wall resembles another wall's declaration more than its own") {
        for (std::size_t i = 0; i < WALLS.size(); ++i) {
            const double own = distance_squared(mean[i], channels(eh::wall_swatch(WALLS[i])));
            for (std::size_t j = 0; j < WALLS.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const double other = distance_squared(mean[i], channels(eh::wall_swatch(WALLS[j])));
                CAPTURE(i, j, own, other);
                CHECK(own * 4.0 < other);
            }
        }
    }
}

// Finding #63. The wall texture has two independent axes and only one of them was
// pinned. `raycast: wall texture orientation matches on both wall axes` compares
// opposite FACES, so a mutation that treats every face identically cancels on both
// sides of its own comparison -- the same relative-comparison blind spot finding #60
// found in the damage flash. And `raycast: a wall tile shows exactly one full texture
// period` is named for the vertical period but asserts only that the wall's upper and
// lower halves DIFFER, which is one-sided.
//
// Measured against the shipped suite:
//
//   walls drawn upside down                    133/133 PASS -- undetected
//   texture stepped twice as fast down a wall  133/133 PASS -- undetected
//   the vertical cursor frozen (smeared walls)  2 failed -- already covered
//
// The middle one is the sharp case, because it survives a test whose name claims it.
// texture_position is CLAMPED, not wrapped, so at double rate the wall reaches the
// last texel row halfway down and holds it: the bottom half becomes one flat smear,
// which is maximally UNLIKE the top half. The existing "the halves must differ"
// assertion is satisfied by the bug, for the wrong reason.
//
// So the vertical channel had presence (the frozen-cursor case is caught) but neither
// orientation nor scale -- finding #59's presence/extent/shape split, one axis over.
//
// Both sections below read the artwork through the public sampler and the picture
// through the framebuffer, and reproduce no coordinate arithmetic from raycast.cpp.
TEST_CASE("raycast: a wall face steps down its own texture from the top edge") {
    eh::init_textures();
    eh::GameState game;
    eh::reset(game, 0x5eed1234u);

    constexpr int ROOM = 7;
    eh::Map &map = game.level.map;
    map.width = ROOM;
    map.height = ROOM;
    map.tiles.assign(static_cast<std::size_t>(ROOM) * static_cast<std::size_t>(ROOM),
                     eh::Tile::Floor);
    for (int i = 0; i < ROOM; ++i) {
        map.tiles[static_cast<std::size_t>(i)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>((ROOM - 1) * ROOM + i)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>(i * ROOM)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>(i * ROOM + ROOM - 1)] = eh::Tile::WallPantry;
    }
    game.player.x = eh::fx_from_float(3.5f);
    game.player.y = eh::fx_from_float(3.5f);
    game.player.angle = eh::angle_from_deg(0.0);

    TestFramebuffer target;

    // Wall pixels are identified by differing from a render of the same room with no
    // walls in it, rather than by assuming where the renderer puts them.
    eh::GameState empty = game;
    empty.level.map.tiles.assign(static_cast<std::size_t>(ROOM) * static_cast<std::size_t>(ROOM),
                                 eh::Tile::Floor);
    eh::render_walls(empty, target.framebuffer);
    const std::vector<uint32_t> background = target.pixels;

    eh::render_walls(game, target.framebuffer);
    const std::vector<uint32_t> wall = target.pixels;

    int top = -1;
    int bottom = -1;
    for (int y = 0; y < eh::Framebuffer::H; ++y) {
        const std::size_t at = static_cast<std::size_t>(y) * eh::Framebuffer::W + 320;
        if (wall[at] != background[at]) {
            if (top < 0) {
                top = y;
            }
            bottom = y;
        }
    }
    REQUIRE(top >= 0);
    const int height = bottom - top + 1;

    // The face must be flat, or "the top of the wall" is not one texture row across
    // the band, and it must be magnified, or a smear is not resolvable from a texel.
    for (int column = 0; column < eh::Framebuffer::W; ++column) {
        REQUIRE(std::abs(target.depth[static_cast<std::size_t>(column)] - 2.5f) < 0.001f);
    }
    REQUIRE(height > 2 * eh::WALL_TEXTURE_SIZE);

    SECTION("the wall's dark end is the texture's dark end, not its mirror") {
        // The artwork's own vertical asymmetry, measured through the public sampler.
        // The pantry texture carries a dark mortar band across its first rows, so its
        // top decile is materially darker than its bottom decile.
        std::vector<double> texture_profile;
        for (int y = 0; y < eh::WALL_TEXTURE_SIZE; ++y) {
            double sum = 0.0;
            for (int x = 0; x < eh::WALL_TEXTURE_SIZE; ++x) {
                sum += luma(eh::sample_wall(eh::Tile::WallPantry, x, y));
            }
            texture_profile.push_back(sum / eh::WALL_TEXTURE_SIZE);
        }

        std::vector<double> rendered_profile;
        for (int y = top; y <= bottom; ++y) {
            rendered_profile.push_back(row_band_mean(wall, y));
        }

        const double texture_ratio =
            decile_mean(texture_profile, true) / decile_mean(texture_profile, false);
        const double rendered_ratio =
            decile_mean(rendered_profile, true) / decile_mean(rendered_profile, false);
        CAPTURE(texture_ratio, rendered_ratio);

        // Non-vacuity, and a live alarm if the art is ever retouched: if the texture
        // stopped being asymmetric top-to-bottom, no rendering of it could witness a
        // flip, and this test would silently become an assertion about nothing.
        REQUIRE(std::abs(texture_ratio - 1.0) > 0.05);

        // Direction only, deliberately. Magnitude would also move under an unrelated
        // change to the vertical sampling RATE, which the next section owns; keeping
        // this to the sign is what lets each section fail on its own defect.
        CHECK(std::abs(rendered_ratio - 1.0) > 0.05);
        CHECK(((texture_ratio < 1.0) == (rendered_ratio < 1.0)));
    }

    SECTION("no single texture row is smeared down the wall") {
        int longest_run = 1;
        int run = 1;
        int distinct_runs = 1;
        for (int y = top + 1; y <= bottom; ++y) {
            const std::size_t here = static_cast<std::size_t>(y) * eh::Framebuffer::W + 320;
            const std::size_t above = static_cast<std::size_t>(y - 1) * eh::Framebuffer::W + 320;
            if (wall[here] == wall[above]) {
                ++run;
                longest_run = std::max(longest_run, run);
            } else {
                run = 1;
                ++distinct_runs;
            }
        }
        CAPTURE(longest_run, distinct_runs, height);

        // Non-vacuity: a column that is one flat colour, or nearly so, would satisfy
        // any upper bound on its longest run for the wrong reason.
        REQUIRE(distinct_runs > 4);

        // The wall steps CONTINUOUSLY through the texture, so no texel row may occupy
        // a third of the face. Sampling past the last row clamps rather than wraps, so
        // any rate that overruns the texture parks on its final row and smears it over
        // everything below -- measured at 30 of 144 rows on the shared constant
        // against 73 of 144 at double rate.
        CHECK(longest_run * 100 < 35 * height);
    }
}