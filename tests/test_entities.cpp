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

// --- Coordinator-added post-ship ------------------------------------------
// mvp.md line 173 gives both pickups a 0.4-tile radius and line 179 gives the
// Basket 0.5 tiles. Every existing test for either placed the target *exactly*
// on the player, so distance was always zero and neither radius was pinned at
// all. Measured: shrinking PICKUP_RANGE to 0.01 tiles -- pickups you can walk
// straight through -- passed 107/107, and BASKET_RANGE passed 107/107 at both
// 0.01 and 6.0 tiles. At 6.0 the game is won the instant the last egg dies,
// which deletes the second half of the objective; the test written to pin that
// two-stage win survived because its separability guard only asks for >2 tiles
// apart while the real spawn-to-basket distance is 18.03.
//
// Both radii are pinned from both sides. The control is that only the target
// moves between the two halves of each section: same entity, same map, same
// walls, so "not collected" cannot be explained by anything but distance.
//
// The pickup sections deactivate every other pickup first. Without that, an
// over-wide radius sweeps up the neighbouring jellybeans too and fails the
// *inside* section as well -- a true failure but for the wrong reason, which
// per finding #24 is indistinguishable from coverage until you read the
// expansion. Isolated, each of the four mutants kills exactly one section.
TEST_CASE("entities: pickup and basket reach match the radii in mvp.md") {
    auto isolate_pickup = [](eh::GameState &gs, uint32_t keep_id) {
        for (eh::Entity &entity : gs.entities) {
            if (entity.id != keep_id && (entity.type == eh::EntityType::Jellybean ||
                                         entity.type == eh::EntityType::Carrot)) {
                entity.alive = false;
            }
        }
    };

    SECTION("a jellybean is collected just inside 0.4 tiles") {
        eh::GameState gs = fresh_game();
        eh::Entity &jellybean = entity_of_type(gs, eh::EntityType::Jellybean);
        isolate_pickup(gs, jellybean.id);
        jellybean.x = gs.player.x + eh::fx_from_float(0.35f);
        jellybean.y = gs.player.y;
        gs.player.ammo = 24;

        eh::entities_tick(gs);

        REQUIRE(gs.player.ammo == 34);
        REQUIRE_FALSE(jellybean.alive);
    }

    SECTION("the same jellybean is out of reach just outside 0.4 tiles") {
        eh::GameState gs = fresh_game();
        eh::Entity &jellybean = entity_of_type(gs, eh::EntityType::Jellybean);
        isolate_pickup(gs, jellybean.id);
        jellybean.x = gs.player.x + eh::fx_from_float(0.45f);
        jellybean.y = gs.player.y;
        gs.player.ammo = 24;

        eh::entities_tick(gs);

        REQUIRE(gs.player.ammo == 24);
        REQUIRE(jellybean.alive);
        REQUIRE(event_count(gs, eh::EventType::Pickup) == 0);
    }

    SECTION("the basket is reached just inside 0.5 tiles") {
        eh::GameState gs = fresh_game();
        eh::Entity &basket = entity_of_type(gs, eh::EntityType::Basket);
        gs.player.x = basket.x + eh::fx_from_float(0.45f);
        gs.player.y = basket.y;
        gs.eggs_remaining = 0;

        eh::entities_tick(gs);

        REQUIRE(event_count(gs, eh::EventType::Win) == 1);
    }

    SECTION("the basket is out of reach just outside 0.5 tiles") {
        eh::GameState gs = fresh_game();
        eh::Entity &basket = entity_of_type(gs, eh::EntityType::Basket);
        gs.player.x = basket.x + eh::fx_from_float(0.55f);
        gs.player.y = basket.y;
        gs.eggs_remaining = 0;

        eh::entities_tick(gs);

        REQUIRE(event_count(gs, eh::EventType::Win) == 0);
    }
}

// mvp.md line 166 accepts greedy chase with wall sliding and no pathfinding,
// arguing it is "indistinguishable from smart behaviour" on this grid. The
// existing corner test asserts only that a chasing egg never overlaps a wall --
// which a *stationary* egg also satisfies. Measured in that fixture: the egg
// travels 0.12 tiles in its 240 ticks, so the safety assertion was passing on an
// egg that had effectively stopped, and nothing in the suite required a chasing
// egg to ever arrive.
//
// The x=4 and x=18 corridors are the only links between the map's halves, so
// "can an egg get through a one-tile gap" is the contract that decides whether
// eggs are a threat at all. Measured on main: from (2.5, 5.5) the egg rounds the
// corner and reaches attack range at tick 666. Nominal straight-line time is
// ~2.3 s, so cornering costs roughly 4.8x -- the axis-separated slide only ever
// applies the free axis's *component* of the speed, and that component shrinks
// as the egg aligns with the player. Slow, but it does arrive; an earlier
// 600-tick probe of mine reported "blocked" purely because the budget was 66
// ticks too short.
//
// The bound below is that measurement plus headroom, not a guess. Deleting
// either sliding axis leaves the egg pinned at the corridor mouth forever.
TEST_CASE("entities: a chasing egg rounds a corner into a one-tile corridor") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    egg.x = eh::fx_from_float(2.5f);
    egg.y = eh::fx_from_float(5.5f);
    gs.player.x = eh::fx_from_float(4.5f);
    gs.player.y = eh::fx_from_float(10.5f);

    // The two are in different halves of the map, so arrival is only possible
    // through the corridor -- a straight line between them crosses solid wall.
    REQUIRE(gs.level.map.is_wall(2, 7));
    REQUIRE(gs.level.map.is_wall(3, 7));
    REQUIRE_FALSE(gs.level.map.is_wall(4, 7));

    constexpr int BUDGET = 900;
    auto distance_tiles = [&] {
        const double dx = static_cast<double>(egg.x) - static_cast<double>(gs.player.x);
        const double dy = static_cast<double>(egg.y) - static_cast<double>(gs.player.y);
        return std::sqrt(dx * dx + dy * dy) / static_cast<double>(eh::FX_ONE);
    };
    REQUIRE(distance_tiles() > 5.0);

    int arrived = -1;
    for (int tick = 0; tick < BUDGET; ++tick) {
        // Hold the egg awake so this measures movement alone; the sight range
        // and lost-sight grace period are pinned by their own tests.
        egg.ai = eh::AiState::Chase;
        gs.events.clear();
        eh::entities_tick(gs);
        REQUIRE_FALSE(overlaps_wall(gs, egg));
        if (arrived < 0 && distance_tiles() <= 0.75) {
            arrived = tick;
        }
    }

    INFO("egg finished at " << static_cast<double>(egg.x) / eh::FX_ONE << ", "
                            << static_cast<double>(egg.y) / eh::FX_ONE);
    REQUIRE(arrived >= 0);
    // It must also not be trivially close already: the corridor really was crossed.
    REQUIRE(arrived > 60);
}

// `has_line_of_sight` walks the grid and, when the ray crosses an x and a y line at the same
// distance, refuses to pass if either orthogonal neighbour of that corner is solid -- an egg may
// not see between two tiles meeting at a point. Disabling that check left the suite at 111/111.
//
// Reachability was measured, not assumed. Sweeping every ordered pair of open tiles at two
// sub-tile offsets -- 123,008 pairs -- the guard changes the outcome for 116 of them. That is two
// orders of magnitude more reachable than the same hole in the weapon raycast, because a
// 45-degree diagonal between two tile centres lands exactly on a corner every time, and it is
// the shape of unfairness a player notices: being spotted through a wall.
//
// The weapon raycast tests a third tile at its corner (the diagonal destination) where this one
// tests two. That is not a divergence: the destination is rejected by the `is_wall(cell_x,
// cell_y)` check at the top of the following iteration.
TEST_CASE("entities: a sightline cannot squeeze through the corner between two walls") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);
    egg.ai = eh::AiState::Idle;

    // Both diagonals below span one tile on each axis, well inside sight range, so the only
    // difference between the sections is whether the corner they cross is solid.
    REQUIRE(gs.level.map.is_wall(8, 2));
    REQUIRE_FALSE(gs.level.map.is_wall(7, 3));
    REQUIRE_FALSE(gs.level.map.is_wall(6, 2));
    REQUIRE_FALSE(gs.level.map.is_wall(5, 3));

    SECTION("the wall corner blocks it") {
        place(egg, 7, 2);
        gs.player.x = tile_center(8);
        gs.player.y = tile_center(3);

        eh::entities_tick(gs);

        CHECK(egg.ai == eh::AiState::Idle);
    }

    SECTION("an equally distant open diagonal is seen") {
        place(egg, 5, 2);
        gs.player.x = tile_center(6);
        gs.player.y = tile_center(3);

        eh::entities_tick(gs);

        CHECK(egg.ai == eh::AiState::Chase);
    }
}

// Finding #56. `entities_tick` treats the tick that kills the player as terminal: once health
// reaches zero it emits Lose and returns, so the pickup loop below never runs and no further egg
// may strike. Both halves of that contract passed 127/127 when removed.
//
// The pickup half is the severe one, measured rather than argued: with the early `return` deleted,
// a player killed while standing on a carrot ends the tick at 25 health with the carrot consumed
// and a cheerful Pickup event riding alongside Lose -- you die, heal, and hear the pickup jingle
// over the defeat sting. `entities: lethal contact emits one lose event` asserts exactly the right
// thing (`health == 0`, even 100 ticks later) and could not see it, because its fixture places
// nothing within PICKUP_RANGE of the dying player. Finding #38's shape again: the assertion was
// right and the fixture made the code under test irrelevant.
//
// The corpse half is honestly narrower. Dropping `gs.player.health <= 0` from attack_player's
// guard lets a second contact egg strike the same tick, which yields a duplicate PlayerHurt event
// and burns that egg's cooldown. It is *not* audible today -- main.cpp plays one sound per event
// type per tick (finding #26) and hurt_flash is already saturated -- so this pins the event
// contract, not something on screen. Said plainly rather than dressed up.
//
// Not covered, and unreachable rather than untested: the same `return` also guards the Win check.
// Reaching it dead requires eggs_remaining == 0 while an egg is alive enough to land a killing
// blow, and eggs_remaining tracks the live count exactly, so no such state exists.
//
// Every negative here is paired with a positive control on the same instrument (finding #48's
// rule): the carrot must genuinely be in reach, and both eggs must genuinely be able to strike,
// or "it was not collected" and "only one hurt event" are satisfied by a fixture that reaches
// nothing.
TEST_CASE("entities: the tick that kills you is the last thing that happens to you") {
    eh::GameState gs = fresh_game();

    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    eh::Entity &second_egg = entity_of_type(gs, eh::EntityType::Egg, 1);
    disable_other_eggs(gs, egg.id, second_egg.id);
    second_egg.alive = false;

    egg.x = gs.player.x + eh::FX_ONE / 2;
    egg.y = gs.player.y;
    egg.ai = eh::AiState::Attack;
    egg.timer = 0;

    // Only the carrot under test may be in reach, so a section cannot be answered by a
    // neighbouring pickup (finding #36).
    eh::Entity &carrot = entity_of_type(gs, eh::EntityType::Carrot);
    for (eh::Entity &entity : gs.entities) {
        if (entity.id != carrot.id &&
            (entity.type == eh::EntityType::Carrot || entity.type == eh::EntityType::Jellybean)) {
            entity.alive = false;
        }
    }
    carrot.x = gs.player.x;
    carrot.y = gs.player.y;

    SECTION("a survivable blow still collects the carrot underfoot") {
        gs.player.health = 40;

        eh::entities_tick(gs);

        // 40 - 12 contact damage, then +25 from the carrot.
        CHECK(gs.player.health == 53);
        CHECK_FALSE(carrot.alive);
        CHECK(event_count(gs, eh::EventType::Pickup) == 1);
        CHECK(event_count(gs, eh::EventType::Lose) == 0);
    }

    SECTION("a lethal blow leaves the carrot on the floor and the player at zero") {
        gs.player.health = 12;

        eh::entities_tick(gs);

        CAPTURE(gs.player.health, gs.events.size());
        CHECK(gs.player.health == 0);
        CHECK(carrot.alive);
        CHECK(event_count(gs, eh::EventType::Pickup) == 0);
        CHECK(event_count(gs, eh::EventType::Lose) == 1);
    }

    SECTION("two contact eggs both strike a living player") {
        // The carrot belongs to the sections above; left underfoot it heals both hits straight
        // back to the cap and hides whether either egg struck at all.
        carrot.alive = false;
        second_egg.alive = true;
        second_egg.x = gs.player.x - eh::FX_ONE / 2;
        second_egg.y = gs.player.y;
        second_egg.ai = eh::AiState::Attack;
        second_egg.timer = 0;
        gs.player.health = 100;

        eh::entities_tick(gs);

        CHECK(gs.player.health == 76);
        CHECK(event_count(gs, eh::EventType::PlayerHurt) == 2);
    }

    SECTION("the second egg does not strike a corpse") {
        carrot.alive = false;
        second_egg.alive = true;
        second_egg.x = gs.player.x - eh::FX_ONE / 2;
        second_egg.y = gs.player.y;
        second_egg.ai = eh::AiState::Attack;
        second_egg.timer = 0;
        gs.player.health = 12;

        eh::entities_tick(gs);

        CAPTURE(gs.player.health, gs.events.size());
        CHECK(gs.player.health == 0);
        CHECK(event_count(gs, eh::EventType::PlayerHurt) == 1);
        CHECK(second_egg.timer == 0);
    }
}

// Entity::timer is a multiplexed field: in AiState::Attack it is the 48-tick contact
// cooldown, and in AiState::Chase it is the lost-sight counter that expires after 180
// ticks. Every handoff between those two meanings must clear it, and none of the three
// clears was covered. Deleted one at a time (entities.cpp):
//
//   231  entering Attack from Chase          129/129 passed
//   226  leaving Attack for Chase            129/129 passed
//   237  re-seeing the player while chasing  caught only by the replay digest, unnamed
//
// The consequences were measured, not argued. A sweep of every quarter-tile pair inside
// EGG_ATTACK_RANGE found 96 positions where an egg touches the player with no line of
// sight -- diagonals across a wall corner -- so an egg really can arrive in contact
// carrying a large lost-sight count. Without the clear at 231 that count is spent as an
// attack cooldown and the egg stands on you doing nothing for up to three seconds.
// Without 226, an egg that has just bitten you carries 48 ticks of cooldown into the
// chase and forgets you 0.8s early -- but only if you leave its range and its sight on
// the same tick, because otherwise the clear at 237 fires immediately after and covers
// for it. Ducking round a corner mid-fight is exactly that case. Without 237, the grace
// period is cumulative across separate sightings rather than restarting, so ducking
// behind two pillars is enough to shake a pursuer that saw you clearly in between.
//
// The fourth clear, at 211 in the Idle arm, is deliberately not tested: a probe ticking
// an egg to expiry showed Idle is only ever entered from 244, which has already zeroed
// the timer, so no reachable state has an idle egg holding a count. Deleting it passes
// 129/129 and always will.
TEST_CASE("entities: the contact cooldown and the lost-sight counter never inherit each other") {
    eh::GameState gs = fresh_game();
    eh::Entity &egg = entity_of_type(gs, eh::EntityType::Egg);
    disable_other_eggs(gs, egg.id);

    // A spot the egg can see from tile (7, 2), and one it cannot.
    const int64_t seen_x = tile_center(5);
    const int64_t hidden_x = tile_center(12);
    const int64_t row_y = tile_center(2);

    SECTION("an egg that closed in unseen bites the moment it touches you") {
        gs.player.x = hidden_x;
        gs.player.y = row_y;
        place(egg, 7, 2);
        egg.ai = eh::AiState::Chase;
        egg.timer = 0;

        for (int tick = 0; tick < 120; ++tick) {
            gs.events.clear();
            eh::entities_tick(gs);
            REQUIRE(egg.ai == eh::AiState::Chase);
        }
        // Fixture guard: the hunt really did bank a large lost-sight count, otherwise
        // the assertion below is satisfied by an egg that never lost sight at all.
        REQUIRE(egg.timer > 100);

        // It rounds the corner and lands on the player. In range, still unseen.
        egg.x = gs.player.x + eh::FX_ONE / 2;
        egg.y = gs.player.y;
        const int32_t health_before = gs.player.health;
        gs.events.clear();
        eh::entities_tick(gs);

        CHECK(egg.ai == eh::AiState::Attack);
        CHECK(gs.player.health < health_before);
        CHECK(event_count(gs, eh::EventType::PlayerHurt) == 1);
    }

    SECTION("an egg that just bit you still hunts for the full grace period") {
        gs.player.x = seen_x;
        gs.player.y = row_y;
        egg.x = gs.player.x + eh::FX_ONE / 2;
        egg.y = gs.player.y;
        egg.ai = eh::AiState::Chase;
        egg.timer = 0;

        gs.events.clear();
        eh::entities_tick(gs);
        REQUIRE(egg.ai == eh::AiState::Attack);
        // Fixture guard: it really bit, so a cooldown really was banked.
        REQUIRE(gs.player.health < 100);

        // The player ducks around a corner: out of contact and out of sight on the same
        // tick, so Attack hands back to Chase with no re-sighting to tidy up after it.
        // Retreating in plain sight would not do -- the clear at 237 fires on the very
        // next line and masks a missing clear at 226 entirely.
        gs.player.x = hidden_x;
        gs.events.clear();
        eh::entities_tick(gs);
        REQUIRE(egg.ai == eh::AiState::Chase);

        // The full grace period starts from the corner, not 48 ticks into it.
        for (int tick = 0; tick < 179; ++tick) {
            gs.events.clear();
            eh::entities_tick(gs);
            CHECK(egg.ai == eh::AiState::Chase);
        }
        gs.events.clear();
        eh::entities_tick(gs);
        CHECK(egg.ai == eh::AiState::Idle);
    }

    SECTION("being seen again restarts the grace period rather than extending it") {
        gs.player.x = hidden_x;
        gs.player.y = row_y;
        place(egg, 7, 2);
        egg.ai = eh::AiState::Chase;
        egg.timer = 0;

        // Two long stretches out of sight, each short of the grace period on its own
        // but well over it combined, separated by one tick of being seen.
        for (int stretch = 0; stretch < 2; ++stretch) {
            gs.player.x = hidden_x;
            for (int tick = 0; tick < 150; ++tick) {
                gs.events.clear();
                eh::entities_tick(gs);
                CHECK(egg.ai == eh::AiState::Chase);
            }
            gs.player.x = seen_x;
            gs.events.clear();
            eh::entities_tick(gs);
            CHECK(egg.ai == eh::AiState::Chase);
        }

        // 300 unseen ticks have now elapsed in total. The egg must still be hunting,
        // and must still take the full grace period from the most recent sighting.
        gs.player.x = hidden_x;
        for (int tick = 0; tick < 180; ++tick) {
            gs.events.clear();
            eh::entities_tick(gs);
            CHECK(egg.ai == eh::AiState::Chase);
        }
        gs.events.clear();
        eh::entities_tick(gs);
        CHECK(egg.ai == eh::AiState::Idle);
    }
}