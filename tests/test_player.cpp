#include "core/player.h"
#include "core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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
