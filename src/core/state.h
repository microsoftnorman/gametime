#pragma once

#include "core/events.h"
#include "core/input.h"
#include "core/map.h"
#include "core/rng.h"

#include <cstdint>
#include <vector>

namespace eh {

enum class Screen : uint8_t { Title, Playing, Won, Lost };
enum class EntityType : uint8_t { Egg, Jellybean, Carrot, Basket };
enum class AiState : uint8_t { Idle, Chase, Attack, Dead };

struct Entity {
    uint32_t id = 0;
    EntityType type = EntityType::Egg;
    fx x = 0, y = 0;
    int16_t health = 0;
    AiState ai = AiState::Idle;
    uint16_t timer = 0;     // ticks
    uint16_t hit_flash = 0; // ticks remaining
    bool alive = true;
};

struct Player {
    fx x = 0, y = 0;
    angle_t angle = 0;
    int16_t health = 100;
    int16_t ammo = 24;
    uint16_t fire_cooldown = 0; // ticks
    uint16_t hurt_flash = 0;
    fx bob = 0;
};

struct GameState {
    Screen screen = Screen::Title;
    uint32_t tick = 0;
    MapData level;
    Player player;
    std::vector<Entity> entities; // ALWAYS sorted by id, never reordered
    Rng rng{0x1234567u};
    int eggs_remaining = 0;
    uint16_t muzzle_flash = 0;
    uint16_t shake = 0;
    std::vector<GameEvent> events; // cleared at the START of each tick
};

constexpr int TICKS_PER_SECOND = 60;

void reset(GameState &gs, uint32_t seed);
void tick(GameState &gs, const InputFrame &in);

} // namespace eh
