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

// The test above fires straight down a row, so it only ever proves orthogonal occlusion.
// `distance_to_wall` also has a branch for the exact-diagonal case, where the ray crosses an x
// and a y grid line at the same distance -- a wall corner. It stops the shot when either
// orthogonal neighbour of that corner is solid, so a bullet cannot squeeze between two tiles
// meeting at a point. Disabling that branch left the suite at 110/110.
//
// The branch is not dead code. A sweep of 63,488 shots (every open tile, four sub-tile offsets,
// 64 headings) found exactly one outcome that changes, and it is a real exploit: from
// (18.5, 7.5) facing 315 degrees, an egg 2.83 tiles away loses 34 health through the corner of a
// solid wall. One case in 63,488 is why no hand-written fixture stumbled into it -- and the
// corridor it happens in is one of only two links between the map's halves, so a player walking
// the level passes through it.
TEST_CASE("player: a shot cannot squeeze through the corner between two walls") {
    eh::GameState game;
    eh::reset(game, 11);
    eh::Entity &egg = game.entities.front();
    for (std::size_t i = 1; i < game.entities.size(); ++i) {
        game.entities[i].alive = false;
    }
    egg.x = eh::fx_from_int(20) + eh::FX_ONE / 2;
    egg.y = eh::fx_from_int(5) + eh::FX_ONE / 2;
    game.player.angle = eh::angle_from_deg(315.0);

    // Read out of the map rather than assumed, so a level edit fails here instead of quietly
    // turning this into a second copy of the unobstructed-shot test. The diagonal leaves tile
    // (18,7) for (19,6); (19,7) is solid and both (18,6) and the destination are open.
    REQUIRE(game.level.map.is_wall(19, 7));
    REQUIRE_FALSE(game.level.map.is_wall(18, 6));
    REQUIRE_FALSE(game.level.map.is_wall(19, 6));

    // The two sections differ only in which side of that corner the shooter stands on. Same
    // heading, same target, same weapon -- so a failure cannot be blamed on range or aim.
    SECTION("the wall corner stops it") {
        game.player.x = eh::fx_from_int(18) + eh::FX_ONE / 2;
        game.player.y = eh::fx_from_int(7) + eh::FX_ONE / 2;

        eh::fire(game);

        CHECK(egg.health == 60);
        CHECK(egg.hit_flash == 0);
        CHECK(event_count(game, eh::EventType::EggHit) == 0);
    }

    SECTION("past the corner the identical shot connects") {
        game.player.x = eh::fx_from_int(19) + eh::FX_ONE / 2;
        game.player.y = eh::fx_from_int(6) + eh::FX_ONE / 2;

        eh::fire(game);

        CHECK(egg.health == 26);
        CHECK(event_count(game, eh::EventType::EggHit) == 1);
    }
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

// `move_axis` does two separable jobs.
//
//  1. It chunks a displacement into pieces no larger than COLLISION_STEP, so a fast move cannot
//     skip over a wall. That is covered: disabling the clamp fails the tunnelling test above.
//  2. It then binary-searches the first blocked piece for the largest safe sub-step, so the
//     player ends flush against the wall face instead of up to one movement step short.
//
// Job 2 had no coverage on either axis. Deleting the entire search left the suite at 109/109.
//
// The tunnelling test looks like it pins the stop position, because it asserts an exact value.
// It does not, and the reason is its fixture: it starts at x = 6.5 while COLLISION_STEP is
// exactly PLAYER_RADIUS = 0.25 tiles, so 6.5 + n * 0.25 reaches the flush 7.75 on its own and
// the sub-step search never has to contribute anything. An exact-value assertion can still be
// satisfied without the code it appears to pin, when the fixture's coordinates happen to be
// commensurate with the step size.
//
// Measured with the search deleted, walking -- the only forward magnitude the app produces:
//     east   7.72412 instead of 7.75000
//     south  6.71289 instead of 6.75000
//     north  1.28418 instead of 1.25000
// and one large displacement from a start that is not a multiple of COLLISION_STEP stops
// 0.20020 tiles short. So every approach below starts on a deliberately fractional coordinate,
// and all four faces are asserted because the two axes and two signs are independent paths.
TEST_CASE("player: walking into a wall settles flush against every face") {
    constexpr eh::fx OFF_GRID = eh::FX_ONE * 37 / 100;
    constexpr eh::fx MID_TILE = eh::FX_ONE / 2;

    struct Approach {
        const char *face;
        eh::fx start_x;
        eh::fx start_y;
        double heading_deg;
        bool along_x;
        eh::fx flush;
    };

    const Approach approaches[] = {
        {"east", eh::fx_from_int(4) + OFF_GRID, eh::fx_from_int(4) + MID_TILE, 0.0, true,
         eh::fx_from_int(8) - PLAYER_RADIUS},
        {"west", eh::fx_from_int(4) + OFF_GRID, eh::fx_from_int(4) + MID_TILE, 180.0, true,
         eh::fx_from_int(1) + PLAYER_RADIUS},
        {"south", eh::fx_from_int(6) + MID_TILE, eh::fx_from_int(4) + OFF_GRID, 90.0, false,
         eh::fx_from_int(7) - PLAYER_RADIUS},
        {"north", eh::fx_from_int(6) + MID_TILE, eh::fx_from_int(4) + OFF_GRID, 270.0, false,
         eh::fx_from_int(1) + PLAYER_RADIUS},
    };

    for (const Approach &approach : approaches) {
        INFO("approach: " << approach.face);

        eh::GameState game;
        eh::reset(game, 71);
        game.player.x = approach.start_x;
        game.player.y = approach.start_y;
        game.player.angle = eh::angle_from_deg(approach.heading_deg);

        const eh::fx start = approach.along_x ? game.player.x : game.player.y;
        REQUIRE(start != approach.flush);

        eh::InputFrame input;
        input.move_y = 1;
        for (int tick = 0; tick < 200; ++tick) {
            eh::player_tick(game, input);
        }

        const eh::fx settled = approach.along_x ? game.player.x : game.player.y;
        const eh::fx travelled = settled > start ? settled - start : start - settled;

        // Non-vacuity: the walk must cover real ground, so a player frozen at a coincidentally
        // flush spawn cannot satisfy the assertion below.
        REQUIRE(travelled > eh::fx_from_int(2));
        CHECK(settled == approach.flush);
        CHECK_FALSE(overlaps_wall(game));
    }
}

// The weapon's footfall cadence is driven by `bob`, which advances a fixed phase step on any tick
// where the player's position changed *at all* -- the test is `!=`, a zero threshold. The cadence
// is therefore completely independent of how fast the player is actually moving.
//
// Measured over three seconds of rendered frames: a free walk covers 3.19 tiles/sec, a sprint
// 4.79, and a player grinding 0.2 degrees off a wall 0.029 -- a 163x spread in ground speed --
// while the weapon dips 19.67 times per second in every single one. Sprinting lengthens the
// stride rather than quickening the step, and a player creeping along a wall still hears a full
// run. `sprites: the weapon dips four times per left-right sweep` pins the other half of this
// seam, phase to pixels; this pins the producer.
//
// Making the advance distance-proportional -- the obvious improvement -- leaves 111 of 112 tests
// green. The only failure is the replay digest, which reports that something changed, not what,
// and would most likely just be re-blessed. So this test states the behaviour as it currently
// stands, to force that change to be deliberate. Whether ~20 footfalls per second is the right
// *feel*, and whether sprint should quicken the cadence, are playtest questions it cannot answer.
TEST_CASE("player: footfall cadence is currently independent of ground speed") {
    constexpr int TICKS = 120;

    struct Result {
        eh::fx bob;
        double tiles;
    };

    auto run = [](double degrees, bool sprint, bool flush_to_wall) {
        eh::GameState game;
        eh::reset(game, 0x5eed1234u);
        game.player.angle = eh::angle_from_deg(degrees);
        // Row 3 is a fully open corridor. `flush_to_wall` instead parks the player already hard
        // against the north wall, so the entire window is a grind with no free approach in it.
        game.player.x = eh::fx_from_int(2) + eh::FX_ONE / 2;
        game.player.y = flush_to_wall ? eh::fx_from_int(1) + eh::FX_ONE / 4
                                      : eh::fx_from_int(3) + eh::FX_ONE / 2;

        eh::InputFrame in;
        in.move_y = 1;
        if (sprint) {
            in.buttons |= eh::InputFrame::Sprint;
        }

        const eh::fx start_x = game.player.x;
        const eh::fx start_y = game.player.y;
        for (int t = 0; t < TICKS; ++t) {
            eh::player_tick(game, in);
        }

        const double dx = static_cast<double>(game.player.x - start_x) / eh::FX_ONE;
        const double dy = static_cast<double>(game.player.y - start_y) / eh::FX_ONE;
        return Result{game.player.bob, std::sqrt(dx * dx + dy * dy)};
    };

    const Result walk = run(0.0, false, false);
    const Result sprint = run(0.0, true, false);
    const Result grind = run(269.8, false, true);

    // Non-vacuity: the three runs must really have moved at very different speeds, or asserting
    // that cadence ignores speed would be asserting nothing.
    INFO("walk " << walk.tiles << " sprint " << sprint.tiles << " grind " << grind.tiles);
    REQUIRE(walk.tiles > 5.0);
    REQUIRE(sprint.tiles > walk.tiles * 1.4);
    REQUIRE(grind.tiles < walk.tiles / 50.0);
    REQUIRE(grind.tiles > 0.0);

    // The phase reached is nonetheless bit-identical across all three.
    CHECK(sprint.bob == walk.bob);
    CHECK(grind.bob == walk.bob);

    // And the phase genuinely advanced, so equality is not three copies of a resting zero.
    REQUIRE(walk.bob != 0);
}
