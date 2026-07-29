#include "core/player.h"
#include "core/state.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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
    return static_cast<int>(
        std::count_if(game.events.begin(), game.events.end(),
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

// mvp.md fixes the weapon's max range at 20 tiles. Cutting it to 4 passed all 98 tests -
// not even the replay trajectory digest noticed - which would leave the gun useless down the
// map's longest corridor while every close-range test stayed green, because the existing
// shooting tests all fire from about three tiles away.
//
// The upper bound is deliberately not asserted. At 20 tiles the limit is longer than any open
// sightline in this level, so it is unobservable here; claiming to test it would be a fiction.
TEST_CASE("player: a shot carries the full length of the longest corridor") {
    eh::GameState game;
    eh::reset(game, 9);
    eh::Entity &egg = game.entities.front();
    for (std::size_t i = 1; i < game.entities.size(); ++i) {
        game.entities[i].alive = false;
    }
    egg.x = eh::fx_from_int(22) + eh::FX_ONE / 2;
    egg.y = game.player.y;

    eh::fire(game);

    REQUIRE(egg.health == 26);
    REQUIRE(event_count(game, eh::EventType::EggHit) == 1);
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

TEST_CASE("player: bob is a normalized phase advanced only by real displacement") {
    // Two workstreams read this field differently at integration: the producer writes a
    // normalized 0..1 cycle in Q12, while a radians reading sweeps only about one radian
    // before wrapping. The consumer side is pinned in test_sprites.cpp; this pins the
    // producer, so a violation names the field instead of surfacing as an opaque replay
    // digest mismatch.
    constexpr eh::fx BOB_STEP = eh::FX_ONE / 12;
    REQUIRE(BOB_STEP == 341);

    eh::GameState game;
    eh::reset(game, 1);
    game.player.angle = eh::angle_from_deg(0.0);
    REQUIRE(game.player.bob == 0);

    eh::InputFrame walking;
    walking.move_y = 1;

    const eh::fx before_x = game.player.x;
    eh::player_tick(game, walking);
    REQUIRE(game.player.x != before_x);
    REQUIRE(game.player.bob == BOB_STEP);

    // The step truncates, so twelve ticks reaches 4092 rather than wrapping cleanly to
    // zero: the phase drifts slightly on every cycle. Assert the modular arithmetic, not
    // a tidy twelve-tick period that does not actually exist.
    for (int tick = 2; tick <= 40; ++tick) {
        eh::player_tick(game, walking);
        REQUIRE(game.player.bob == static_cast<eh::fx>((BOB_STEP * tick) % eh::FX_ONE));
        REQUIRE(game.player.bob >= 0);
        REQUIRE(game.player.bob < eh::FX_ONE);
    }

    // Standing still holds the phase. There is no decay and no reset to zero.
    const eh::fx held = game.player.bob;
    REQUIRE(held != 0);
    const eh::InputFrame idle;
    for (int tick = 0; tick < 5; ++tick) {
        eh::player_tick(game, idle);
    }
    REQUIRE(game.player.bob == held);
}

TEST_CASE("player: bob does not advance while movement is fully blocked") {
    eh::GameState game;
    eh::reset(game, 1);
    game.player.x = eh::fx_from_int(8) + eh::FX_ONE / 2;
    game.player.y = eh::fx_from_int(3) + PLAYER_RADIUS;
    game.player.angle = eh::angle_from_deg(270.0);
    game.player.bob = eh::FX_ONE / 3;

    const eh::fx starting_x = game.player.x;
    const eh::fx starting_y = game.player.y;
    eh::InputFrame walking;
    walking.move_y = 1;
    for (int tick = 0; tick < 10; ++tick) {
        eh::player_tick(game, walking);
    }

    // Held against a wall the player never displaces, so the weapon must not bob.
    REQUIRE(game.player.x == starting_x);
    REQUIRE(game.player.y == starting_y);
    REQUIRE(game.player.bob == eh::FX_ONE / 3);
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
double tiles_per_second(int8_t move_x, int8_t move_y, uint8_t buttons = 0) {
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
    input.buttons = buttons;
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

// mvp.md's tuning table specifies "Sprint multiplier | 1.5", and the controls table binds it
// to Shift. Setting SPRINT_NUMERATOR from 3 to 2 - so sprint multiplies by exactly 1.0 and the
// Shift key does nothing at all - was caught only by the replay trajectory digest, which
// reports that something moved without naming what. The Sprint button appears elsewhere in
// this file only inside a determinism equality, which holds no matter what the button does.
TEST_CASE("player: sprint multiplies every axis by the tuning table's 1.5") {
    const double walk = tiles_per_second(0, 1);
    const double sprint = tiles_per_second(0, 1, eh::InputFrame::Sprint);

    // Ratio first: this is what the table actually specifies, and it survives a deliberate
    // retune of the base speeds. The absolute values below then pin those base speeds too.
    REQUIRE(sprint > walk);
    CHECK(sprint / walk == Catch::Approx(1.5).epsilon(0.015));

    CHECK(sprint == Catch::Approx(4.8).epsilon(0.015)); // forward
    CHECK(tiles_per_second(1, 0, eh::InputFrame::Sprint) ==
          Catch::Approx(3.9).epsilon(0.015)); // strafe
    CHECK(tiles_per_second(0, -1, eh::InputFrame::Sprint) ==
          Catch::Approx(3.0).epsilon(0.015)); // back
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

TEST_CASE("player: turn rates match the tuning table in mvp.md") {
    // angle_t is a uint16 turn: 65536 units == one full revolution.
    constexpr double UNITS_TO_RAD = 6.283185307179586 / 65536.0;

    SECTION("keyboard turn is 2.6 rad/s") {
        eh::GameState game;
        eh::reset(game, 91);
        const eh::angle_t start = game.player.angle;

        eh::InputFrame input;
        input.turn = 1;
        for (int tick = 0; tick < eh::TICKS_PER_SECOND; ++tick) {
            eh::player_tick(game, input);
        }

        const double radians_per_second =
            static_cast<double>(static_cast<eh::angle_t>(game.player.angle - start)) * UNITS_TO_RAD;
        CHECK(radians_per_second == Catch::Approx(2.6).epsilon(0.01));
    }

    SECTION("mouse is 0.0022 rad per count") {
        eh::GameState game;
        eh::reset(game, 92);
        const eh::angle_t start = game.player.angle;

        eh::InputFrame input;
        input.mouse_dx = 100;
        eh::player_tick(game, input);

        const double radians_per_count =
            static_cast<double>(static_cast<eh::angle_t>(game.player.angle - start)) *
            UNITS_TO_RAD / 100.0;
        CHECK(radians_per_count == Catch::Approx(0.0022).epsilon(0.01));
    }

    SECTION("turning is symmetric and reversible") {
        eh::GameState game;
        eh::reset(game, 93);
        const eh::angle_t start = game.player.angle;

        eh::InputFrame left;
        left.turn = 1;
        eh::InputFrame right;
        right.turn = -1;
        for (int tick = 0; tick < 25; ++tick) {
            eh::player_tick(game, left);
        }
        REQUIRE(game.player.angle != start);
        for (int tick = 0; tick < 25; ++tick) {
            eh::player_tick(game, right);
        }
        CHECK(game.player.angle == start);
    }
}
