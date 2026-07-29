#include "core/entities.h"
#include "core/state.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

constexpr eh::fx tile_center(int tile) { return eh::fx_from_int(tile) + eh::FX_ONE / 2; }

eh::GameState fresh_game() {
    eh::GameState gs;
    eh::reset(gs, 0x5eed1234u);
    gs.events.clear();
    return gs;
}

eh::Entity &entity_of_type(eh::GameState &gs, eh::EntityType type, std::size_t ordinal = 0) {
    for (eh::Entity &entity : gs.entities) {
        if (entity.type == type) {
            if (ordinal == 0) {
                return entity;
            }
            --ordinal;
        }
    }
    throw std::runtime_error("missing entity in reset game state");
}

void place(eh::Entity &entity, int tile_x, int tile_y) {
    entity.x = tile_center(tile_x);
    entity.y = tile_center(tile_y);
}

void disable_other_eggs(eh::GameState &gs, uint32_t first_id, uint32_t second_id = 0) {
    for (eh::Entity &entity : gs.entities) {
        if (entity.type == eh::EntityType::Egg && entity.id != first_id && entity.id != second_id) {
            entity.alive = false;
        }
    }
}

std::size_t event_count(const eh::GameState &gs, eh::EventType type) {
    return static_cast<std::size_t>(
        std::count_if(gs.events.begin(), gs.events.end(),
                      [type](const eh::GameEvent &event) { return event.type == type; }));
}

uint64_t distance_squared(const eh::Entity &entity, const eh::Player &player) {
    const int64_t dx = static_cast<int64_t>(entity.x) - player.x;
    const int64_t dy = static_cast<int64_t>(entity.y) - player.y;
    return static_cast<uint64_t>(dx * dx + dy * dy);
}

bool overlaps_wall(const eh::GameState &gs, const eh::Entity &entity) {
    constexpr eh::fx radius = (eh::FX_ONE * 3 + 5) / 10;
    const int min_x = (entity.x - radius) / eh::FX_ONE;
    const int max_x = (entity.x + radius - 1) / eh::FX_ONE;
    const int min_y = (entity.y - radius) / eh::FX_ONE;
    const int max_y = (entity.y + radius - 1) / eh::FX_ONE;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (gs.level.map.is_wall(x, y)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("entities: egg death resolves exactly once") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    const uint32_t egg_id = egg.id;
    egg.health = 0;

    eh::entities_tick(gs);

    REQUIRE_FALSE(egg.alive);
    REQUIRE(egg.ai == eh::AiState::Dead);
    REQUIRE(gs.eggs_remaining == 4);
    REQUIRE(event_count(gs, eh::EventType::EggDeath) == 1);
    REQUIRE(std::any_of(gs.events.begin(), gs.events.end(), [egg_id](const eh::GameEvent &event) {
        return event.type == eh::EventType::EggDeath && event.entity_id == egg_id;
    }));

    for (int tick = 0; tick < 240; ++tick) {
        eh::entities_tick(gs);
    }
    REQUIRE(gs.eggs_remaining == 4);
    REQUIRE(event_count(gs, eh::EventType::EggDeath) == 1);
}

TEST_CASE("entities: EggHit authoritatively sets a nine tick flash") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    gs.events.push_back({eh::EventType::EggHit, egg.id});

    eh::entities_tick(gs);

    REQUIRE(egg.hit_flash == 9);
}

TEST_CASE("entities: line of sight gates idle eggs") {
    eh::GameState gs = fresh_game();
    eh::Entity &visible = entity_of_type(gs, eh::EntityType::Egg, 0);
    eh::Entity &hidden = entity_of_type(gs, eh::EntityType::Egg, 1);
    disable_other_eggs(gs, visible.id, hidden.id);

    gs.player.x = tile_center(7);
    gs.player.y = tile_center(2);
    place(visible, 5, 2);
    place(hidden, 12, 2);

    eh::entities_tick(gs);

    REQUIRE(visible.ai == eh::AiState::Chase);
    REQUIRE(hidden.ai == eh::AiState::Idle);
}

// mvp.md fixes the egg sight range at 12 tiles. Widening it to 40 - every egg on the map
// waking at once, in a level whose longest sightline is under 20 - was caught only by the
// replay trajectory digest, which reports that something moved without naming it.
TEST_CASE("entities: eggs wake inside twelve tiles and ignore you beyond it") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    place(egg, 17, 3);
    gs.player.y = tile_center(3);

    SECTION("fourteen tiles away it stays asleep") {
        gs.player.x = tile_center(3);
        eh::entities_tick(gs);
        REQUIRE(egg.ai == eh::AiState::Idle);
    }

    SECTION("nine tiles down the same sightline it wakes") {
        // Control for the section above. Same egg, same row, same walls between them - only
        // the distance changes. Without it, a blocked corridor would satisfy the idle case
        // and the test would pass while asserting nothing about range at all.
        gs.player.x = tile_center(8);
        eh::entities_tick(gs);
        REQUIRE(egg.ai == eh::AiState::Chase);
    }
}

TEST_CASE("entities: a killing blow neither knocks the egg back nor refreshes its flash") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    place(egg, 8, 3);
    gs.player.x = tile_center(5);
    gs.player.y = tile_center(3);

    const eh::fx x_before = egg.x;
    const eh::fx y_before = egg.y;

    // Exactly what player.cpp's fire() writes at the hit site for a lethal shot.
    egg.health = -1;
    egg.hit_flash = 9;
    gs.events.push_back({eh::EventType::EggHit, egg.id});

    eh::entities_tick(gs);

    REQUIRE_FALSE(egg.alive);
    REQUIRE(egg.x == x_before);
    REQUIRE(egg.y == y_before);
    // Measured, not assumed. entities_tick decrements every entity's flash before
    // apply_hit_reactions runs, so a surviving egg is reset to 9 by the reaction path while a
    // fatal hit, which the health guard skips, keeps the already-decremented 8. It is NOT the
    // 9 the player wrote at the hit site.
    REQUIRE(egg.hit_flash == 8);
}

TEST_CASE("entities: chase forgets the player only after the sight grace period") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);

    gs.player.x = tile_center(5);
    gs.player.y = tile_center(2);
    place(egg, 7, 2);
    eh::entities_tick(gs);
    REQUIRE(egg.ai == eh::AiState::Chase);

    gs.player.x = tile_center(12);
    gs.player.y = tile_center(2);
    for (int tick = 0; tick < 180; ++tick) {
        gs.events.clear();
        eh::entities_tick(gs);
        REQUIRE(egg.ai == eh::AiState::Chase);
    }

    gs.events.clear();
    eh::entities_tick(gs);
    REQUIRE(egg.ai == eh::AiState::Idle);
}

TEST_CASE("entities: chasing closes the distance over successive ticks") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    place(egg, 6, 3);
    gs.player.x = tile_center(3);
    gs.player.y = tile_center(3);
    const uint64_t initial_distance = distance_squared(egg, gs.player);

    for (int tick = 0; tick < 20; ++tick) {
        gs.events.clear();
        eh::entities_tick(gs);
    }

    REQUIRE(egg.ai == eh::AiState::Chase);
    REQUIRE(distance_squared(egg, gs.player) < initial_distance);
}

TEST_CASE("entities: wall sliding keeps a chasing egg outside corner walls") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    place(egg, 7, 2);
    egg.ai = eh::AiState::Chase;
    gs.player.x = tile_center(12);
    gs.player.y = tile_center(3);

    for (int tick = 0; tick < 240; ++tick) {
        gs.events.clear();
        eh::entities_tick(gs);
        REQUIRE_FALSE(overlaps_wall(gs, egg));
    }
}

TEST_CASE("entities: contact damage has a 48 tick per-egg cooldown") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    egg.x = gs.player.x + eh::FX_ONE / 2;
    egg.y = gs.player.y;
    egg.ai = eh::AiState::Attack;
    egg.timer = 0;

    eh::entities_tick(gs);
    REQUIRE(gs.player.health == 88);
    REQUIRE(event_count(gs, eh::EventType::PlayerHurt) == 1);

    for (int tick = 1; tick < 48; ++tick) {
        gs.events.clear();
        eh::entities_tick(gs);
        REQUIRE(gs.player.health == 88);
        REQUIRE(event_count(gs, eh::EventType::PlayerHurt) == 0);
    }

    gs.events.clear();
    eh::entities_tick(gs);
    REQUIRE(gs.player.health == 76);
    REQUIRE(event_count(gs, eh::EventType::PlayerHurt) == 1);
}

TEST_CASE("entities: lethal contact emits one lose event") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    egg.x = gs.player.x + eh::FX_ONE / 2;
    egg.y = gs.player.y;
    egg.ai = eh::AiState::Attack;
    egg.timer = 0;
    gs.player.health = 12;

    eh::entities_tick(gs);
    REQUIRE(gs.player.health == 0);
    REQUIRE(event_count(gs, eh::EventType::Lose) == 1);

    for (int tick = 0; tick < 100; ++tick) {
        eh::entities_tick(gs);
    }
    REQUIRE(gs.player.health == 0);
    REQUIRE(event_count(gs, eh::EventType::Lose) == 1);
}

TEST_CASE("entities: pickups apply their effects and caps") {
    eh::GameState gs = fresh_game();

    SECTION("jellybean adds ten ammo") {
        eh::Entity &jellybean = entity_of_type(gs, eh::EntityType::Jellybean);
        jellybean.x = gs.player.x;
        jellybean.y = gs.player.y;
        gs.player.ammo = 24;

        eh::entities_tick(gs);

        REQUIRE(gs.player.ammo == 34);
        REQUIRE_FALSE(jellybean.alive);
        REQUIRE(event_count(gs, eh::EventType::Pickup) == 1);
    }

    SECTION("jellybean respects the ammo cap") {
        eh::Entity &jellybean = entity_of_type(gs, eh::EntityType::Jellybean);
        jellybean.x = gs.player.x;
        jellybean.y = gs.player.y;
        gs.player.ammo = 55;

        eh::entities_tick(gs);

        REQUIRE(gs.player.ammo == 60);
        REQUIRE_FALSE(jellybean.alive);
    }

    SECTION("carrot adds twenty-five health") {
        eh::Entity &carrot = entity_of_type(gs, eh::EntityType::Carrot);
        carrot.x = gs.player.x;
        carrot.y = gs.player.y;
        gs.player.health = 50;

        eh::entities_tick(gs);

        REQUIRE(gs.player.health == 75);
        REQUIRE_FALSE(carrot.alive);
        REQUIRE(event_count(gs, eh::EventType::Pickup) == 1);
    }

    SECTION("carrot respects the health cap") {
        eh::Entity &carrot = entity_of_type(gs, eh::EntityType::Carrot);
        carrot.x = gs.player.x;
        carrot.y = gs.player.y;
        gs.player.health = 90;

        eh::entities_tick(gs);

        REQUIRE(gs.player.health == 100);
        REQUIRE_FALSE(carrot.alive);
    }
}

TEST_CASE("entities: basket wins only after every egg is cleared") {
    eh::GameState gs = fresh_game();
    eh::Entity &basket = entity_of_type(gs, eh::EntityType::Basket);
    gs.player.x = basket.x;
    gs.player.y = basket.y;

    eh::entities_tick(gs);
    REQUIRE(event_count(gs, eh::EventType::Win) == 0);

    gs.events.clear();
    gs.eggs_remaining = 0;
    eh::entities_tick(gs);
    REQUIRE(event_count(gs, eh::EventType::Win) == 1);
}

TEST_CASE("entities: stable id ordering survives many ticks") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    place(egg, 6, 3);
    std::vector<uint32_t> ids;
    ids.reserve(gs.entities.size());
    for (const eh::Entity &entity : gs.entities) {
        ids.push_back(entity.id);
    }

    for (int tick = 0; tick < 600; ++tick) {
        gs.events.clear();
        eh::entities_tick(gs);
    }

    std::vector<uint32_t> final_ids;
    final_ids.reserve(gs.entities.size());
    for (const eh::Entity &entity : gs.entities) {
        final_ids.push_back(entity.id);
    }
    REQUIRE(final_ids == ids);
    REQUIRE(std::is_sorted(final_ids.begin(), final_ids.end()));
}

// --- Coordinator-added at integration -------------------------------------
// mvp.md gives the Egg a 1.8 tiles/s move speed, and while "chasing closes the
// distance" proves direction, nothing proved the rate. The same gap existed on
// the player side and a 7.7% speed regression there passed the whole suite.
TEST_CASE("entities: chase speed matches the tuning table in mvp.md") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    place(egg, 6, 3);
    egg.ai = eh::AiState::Chase;
    gs.player.x = tile_center(3);
    gs.player.y = tile_center(3);

    const eh::fx start_x = egg.x;
    const eh::fx start_y = egg.y;

    // 20 ticks covers ~0.6 tiles, well short of the 0.7-tile attack range at
    // which the egg would stop closing and the measurement would go flat.
    constexpr int TICKS = 20;
    for (int tick = 0; tick < TICKS; ++tick) {
        gs.events.clear();
        eh::entities_tick(gs);
    }

    REQUIRE(egg.ai == eh::AiState::Chase);

    const double dx = static_cast<double>(egg.x - start_x);
    const double dy = static_cast<double>(egg.y - start_y);
    const double tiles = std::sqrt(dx * dx + dy * dy) / static_cast<double>(eh::FX_ONE);
    const double tiles_per_second =
        tiles * static_cast<double>(eh::TICKS_PER_SECOND) / static_cast<double>(TICKS);

    CHECK(tiles_per_second == Catch::Approx(1.8).epsilon(0.015));
}
