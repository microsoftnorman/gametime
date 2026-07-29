#include "core/fixed.h"
#include "core/framebuffer.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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

namespace {

int pending_events(const eh::GameState &gs, eh::EventType type) {
    int count = 0;
    for (const eh::GameEvent &event : gs.events) {
        count += event.type == type ? 1 : 0;
    }
    return count;
}

float tiles_between(eh::fx ax, eh::fx ay, eh::fx bx, eh::fx by) {
    const float dx = eh::fx_to_float(static_cast<eh::fx>(bx - ax));
    const float dy = eh::fx_to_float(static_cast<eh::fx>(by - ay));
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

// entities_tick emits Win and Lose, and the HUD draws the Won and Lost screens, but until this
// test nothing asserted that the events ever reach the screen. Both endpoints were covered and
// the wire between them was not: deleting either transition left the game unwinnable and
// unloseable with the whole suite green.
TEST_CASE("state: winning requires clearing every egg and reaching the basket") {
    eh::GameState game;
    eh::reset(game, 1u);
    const eh::InputFrame idle;

    const eh::fx basket_x = game.level.basket.x;
    const eh::fx basket_y = game.level.basket.y;
    const eh::fx spawn_x = game.player.x;
    const eh::fx spawn_y = game.player.y;

    // The two halves of the objective have to be separable or this test cannot tell them apart.
    REQUIRE(tiles_between(spawn_x, spawn_y, basket_x, basket_y) > 2.0f);

    SECTION("standing on the basket with eggs alive is not a win") {
        game.player.x = basket_x;
        game.player.y = basket_y;
        eh::tick(game, idle);

        REQUIRE(game.eggs_remaining == 5);
        REQUIRE(pending_events(game, eh::EventType::Win) == 0);
        REQUIRE(game.screen == eh::Screen::Playing);
    }

    SECTION("clearing every egg away from the basket is not a win") {
        for (eh::Entity &entity : game.entities) {
            if (entity.type == eh::EntityType::Egg) {
                entity.health = 0;
            }
        }
        eh::tick(game, idle);

        REQUIRE(game.eggs_remaining == 0);
        REQUIRE(pending_events(game, eh::EventType::Win) == 0);
        REQUIRE(game.screen == eh::Screen::Playing);
    }

    SECTION("both together end the game on the winning screen") {
        for (eh::Entity &entity : game.entities) {
            if (entity.type == eh::EntityType::Egg) {
                entity.health = 0;
            }
        }
        eh::tick(game, idle);
        REQUIRE(game.eggs_remaining == 0);
        REQUIRE(game.screen == eh::Screen::Playing);

        game.player.x = basket_x;
        game.player.y = basket_y;
        eh::tick(game, idle);

        REQUIRE(pending_events(game, eh::EventType::Win) == 1);
        REQUIRE(game.screen == eh::Screen::Won);
    }
}

TEST_CASE("state: losing the last of your health ends the game and announces it") {
    eh::GameState game;
    eh::reset(game, 1u);
    const eh::InputFrame idle;

    // Park an egg inside its own attack range and leave the player one point of health, so the
    // kill arrives through the real AI and damage path rather than by assignment.
    eh::Entity *attacker = nullptr;
    for (eh::Entity &entity : game.entities) {
        if (entity.type == eh::EntityType::Egg) {
            attacker = &entity;
            break;
        }
    }
    REQUIRE(attacker != nullptr);
    attacker->x = static_cast<eh::fx>(game.player.x + eh::FX_ONE / 4);
    attacker->y = game.player.y;
    game.player.health = 1;

    int ticks = 0;
    while (game.screen == eh::Screen::Playing && ticks < 600) {
        eh::tick(game, idle);
        ++ticks;
    }

    REQUIRE(ticks < 600);
    REQUIRE(game.screen == eh::Screen::Lost);
    REQUIRE(game.player.health == 0);
    // The same tick must still carry the event, or the defeat sound never plays.
    REQUIRE(pending_events(game, eh::EventType::Lose) == 1);
    // tick() reaches this screen by two redundant routes: the direct health check and the Lose
    // event. Measured: disabling either one alone still loses the game, so this asserts the
    // outcome rather than one branch. Disabling both is what it catches.
}

// `wall_tile_for` assigns one of four wall textures by map quadrant -- the last of the enum-to-art
// mappings in this codebase, after the HUD screens, the wall swatches and the sprite dimensions.
// Exchanging two quadrants is caught, but only by the 600-tick replay determinism golden, which
// reports a changed digest and names nothing: the burrow could be panelled like the cellar and the
// only signal would be "the world differs from the recorded one".
//
// The oracle here is level geography rather than the mapping itself, so this is not the mapping
// restated. Measured on the shipped level: the map is 24x18, the player spawns at (3.5, 3.5) and
// the basket stands at (18.5, 13.5) -- opposite corners. The burrow is where the bunny starts and
// the basket-themed walls are where the basket actually is, which is a claim about the game rather
// than about the switch.
//
// Stated limitation, because a known gap beats a test that pretends to close it: nothing in this
// level distinguishes the pantry from the cellar. Eggs and pickups are spread evenly across both
// (NE 2 eggs / 1 jellybean / 1 carrot, SW 1 / 1 / 1), so exchanging *those two* quadrants is the
// one pairwise exchange this test cannot see. It remains covered only by the replay digest. Giving
// either room a themed prop -- a pantry shelf, a cellar barrel -- would anchor it here too.
TEST_CASE("map: the burrow is where the bunny starts and the basket walls are where it ends") {
    eh::GameState game;
    eh::reset(game, 0x5eed1234u);
    const eh::Map &map = game.level.map;

    // The corner of the map nearest a point, by plain distance -- no quadrant arithmetic.
    const auto nearest_corner = [&map](eh::fx x, eh::fx y) {
        const float fx_x = eh::fx_to_float(x);
        const float fx_y = eh::fx_to_float(y);
        const int corner_x = fx_x < static_cast<float>(map.width - 1) - fx_x ? 0 : map.width - 1;
        const int corner_y = fx_y < static_cast<float>(map.height - 1) - fx_y ? 0 : map.height - 1;
        return map.at(corner_x, corner_y);
    };

    SECTION("each landmark is walled in the texture named after it") {
        CHECK(nearest_corner(game.player.x, game.player.y) == eh::Tile::WallBurrow);
        CHECK(nearest_corner(game.level.basket.x, game.level.basket.y) == eh::Tile::WallBasket);
    }

    SECTION("the two landmarks are in different corners, so the check above is not trivial") {
        const int player_corner_x =
            eh::fx_to_float(game.player.x) * 2.0f < static_cast<float>(map.width) ? 0 : 1;
        const int basket_corner_x =
            eh::fx_to_float(game.level.basket.x) * 2.0f < static_cast<float>(map.width) ? 0 : 1;
        CHECK(player_corner_x != basket_corner_x);
        CHECK(nearest_corner(game.player.x, game.player.y) !=
              nearest_corner(game.level.basket.x, game.level.basket.y));
    }

    SECTION("all four corners are walls, and all four are different") {
        const eh::Tile north_west = map.at(0, 0);
        const eh::Tile north_east = map.at(map.width - 1, 0);
        const eh::Tile south_west = map.at(0, map.height - 1);
        const eh::Tile south_east = map.at(map.width - 1, map.height - 1);
        for (eh::Tile corner : {north_west, north_east, south_west, south_east}) {
            CHECK(corner != eh::Tile::Floor);
        }
        // Collapse -- two quadrants sharing one texture -- is the failure this can see. An
        // exchange is not; that is what the section above is for.
        CHECK(north_west != north_east);
        CHECK(north_west != south_west);
        CHECK(north_west != south_east);
        CHECK(north_east != south_west);
        CHECK(north_east != south_east);
        CHECK(south_west != south_east);
    }
}
