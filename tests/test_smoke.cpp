#include "core/fixed.h"
#include "core/framebuffer.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

TEST_CASE("fixed-point conversions and arithmetic are stable") {
    REQUIRE(eh::fx_from_int(7) == 7 * eh::FX_ONE);
    REQUIRE(eh::fx_to_float(eh::fx_from_int(-3)) == -3.0f);
    REQUIRE(eh::fx_from_float(1.5f) == eh::FX_ONE + eh::FX_ONE / 2);
    REQUIRE(eh::fx_mul(eh::fx_from_int(3), eh::fx_from_float(0.5f)) == eh::fx_from_float(1.5f));
    REQUIRE(eh::fx_div(eh::fx_from_int(3), eh::fx_from_int(2)) == eh::fx_from_float(1.5f));
}

TEST_CASE("fixed-point trigonometry recognizes cardinal angles") {
    REQUIRE(eh::fx_sin(eh::angle_from_deg(0.0)) == 0);
    REQUIRE(eh::fx_sin(eh::angle_from_deg(90.0)) == eh::FX_ONE);
    REQUIRE(eh::fx_cos(eh::angle_from_deg(0.0)) == eh::FX_ONE);
    REQUIRE(eh::fx_cos(eh::angle_from_deg(180.0)) == -eh::FX_ONE);
}

TEST_CASE("xorshift32 has a deterministic known sequence") {
    eh::Rng rng{0x12345678u};
    REQUIRE(eh::next(rng) == 0x87985aa5u);
    REQUIRE(eh::next(rng) == 0x155b24a3u);
    REQUIRE(eh::next(rng) == 0x4820f4c4u);
    REQUIRE(eh::next(rng) == 0x81b3ac98u);
}

TEST_CASE("embedded burrow parses with every spawn") {
    const eh::MapParseResult result = eh::parse_map(eh::BURROW_01);
    REQUIRE(result.ok);
    REQUIRE(result.data.map.width == 24);
    REQUIRE(result.data.map.height == 18);
    REQUIRE(result.data.eggs.size() == 5);
    REQUIRE(result.data.jellybeans.size() == 3);
    REQUIRE(result.data.carrots.size() == 2);
    REQUIRE(std::count(result.data.map.tiles.begin(), result.data.map.tiles.end(),
                       eh::Tile::Floor) == 248);
    REQUIRE(result.data.player.x == eh::fx_from_int(3) + eh::FX_ONE / 2);
    REQUIRE(result.data.player.y == eh::fx_from_int(3) + eh::FX_ONE / 2);
}

TEST_CASE("map parser rejects a hole in the border") {
    std::string malformed = eh::BURROW_01;
    malformed[0] = '.';

    const eh::MapParseResult result = eh::parse_map(malformed);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("row 1, col 1") != std::string::npos);
}

TEST_CASE("map parser enforces every structural invariant") {
    SECTION("rectangular rows") {
        const eh::MapParseResult result = eh::parse_map("#####\n#P.E#\n#..X#\n####");
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.find("row 4, col 5") != std::string::npos);
    }

    SECTION("exactly one player") {
        const eh::MapParseResult duplicate = eh::parse_map("#######\n#PPE.X#\n#.....#\n#######");
        REQUIRE_FALSE(duplicate.ok);
        REQUIRE(duplicate.error.find("duplicate player") != std::string::npos);

        const eh::MapParseResult missing = eh::parse_map("#####\n#.E.#\n#..X#\n#####");
        REQUIRE_FALSE(missing.ok);
        REQUIRE(missing.error.find("exactly one player") != std::string::npos);
    }

    SECTION("at least one egg") {
        const eh::MapParseResult result = eh::parse_map("#####\n#P..#\n#..X#\n#####");
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.find("at least one Egg") != std::string::npos);
    }

    SECTION("exactly one basket") {
        const eh::MapParseResult duplicate = eh::parse_map("#######\n#P.EXX#\n#.....#\n#######");
        REQUIRE_FALSE(duplicate.ok);
        REQUIRE(duplicate.error.find("duplicate Basket") != std::string::npos);

        const eh::MapParseResult missing = eh::parse_map("#####\n#P.E#\n#...#\n#####");
        REQUIRE_FALSE(missing.ok);
        REQUIRE(missing.error.find("exactly one Basket") != std::string::npos);
    }

    SECTION("all open cells reachable") {
        const eh::MapParseResult result =
            eh::parse_map("#######\n#P.E.X#\n#######\n###.###\n#######");
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.find("row 4, col 4") != std::string::npos);
        REQUIRE(result.error.find("unreachable") != std::string::npos);
    }
}

TEST_CASE("framebuffer colors use RGBA byte order") {
    REQUIRE(eh::rgba(0x11, 0x22, 0x33, 0x44) == 0x44332211u);
}

TEST_CASE("screen dispatch starts and restarts a fresh game") {
    eh::GameState game;
    eh::InputFrame input;
    input.buttons = eh::InputFrame::Start;
    eh::tick(game, input);

    REQUIRE(game.screen == eh::Screen::Playing);
    REQUIRE(game.eggs_remaining == 5);
    REQUIRE(game.entities.size() == 11);
    REQUIRE(std::is_sorted(
        game.entities.begin(), game.entities.end(),
        [](const eh::Entity &lhs, const eh::Entity &rhs) { return lhs.id < rhs.id; }));

    game.screen = eh::Screen::Lost;
    game.player.health = 0;
    input.buttons = eh::InputFrame::Restart;
    eh::tick(game, input);

    REQUIRE(game.screen == eh::Screen::Playing);
    REQUIRE(game.player.health == 100);
    REQUIRE(game.tick == 0);
}
