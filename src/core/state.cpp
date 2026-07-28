#include "core/state.h"

#include "core/entities.h"
#include "core/player.h"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace eh {

namespace {

constexpr uint32_t DEFAULT_SEED = 0x1234567u;

void add_entity(GameState &gs, uint32_t id, EntityType type, const SpawnPoint &spawn,
                int16_t health) {
    Entity entity;
    entity.id = id;
    entity.type = type;
    entity.x = spawn.x;
    entity.y = spawn.y;
    entity.health = health;
    gs.entities.push_back(entity);
}

} // namespace

void reset(GameState &gs, uint32_t seed) {
    MapParseResult parsed = parse_map(BURROW_01);
    if (!parsed.ok) {
        throw std::runtime_error("BURROW_01 validation failed: " + parsed.error);
    }

    GameState fresh;
    fresh.screen = Screen::Playing;
    fresh.level = std::move(parsed.data);
    fresh.player.x = fresh.level.player.x;
    fresh.player.y = fresh.level.player.y;
    fresh.player.angle = fresh.level.player_angle;
    fresh.rng.state = seed;
    fresh.eggs_remaining = static_cast<int>(fresh.level.eggs.size());

    const std::size_t entity_count =
        fresh.level.eggs.size() + fresh.level.jellybeans.size() + fresh.level.carrots.size() + 1;
    fresh.entities.reserve(entity_count);

    uint32_t id = 1;
    for (const SpawnPoint &spawn : fresh.level.eggs) {
        add_entity(fresh, id++, EntityType::Egg, spawn, 60);
    }
    for (const SpawnPoint &spawn : fresh.level.jellybeans) {
        add_entity(fresh, id++, EntityType::Jellybean, spawn, 0);
    }
    for (const SpawnPoint &spawn : fresh.level.carrots) {
        add_entity(fresh, id++, EntityType::Carrot, spawn, 0);
    }
    add_entity(fresh, id, EntityType::Basket, fresh.level.basket, 0);

    gs = std::move(fresh);
}

void tick(GameState &gs, const InputFrame &in) {
    gs.events.clear();

    switch (gs.screen) {
    case Screen::Title:
        if (in.held(InputFrame::Start)) {
            reset(gs, DEFAULT_SEED);
        }
        break;

    case Screen::Playing: {
        ++gs.tick;
        player_tick(gs, in);
        entities_tick(gs);

        if (gs.player.health <= 0) {
            gs.screen = Screen::Lost;
            break;
        }
        if (gs.screen != Screen::Playing) {
            break;
        }

        bool won = false;
        for (const GameEvent &event : gs.events) {
            if (event.type == EventType::Lose) {
                gs.screen = Screen::Lost;
                return;
            }
            won = won || event.type == EventType::Win;
        }
        if (won) {
            gs.screen = Screen::Won;
        }
        break;
    }

    case Screen::Won:
    case Screen::Lost:
        if (in.held(InputFrame::Restart)) {
            reset(gs, DEFAULT_SEED);
        }
        break;
    }
}

} // namespace eh
