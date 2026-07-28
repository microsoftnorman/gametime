#pragma once

#include <cstdint>

namespace eh {

enum class EventType : uint8_t { Shot, EggHit, EggDeath, Pickup, PlayerHurt, Win, Lose };

struct GameEvent {
    EventType type;
    uint32_t entity_id;
};

} // namespace eh
