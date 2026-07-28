#include "core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr std::uint16_t REPLAY_FORMAT_VERSION = 1;
constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

constexpr std::size_t FIXED_STATE_BYTES = 69;
constexpr std::size_t ENTITY_BYTES = 21;
constexpr std::size_t EVENT_BYTES = 5;

static_assert(std::is_same_v<eh::fx, std::int32_t>);
static_assert(std::is_same_v<eh::angle_t, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<eh::Screen>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<eh::EntityType>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<eh::AiState>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<eh::Tile>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<eh::EventType>, std::uint8_t>);

struct CanonicalField {
    std::string name;
    std::string value;
};

struct CanonicalState {
    std::vector<std::uint8_t> bytes;
    std::vector<CanonicalField> fields;
};

class CanonicalWriter {
  public:
    void put_u8(std::string name, std::uint8_t value) {
        result_.bytes.push_back(value);
        add_field(std::move(name), std::to_string(static_cast<unsigned int>(value)));
    }

    void put_u16(std::string name, std::uint16_t value) {
        append_u16(value);
        add_field(std::move(name), std::to_string(static_cast<unsigned int>(value)));
    }

    void put_u32(std::string name, std::uint32_t value) {
        append_u32(value);
        add_field(std::move(name), std::to_string(static_cast<unsigned long long>(value)));
    }

    void put_u64(std::string name, std::uint64_t value) {
        append_u64(value);
        add_field(std::move(name), std::to_string(static_cast<unsigned long long>(value)));
    }

    void put_i16(std::string name, std::int16_t value) {
        append_u16(static_cast<std::uint16_t>(value));
        add_field(std::move(name), std::to_string(static_cast<int>(value)));
    }

    void put_i32(std::string name, std::int32_t value) {
        append_u32(static_cast<std::uint32_t>(value));
        add_field(std::move(name), std::to_string(static_cast<long long>(value)));
    }

    CanonicalState finish() && { return std::move(result_); }

  private:
    void append_u16(std::uint16_t value) {
        result_.bytes.push_back(static_cast<std::uint8_t>(value));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void append_u32(std::uint32_t value) {
        result_.bytes.push_back(static_cast<std::uint8_t>(value));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    }

    void append_u64(std::uint64_t value) {
        result_.bytes.push_back(static_cast<std::uint8_t>(value));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 24));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 32));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 40));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 48));
        result_.bytes.push_back(static_cast<std::uint8_t>(value >> 56));
    }

    void add_field(std::string name, std::string value) {
        result_.fields.push_back({std::move(name), std::move(value)});
    }

    CanonicalState result_;
};

std::uint64_t fnv1a(const std::vector<std::uint8_t> &bytes) {
    std::uint64_t digest = FNV_OFFSET_BASIS;
    for (const std::uint8_t byte : bytes) {
        digest ^= byte;
        digest *= FNV_PRIME;
    }
    return digest;
}

std::uint64_t tile_digest(const std::vector<eh::Tile> &tiles) {
    std::uint64_t digest = FNV_OFFSET_BASIS;
    for (const eh::Tile tile : tiles) {
        digest ^= static_cast<std::uint8_t>(tile);
        digest *= FNV_PRIME;
    }
    return digest;
}

std::uint32_t checked_count(std::size_t value, std::string_view field) {
    if (value > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::overflow_error(std::string(field) + " exceeds the replay format");
    }
    return static_cast<std::uint32_t>(value);
}

std::int32_t checked_i32(int value, std::string_view field) {
    const auto wide = static_cast<std::int64_t>(value);
    if (wide < std::numeric_limits<std::int32_t>::min() ||
        wide > std::numeric_limits<std::int32_t>::max()) {
        throw std::overflow_error(std::string(field) + " exceeds the replay format");
    }
    return static_cast<std::int32_t>(value);
}

CanonicalState serialize_state(const eh::GameState &state) {
    CanonicalWriter writer;

    writer.put_u16("version", REPLAY_FORMAT_VERSION);
    writer.put_u32("tick", state.tick);

    writer.put_i32("map.width", checked_i32(state.level.map.width, "map.width"));
    writer.put_i32("map.height", checked_i32(state.level.map.height, "map.height"));
    writer.put_u32("map.tile_count", checked_count(state.level.map.tiles.size(), "map.tile_count"));
    writer.put_u64("map.tiles_digest", tile_digest(state.level.map.tiles));

    writer.put_i32("player.x", state.player.x);
    writer.put_i32("player.y", state.player.y);
    writer.put_u16("player.angle", state.player.angle);
    writer.put_i16("player.health", state.player.health);
    writer.put_i16("player.ammo", state.player.ammo);
    writer.put_u16("player.fire_cooldown", state.player.fire_cooldown);
    writer.put_u16("player.hurt_flash", state.player.hurt_flash);
    writer.put_i32("player.bob", state.player.bob);

    writer.put_i32("eggs_remaining", checked_i32(state.eggs_remaining, "eggs_remaining"));
    writer.put_u8("screen", static_cast<std::uint8_t>(state.screen));
    writer.put_u32("rng.state", state.rng.state);
    writer.put_u16("muzzle_flash", state.muzzle_flash);
    writer.put_u16("shake", state.shake);

    writer.put_u32("entity_count", checked_count(state.entities.size(), "entity_count"));
    for (const eh::Entity &entity : state.entities) {
        const std::string prefix = "entity[" + std::to_string(entity.id) + "]";
        writer.put_u32(prefix + ".id", entity.id);
        writer.put_u8(prefix + ".type", static_cast<std::uint8_t>(entity.type));
        writer.put_i32(prefix + ".x", entity.x);
        writer.put_i32(prefix + ".y", entity.y);
        writer.put_i16(prefix + ".health", entity.health);
        writer.put_u8(prefix + ".ai", static_cast<std::uint8_t>(entity.ai));
        writer.put_u16(prefix + ".timer", entity.timer);
        writer.put_u16(prefix + ".hit_flash", entity.hit_flash);
        writer.put_u8(prefix + ".alive", entity.alive ? 1u : 0u);
    }

    writer.put_u32("event_count", checked_count(state.events.size(), "event_count"));
    for (std::size_t index = 0; index < state.events.size(); ++index) {
        const eh::GameEvent &event = state.events[index];
        const std::string prefix = "event[" + std::to_string(index) + "]";
        writer.put_u8(prefix + ".type", static_cast<std::uint8_t>(event.type));
        writer.put_u32(prefix + ".entity_id", event.entity_id);
    }

    return std::move(writer).finish();
}

std::uint64_t state_digest(const eh::GameState &state) {
    return fnv1a(serialize_state(state).bytes);
}

std::string diff_states(const CanonicalState &expected, const CanonicalState &actual) {
    std::ostringstream report;
    bool found_difference = false;

    const auto emit = [&](const std::string &line) {
        if (found_difference) {
            report << '\n';
        }
        report << line;
        found_difference = true;
    };

    const std::size_t shared = std::min(expected.fields.size(), actual.fields.size());
    for (std::size_t index = 0; index < shared; ++index) {
        const CanonicalField &lhs = expected.fields[index];
        const CanonicalField &rhs = actual.fields[index];
        if (lhs.name == rhs.name) {
            if (lhs.value != rhs.value) {
                emit(lhs.name + ": " + lhs.value + " != " + rhs.value);
            }
            continue;
        }

        emit("field[" + std::to_string(index) + "]: " + lhs.name + "=" + lhs.value +
             " != " + rhs.name + "=" + rhs.value);
    }

    for (std::size_t index = shared; index < expected.fields.size(); ++index) {
        const CanonicalField &field = expected.fields[index];
        emit(field.name + ": " + field.value + " != <missing>");
    }
    for (std::size_t index = shared; index < actual.fields.size(); ++index) {
        const CanonicalField &field = actual.fields[index];
        emit(field.name + ": <missing> != " + field.value);
    }

    if (!found_difference && expected.bytes != actual.bytes) {
        const std::size_t common_bytes = std::min(expected.bytes.size(), actual.bytes.size());
        std::size_t index = 0;
        while (index < common_bytes && expected.bytes[index] == actual.bytes[index]) {
            ++index;
        }
        emit("canonical byte[" + std::to_string(index) +
             "] differs without a labelled field difference");
    }

    return found_difference ? report.str() : "no differences";
}

void require_same_state(const CanonicalState &expected, const CanonicalState &actual) {
    const std::uint64_t expected_digest = fnv1a(expected.bytes);
    const std::uint64_t actual_digest = fnv1a(actual.bytes);
    INFO("Expected digest: " << expected_digest);
    INFO("Actual digest: " << actual_digest);
    INFO("Canonical state diff:\n" << diff_states(expected, actual));
    REQUIRE(expected_digest == actual_digest);
    REQUIRE(expected.bytes == actual.bytes);
}

std::size_t expected_size(const eh::GameState &state) {
    return FIXED_STATE_BYTES + state.entities.size() * ENTITY_BYTES +
           state.events.size() * EVENT_BYTES;
}

bool same_input(const eh::InputFrame &lhs, const eh::InputFrame &rhs) {
    return lhs.move_x == rhs.move_x && lhs.move_y == rhs.move_y && lhs.turn == rhs.turn &&
           lhs.mouse_dx == rhs.mouse_dx && lhs.buttons == rhs.buttons;
}

struct InputRun {
    std::uint32_t repeat;
    eh::InputFrame input;
};

class InputScript {
  public:
    InputScript &hold(std::uint32_t ticks, const eh::InputFrame &input = {}) {
        if (ticks == 0) {
            throw std::invalid_argument("an input run must contain at least one tick");
        }

        if (!runs_.empty() && same_input(runs_.back().input, input) &&
            ticks <= std::numeric_limits<std::uint32_t>::max() - runs_.back().repeat) {
            runs_.back().repeat += ticks;
        } else {
            runs_.push_back({ticks, input});
        }
        tick_count_ += ticks;
        return *this;
    }

    InputScript &pulse(eh::InputFrame::Button button, eh::InputFrame input = {}) {
        input.buttons =
            static_cast<std::uint8_t>(input.buttons | static_cast<std::uint8_t>(button));
        return hold(1, input);
    }

    template <typename Visitor> void for_each_tick(Visitor &&visitor) const {
        for (const InputRun &run : runs_) {
            for (std::uint32_t tick = 0; tick < run.repeat; ++tick) {
                visitor(run.input);
            }
        }
    }

    std::uint64_t tick_count() const { return tick_count_; }

    std::uint64_t button_ticks(eh::InputFrame::Button button) const {
        std::uint64_t count = 0;
        for (const InputRun &run : runs_) {
            if (run.input.held(button)) {
                count += run.repeat;
            }
        }
        return count;
    }

  private:
    std::vector<InputRun> runs_;
    std::uint64_t tick_count_ = 0;
};

eh::InputFrame input(std::int8_t move_x = 0, std::int8_t move_y = 0, std::int8_t turn = 0,
                     std::int16_t mouse_dx = 0, std::uint8_t buttons = 0) {
    eh::InputFrame frame;
    frame.move_x = move_x;
    frame.move_y = move_y;
    frame.turn = turn;
    frame.mouse_dx = mouse_dx;
    frame.buttons = buttons;
    return frame;
}

// The rotation segments below are deliberately asymmetric, and movement follows
// them, so that the final digest depends on orientation. An earlier version
// turned equally in both directions and then stood still, which left the whole
// 600-tick digest blind to turning: halving the keyboard turn rate passed every
// test in the suite. Keep the net rotation non-zero and keep movement after it.
InputScript ten_second_script() {
    InputScript script;
    script.hold(30, input(0, 1, 0, 0, eh::InputFrame::Sprint))
        .hold(30, input(0, -1))
        .hold(30, input(-1, 0))
        .hold(30, input(1, 0))
        .hold(120, input(0, 0, 1))
        .hold(60, input(0, 0, -1))
        .hold(60, input(0, 0, 0, 3))
        .hold(30, input(0, 0, 0, -3))
        .pulse(eh::InputFrame::Fire)
        .hold(60, input(0, 1))
        .hold(149);
    return script;
}

struct ReplayResult {
    eh::GameState state;
    CanonicalState canonical;
    std::uint64_t digest;
    std::uint64_t trajectory;
};

ReplayResult run_replay(std::uint32_t seed, const InputScript &script) {
    eh::GameState state;
    eh::reset(state, seed);

    // Fold every intermediate tick into a rolling hash, not just the last one.
    // A final-state digest is blind to anything transient: hit flashes, hurt
    // flashes, knockback, muzzle flash, screen shake and every event that has
    // already decayed or been drained by tick 600. Changing HURT_FLASH_TICKS
    // from 9 to 12 was verified to leave the final-state digest untouched.
    std::uint64_t trajectory = FNV_OFFSET_BASIS;
    script.for_each_tick([&](const eh::InputFrame &frame) {
        eh::tick(state, frame);
        const std::uint64_t step = fnv1a(serialize_state(state).bytes);
        for (int shift = 0; shift < 64; shift += 8) {
            trajectory ^= static_cast<std::uint8_t>(step >> shift);
            trajectory *= FNV_PRIME;
        }
    });

    CanonicalState canonical = serialize_state(state);
    const std::uint64_t digest = fnv1a(canonical.bytes);
    return {std::move(state), std::move(canonical), digest, trajectory};
}

} // namespace

TEST_CASE("replay: a 600 tick input script is deterministic") {
    const InputScript script = ten_second_script();
    REQUIRE(script.tick_count() == 10u * eh::TICKS_PER_SECOND);
    REQUIRE(script.button_ticks(eh::InputFrame::Fire) == 1);

    constexpr std::uint32_t seed = 0x5eed1234u;
    const ReplayResult first = run_replay(seed, script);
    const ReplayResult second = run_replay(seed, script);

    REQUIRE(first.state.tick == script.tick_count());
    REQUIRE(second.state.tick == script.tick_count());
    REQUIRE(std::is_sorted(
        first.state.entities.begin(), first.state.entities.end(),
        [](const eh::Entity &lhs, const eh::Entity &rhs) { return lhs.id < rhs.id; }));
    REQUIRE(first.digest == fnv1a(first.canonical.bytes));
    REQUIRE(second.digest == fnv1a(second.canonical.bytes));
    require_same_state(first.canonical, second.canonical);

    // Non-vacuity guard. Determinism is trivially satisfied by a simulation
    // that does nothing, so assert the run actually moved, turned and stayed
    // in play. This harness was authored while player_tick and entities_tick
    // were stubs; without these checks it would still pass green if gameplay
    // regressed back to doing nothing.
    eh::GameState untouched;
    eh::reset(untouched, seed);
    REQUIRE(fnv1a(serialize_state(untouched).bytes) != first.digest);
    REQUIRE(first.state.screen == eh::Screen::Playing);
    REQUIRE(first.state.player.angle != untouched.player.angle);
    REQUIRE(
        (first.state.player.x != untouched.player.x || first.state.player.y != untouched.player.y));

    // Golden digests.
    //
    // Everything above only proves the simulation is self-consistent: it runs
    // the same script twice through the same binary, so a changed tuning
    // constant changes both runs identically and the comparison still passes.
    // Halving KEYBOARD_TURN_UNITS was verified to slip through it untouched.
    //
    // Pinning the digests converts this from a determinism check into a
    // whole-simulation regression detector: any change to movement, turning,
    // collision, firing, AI, damage, RNG advancement, visual timers or event
    // emission moves one of these numbers. And because gameplay state is
    // fixed-point integer maths with explicitly-sized little-endian
    // serialization, the same values must appear under GCC on Linux -- so CI
    // turns the cross-platform determinism claim in mvp.md into an assertion
    // rather than an aspiration.
    //
    // A failure here is expected and correct after a deliberate tuning change.
    // require_same_state names the field that moved; re-run and update.
    constexpr std::uint64_t golden_digest = 0x6069fa714730d78eull;
    constexpr std::uint64_t golden_trajectory = 0xf587096a9c0bceeaull;
    if (first.digest != golden_digest || first.trajectory != golden_trajectory) {
        WARN("600 tick replay digests: final=0x" << std::hex << first.digest << " trajectory=0x"
                                                 << first.trajectory << std::dec);
    }
    REQUIRE(first.digest == golden_digest);
    REQUIRE(first.trajectory == golden_trajectory);
    REQUIRE(second.trajectory == first.trajectory);
}

TEST_CASE("replay: reset is reproducible and seed-sensitive") {
    eh::GameState first;
    eh::GameState second;
    eh::GameState different_seed;
    eh::reset(first, 0x13579bdfu);
    eh::reset(second, 0x13579bdfu);
    eh::reset(different_seed, 0x2468ace0u);

    const CanonicalState first_canonical = serialize_state(first);
    const CanonicalState second_canonical = serialize_state(second);
    const CanonicalState different_canonical = serialize_state(different_seed);

    require_same_state(first_canonical, second_canonical);
    REQUIRE(fnv1a(first_canonical.bytes) != fnv1a(different_canonical.bytes));
    REQUIRE(diff_states(first_canonical, different_canonical).find("rng.state") !=
            std::string::npos);
}

// The seed is currently inert, and this test exists to say so out loud.
//
// The replay session flagged that its seed-sensitivity check only proved a
// different seed changes the serialized rng.state, not that gameplay diverges,
// because nothing consumed the RNG while the tick paths were stubs. That
// caveat survived integration: no simulation code calls next() on gs.rng at
// all. The only RNG consumers are audio.cpp's waveform generators, which use
// their own fixed-seed local Rng objects and never touch game state.
//
// mvp.md asks for "one explicit seeded RNG, its full state part of the game
// state" -- an architectural provision, not a promise of varied gameplay. The
// MVP's level layout and AI are deliberately deterministic, so an unused RNG
// is not a missing feature. But reset(gs, seed) reads as though runs vary, and
// a test named "seed-sensitive" reads as though that is verified. Neither is
// true, so this pins the actual behaviour instead of implying a better one.
//
// If a gameplay path ever consumes gs.rng, this test fails and points at the
// two things that must then be revisited: the pinned replay digests above, and
// whether seeded variation is genuinely wanted.
TEST_CASE("replay: no gameplay path consumes the seeded RNG") {
    constexpr std::uint32_t seed_a = 0x5eed1234u;
    constexpr std::uint32_t seed_b = 0x0badc0deu;
    const InputScript script = ten_second_script();
    const ReplayResult a = run_replay(seed_a, script);
    const ReplayResult b = run_replay(seed_b, script);

    // 600 ticks of real movement, firing, AI and damage leave the generator
    // exactly where reset() put it. One assertion, and it proves the claim.
    CHECK(a.state.rng.state == seed_a);
    CHECK(b.state.rng.state == seed_b);

    // Everything except the stored seed is byte-identical across seeds. This
    // compares whole canonical states rather than a field list, so it stays
    // honest if new gameplay fields are added later.
    eh::GameState a_normalized = a.state;
    eh::GameState b_normalized = b.state;
    a_normalized.rng.state = 0u;
    b_normalized.rng.state = 0u;
    require_same_state(serialize_state(a_normalized), serialize_state(b_normalized));
}

TEST_CASE("replay: serialization is fixed-width and field-sensitive") {
    eh::GameState original;
    eh::reset(original, 0xabcdef01u);

    const CanonicalState first = serialize_state(original);
    const CanonicalState unchanged = serialize_state(original);
    REQUIRE(first.bytes.size() == expected_size(original));
    REQUIRE(unchanged.bytes.size() == expected_size(original));
    REQUIRE(fnv1a(first.bytes) == fnv1a(unchanged.bytes));
    REQUIRE(first.bytes == unchanged.bytes);

    eh::GameState changed = original;
    --changed.player.health;
    const CanonicalState changed_canonical = serialize_state(changed);

    REQUIRE(changed_canonical.bytes.size() == first.bytes.size());
    REQUIRE(fnv1a(changed_canonical.bytes) != fnv1a(first.bytes));
    REQUIRE(diff_states(first, changed_canonical) == "player.health: 100 != 99");
}

TEST_CASE("replay: version and tick lead the little-endian byte stream") {
    eh::GameState state;
    eh::reset(state, 1);
    state.tick = 0x78563412u;

    const CanonicalState canonical = serialize_state(state);
    REQUIRE(canonical.bytes.size() >= 6);
    REQUIRE(canonical.bytes[0] == 0x01);
    REQUIRE(canonical.bytes[1] == 0x00);
    REQUIRE(canonical.bytes[2] == 0x12);
    REQUIRE(canonical.bytes[3] == 0x34);
    REQUIRE(canonical.bytes[4] == 0x56);
    REQUIRE(canonical.bytes[5] == 0x78);
}

TEST_CASE("replay: canonical fixture has a cross-platform golden digest") {
    eh::GameState fixture;
    fixture.tick = 0x01020304u;
    fixture.level.map.width = 2;
    fixture.level.map.height = 1;
    fixture.level.map.tiles = {eh::Tile::Floor, eh::Tile::WallBasket};
    fixture.player.x = -4096;
    fixture.player.y = 8192;
    fixture.player.angle = 0xbeefu;
    fixture.player.health = -7;
    fixture.player.ammo = 33;
    fixture.player.fire_cooldown = 4;
    fixture.player.hurt_flash = 5;
    fixture.player.bob = -6;
    fixture.eggs_remaining = 1;
    fixture.screen = eh::Screen::Won;
    fixture.rng.state = 0x89abcdefu;
    fixture.muzzle_flash = 8;
    fixture.shake = 9;

    eh::Entity entity;
    entity.id = 42;
    entity.type = eh::EntityType::Carrot;
    entity.x = -1;
    entity.y = 2;
    entity.health = -3;
    entity.ai = eh::AiState::Dead;
    entity.timer = 10;
    entity.hit_flash = 11;
    entity.alive = false;
    fixture.entities.push_back(entity);
    fixture.events.push_back({eh::EventType::PlayerHurt, 42});

    const CanonicalState canonical = serialize_state(fixture);
    REQUIRE(canonical.bytes.size() == 95);
    REQUIRE(fnv1a(canonical.bytes) == 0x96b899b371551a77ull);
}

TEST_CASE("replay: field diff names a changed entity field and both values") {
    eh::GameState expected;
    eh::reset(expected, 7);
    REQUIRE(expected.entities.size() >= 3);
    REQUIRE(expected.entities[2].id == 3);
    REQUIRE(expected.entities[2].health == 60);

    eh::GameState actual = expected;
    actual.entities[2].health = 26;

    const std::string report = diff_states(serialize_state(expected), serialize_state(actual));
    INFO(report);
    REQUIRE(report == "entity[3].health: 60 != 26");
}

TEST_CASE("replay: renderer-side floats are outside the digest boundary") {
    struct RuntimeSnapshot {
        eh::GameState simulation;
        float renderer_wall_depth = 0.0f;
    };

    RuntimeSnapshot near_view;
    eh::reset(near_view.simulation, 11);
    near_view.renderer_wall_depth = 1.25f;

    RuntimeSnapshot far_view = near_view;
    far_view.renderer_wall_depth = 42.5f;

    REQUIRE(near_view.renderer_wall_depth != far_view.renderer_wall_depth);
    REQUIRE(state_digest(near_view.simulation) == state_digest(far_view.simulation));
    require_same_state(serialize_state(near_view.simulation), serialize_state(far_view.simulation));
}

TEST_CASE("replay: entity vector order participates in the digest") {
    eh::GameState ordered;
    eh::reset(ordered, 13);
    REQUIRE(ordered.entities.size() >= 3);
    REQUIRE(std::is_sorted(
        ordered.entities.begin(), ordered.entities.end(),
        [](const eh::Entity &lhs, const eh::Entity &rhs) { return lhs.id < rhs.id; }));

    eh::GameState permuted = ordered;
    std::swap(permuted.entities[1], permuted.entities[2]);

    const CanonicalState ordered_canonical = serialize_state(ordered);
    const CanonicalState permuted_canonical = serialize_state(permuted);
    REQUIRE(ordered_canonical.bytes.size() == permuted_canonical.bytes.size());
    REQUIRE(fnv1a(ordered_canonical.bytes) != fnv1a(permuted_canonical.bytes));
    REQUIRE(diff_states(ordered_canonical, permuted_canonical).find("entity[2]") !=
            std::string::npos);
}

TEST_CASE("replay: map tiles and pending events participate in the digest") {
    eh::GameState baseline;
    eh::reset(baseline, 17);

    SECTION("map tile identity") {
        eh::GameState changed = baseline;
        REQUIRE_FALSE(changed.level.map.tiles.empty());
        changed.level.map.tiles[0] = eh::Tile::WallBasket;

        const CanonicalState expected = serialize_state(baseline);
        const CanonicalState actual = serialize_state(changed);
        REQUIRE(fnv1a(expected.bytes) != fnv1a(actual.bytes));
        REQUIRE(diff_states(expected, actual).find("map.tiles_digest") != std::string::npos);
    }

    SECTION("pending semantic event") {
        baseline.events.push_back({eh::EventType::EggHit, 3});
        eh::GameState changed = baseline;
        changed.events[0].entity_id = 4;

        const CanonicalState expected = serialize_state(baseline);
        const CanonicalState actual = serialize_state(changed);
        REQUIRE(expected.bytes.size() == expected_size(baseline));
        REQUIRE(actual.bytes.size() == expected_size(changed));
        REQUIRE(fnv1a(expected.bytes) != fnv1a(actual.bytes));
        REQUIRE(diff_states(expected, actual) == "event[0].entity_id: 3 != 4");
    }
}
