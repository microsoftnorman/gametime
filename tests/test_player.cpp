#include "core/player.h"
#include "core/state.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace {

constexpr eh::fx PLAYER_RADIUS = eh::FX_ONE / 4;

bool overlaps_wall(const eh::GameState &game) {
    const int min_x = (game.player.x - PLAYER_RADIUS) / eh::FX_ONE;
    const int max_x = (game.player.x + PLAYER_RADIUS - 1) / eh::FX_ONE;
    const int min_y = (game.player.y - PLAYER_RADIUS) / eh::FX_ONE;
    const int max_y = (game.player.y + PLAYER_RADIUS - 1) / eh::FX_ONE;

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (game.level.map.is_wall(x, y)) {
                return true;
            }
        }
    }
    return false;
}

int event_count(const eh::GameState &game, eh::EventType type) {
    return static_cast<int>(std::count_if(
        game.events.begin(), game.events.end(),
        [type](const eh::GameEvent &event) { return event.type == type; }));
}

} // namespace

TEST_CASE("player: walking head-on into a wall does not enter it") {
    eh::GameState game;
    eh::reset(game, 1);
    game.player.x = eh::fx_from_int(8) + eh::FX_ONE / 2;
    game.player.y = eh::fx_from_int(3) + PLAYER_RADIUS;
    game.player.angle = eh::angle_from_deg(270.0);

    const eh::fx starting_x = game.player.x;
    const eh::fx starting_y = game.player.y;
    eh::InputFrame input;
    input.move_y = 1;
    eh::player_tick(game, input);

    REQUIRE(game.player.x == starting_x);
    REQUIRE(game.player.y == starting_y);
    REQUIRE_FALSE(overlaps_wall(game));
}

TEST_CASE("player: walking into a wall at 45 degrees slides along it") {
    eh::GameState game;
    eh::reset(game, 2);
    game.player.x = eh::fx_from_int(8) - PLAYER_RADIUS;
    game.player.y = eh::fx_from_int(4) + eh::FX_ONE / 2;
    game.player.angle = eh::angle_from_deg(45.0);

    const eh::fx starting_x = game.player.x;
    const eh::fx starting_y = game.player.y;
    eh::InputFrame input;
    input.move_y = 1;
    eh::player_tick(game, input);

    REQUIRE(game.player.x == starting_x);
    REQUIRE(game.player.y > starting_y);
    REQUIRE_FALSE(overlaps_wall(game));
}

TEST_CASE("player: repeated movement into a corner never enters a wall") {
    eh::GameState game;
    eh::reset(game, 3);
    game.player.x = eh::fx_from_int(8) - PLAYER_RADIUS;
    game.player.y = eh::fx_from_int(7) - PLAYER_RADIUS;
    game.player.angle = eh::angle_from_deg(45.0);

    eh::InputFrame input;
    input.move_y = 1;
    for (int tick = 0; tick < 600; ++tick) {
        eh::player_tick(game, input);
        REQUIRE_FALSE(overlaps_wall(game));
    }
}

TEST_CASE("player: a large single-tick displacement cannot tunnel through a wall") {
    eh::GameState game;
    eh::reset(game, 4);
    game.player.x = eh::fx_from_int(6) + eh::FX_ONE / 2;
    game.player.y = eh::fx_from_int(4) + eh::FX_ONE / 2;
    game.player.angle = eh::angle_from_deg(0.0);

    eh::InputFrame input;
    input.move_y = INT8_MAX;
    eh::player_tick(game, input);

    REQUIRE(game.player.x == eh::fx_from_int(8) - PLAYER_RADIUS);
    REQUIRE_FALSE(overlaps_wall(game));
}

TEST_CASE("player: firing consumes ammo and respects the cooldown") {
    eh::GameState game;
    eh::reset(game, 5);

    eh::InputFrame input;
    input.buttons = eh::InputFrame::Fire;
    eh::player_tick(game, input);

    REQUIRE(game.player.ammo == 23);
    REQUIRE(game.player.fire_cooldown == 18);
    REQUIRE(game.muzzle_flash == 4);
    REQUIRE(event_count(game, eh::EventType::Shot) == 1);

    for (int tick = 0; tick < 17; ++tick) {
        eh::player_tick(game, input);
        REQUIRE(game.player.ammo == 23);
    }

    REQUIRE(game.player.fire_cooldown == 1);
    eh::player_tick(game, input);
    REQUIRE(game.player.ammo == 22);
    REQUIRE(game.player.fire_cooldown == 18);
    REQUIRE(event_count(game, eh::EventType::Shot) == 2);
}

TEST_CASE("player: firing with zero ammo does nothing") {
    eh::GameState game;
    eh::reset(game, 6);
    game.player.ammo = 0;

    eh::InputFrame input;
    input.buttons = eh::InputFrame::Fire;
    eh::player_tick(game, input);

    REQUIRE(game.player.ammo == 0);
    REQUIRE(game.player.fire_cooldown == 0);
    REQUIRE(game.muzzle_flash == 0);
    REQUIRE(game.events.empty());
}

TEST_CASE("player: an egg behind a wall is not damaged") {
    eh::GameState game;
    eh::reset(game, 7);
    game.player.x = eh::fx_from_int(7) + eh::FX_ONE / 2;
    game.player.y = eh::fx_from_int(2) + eh::FX_ONE / 2;
    game.player.angle = eh::angle_from_deg(0.0);
    eh::Entity &egg = game.entities.front();

    eh::fire(game);

    REQUIRE(egg.health == 60);
    REQUIRE(egg.hit_flash == 0);
    REQUIRE(event_count(game, eh::EventType::Shot) == 1);
    REQUIRE(event_count(game, eh::EventType::EggHit) == 0);
}

TEST_CASE("player: an unobstructed egg takes exactly 34 damage without changing the count") {
    eh::GameState game;
    eh::reset(game, 8);
    eh::Entity &egg = game.entities.front();
    egg.x = eh::fx_from_int(6) + eh::FX_ONE / 2;
    egg.y = game.player.y;
    const int eggs_before = game.eggs_remaining;

    eh::fire(game);

    REQUIRE(egg.health == 26);
    REQUIRE(egg.hit_flash == 9);
    REQUIRE(game.eggs_remaining == eggs_before);
    REQUIRE(event_count(game, eh::EventType::EggHit) == 1);
    REQUIRE(game.events.back().entity_id == egg.id);
}

TEST_CASE("player: identical fixed-point input sequences are deterministic") {
    std::array<eh::InputFrame, 240> inputs{};
    for (std::size_t tick = 0; tick < inputs.size(); ++tick) {
        eh::InputFrame &input = inputs[tick];
        input.move_x = static_cast<int8_t>(static_cast<int>(tick / 20) % 3 - 1);
        input.move_y = static_cast<int8_t>(tick % 50 < 38 ? 1 : -1);
        input.turn = static_cast<int8_t>(tick % 31 == 0 ? 1 : 0);
        input.mouse_dx = static_cast<int16_t>(static_cast<int>(tick % 9) - 4);
        if (tick % 7 < 2) {
            input.buttons = static_cast<uint8_t>(input.buttons | eh::InputFrame::Sprint);
        }
        if (tick % 37 == 0) {
            input.buttons = static_cast<uint8_t>(input.buttons | eh::InputFrame::Fire);
        }
    }

    eh::GameState first;
    eh::GameState second;
    eh::reset(first, 0x13579bdu);
    eh::reset(second, 0x13579bdu);
    for (const eh::InputFrame &input : inputs) {
        eh::player_tick(first, input);
        eh::player_tick(second, input);
    }

    REQUIRE(first.player.x == second.player.x);
    REQUIRE(first.player.y == second.player.y);
    REQUIRE(first.player.angle == second.player.angle);
    REQUIRE(first.player.bob == second.player.bob);
}

// --- Coordinator-added at integration -------------------------------------
// mvp.md section "Tuning" specifies exact move speeds, and nothing asserted
// them: the nine tests above cover collision, sliding, tunnelling and firing,
// but never that the player actually travels at the documented rate. A speed
// regression would have shipped silently.

namespace {

// Distance travelled in one tick from a centred open cell. One tick moves
// ~0.05 tiles, so with a 0.25 radius this can never reach a wall.
double tiles_per_second(int8_t move_x, int8_t move_y) {
    eh::GameState game;
    eh::reset(game, 77);
    game.player.x = eh::fx_from_int(game.player.x / eh::FX_ONE) + eh::FX_ONE / 2;
    game.player.y = eh::fx_from_int(game.player.y / eh::FX_ONE) + eh::FX_ONE / 2;
    game.player.angle = eh::angle_from_deg(0.0);
    REQUIRE_FALSE(overlaps_wall(game));

    const eh::fx x0 = game.player.x;
    const eh::fx y0 = game.player.y;

    eh::InputFrame input;
    input.move_x = move_x;
    input.move_y = move_y;
    eh::player_tick(game, input);

    const double dx = static_cast<double>(game.player.x - x0);
    const double dy = static_cast<double>(game.player.y - y0);
    const double tiles = std::sqrt(dx * dx + dy * dy) / static_cast<double>(eh::FX_ONE);
    return tiles * static_cast<double>(eh::TICKS_PER_SECOND);
}

} // namespace

TEST_CASE("player: move speeds match the tuning table in mvp.md") {
    // Fixed-point steps truncate, so allow 1.5% rather than demanding exactness.
    CHECK(tiles_per_second(0, 1) == Catch::Approx(3.2).epsilon(0.015));  // forward
    CHECK(tiles_per_second(1, 0) == Catch::Approx(2.6).epsilon(0.015));  // strafe
    CHECK(tiles_per_second(0, -1) == Catch::Approx(2.0).epsilon(0.015)); // backward
}

TEST_CASE("player: diagonal movement is deliberately unnormalized") {
    // Forward and strafe are applied as independent components, so holding
    // W+D travels sqrt(3.2^2 + 2.6^2) = 4.12 tiles/s -- about 29% faster than
    // forward alone. mvp.md never specified normalization, and this is exactly
    // the strafe-run behaviour original Doom shipped, so it is kept on purpose.
    // Pinned here so a future change to it is a decision, not an accident.
    const double forward = tiles_per_second(0, 1);
    const double diagonal = tiles_per_second(1, 1);

    REQUIRE(diagonal > forward);
    CHECK(diagonal / forward == Catch::Approx(1.288).epsilon(0.01));
    CHECK(diagonal == Catch::Approx(4.12).epsilon(0.015));
}

