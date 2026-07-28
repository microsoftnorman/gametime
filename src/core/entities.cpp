#include "core/entities.h"

#include "core/state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace eh {

namespace {

constexpr fx EGG_RADIUS = (FX_ONE * 3 + 5) / 10;
constexpr fx EGG_STEP = (FX_ONE * 18 + 300) / 600;
constexpr fx EGG_SIGHT_RANGE = FX_ONE * 12;
constexpr fx EGG_ATTACK_RANGE = (FX_ONE * 7 + 5) / 10;
constexpr fx EGG_KNOCKBACK = (FX_ONE + 2) / 5;
constexpr fx PICKUP_RANGE = (FX_ONE * 4 + 5) / 10;
constexpr fx BASKET_RANGE = FX_ONE / 2;

constexpr int EGG_DAMAGE = 12;
constexpr int MAX_PLAYER_HEALTH = 100;
constexpr int MAX_PLAYER_AMMO = 60;
constexpr uint16_t ATTACK_COOLDOWN_TICKS = 48;
constexpr uint16_t LOST_SIGHT_TICKS = 180;
constexpr uint16_t HIT_FLASH_TICKS = 9;
constexpr uint16_t HURT_FLASH_TICKS = 9;

int floor_tile(int64_t coordinate) {
    if (coordinate >= 0) {
        return static_cast<int>(coordinate / FX_ONE);
    }
    return -static_cast<int>((-coordinate + FX_ONE - 1) / FX_ONE);
}

uint64_t squared_distance(fx ax, fx ay, fx bx, fx by) {
    const int64_t dx = static_cast<int64_t>(bx) - ax;
    const int64_t dy = static_cast<int64_t>(by) - ay;
    return static_cast<uint64_t>(dx * dx + dy * dy);
}

bool within_range(fx ax, fx ay, fx bx, fx by, fx range) {
    const int64_t radius = range;
    return squared_distance(ax, ay, bx, by) <= static_cast<uint64_t>(radius * radius);
}

bool can_occupy(const Map &map, fx x, fx y, fx radius) {
    const int min_x = floor_tile(static_cast<int64_t>(x) - radius);
    const int max_x = floor_tile(static_cast<int64_t>(x) + radius - 1);
    const int min_y = floor_tile(static_cast<int64_t>(y) - radius);
    const int max_y = floor_tile(static_cast<int64_t>(y) + radius - 1);

    for (int tile_y = min_y; tile_y <= max_y; ++tile_y) {
        for (int tile_x = min_x; tile_x <= max_x; ++tile_x) {
            if (map.is_wall(tile_x, tile_y)) {
                return false;
            }
        }
    }
    return true;
}

bool has_line_of_sight(const Map &map, fx from_x, fx from_y, fx to_x, fx to_y) {
    int cell_x = floor_tile(from_x);
    int cell_y = floor_tile(from_y);
    const int target_x = floor_tile(to_x);
    const int target_y = floor_tile(to_y);
    if (cell_x == target_x && cell_y == target_y) {
        return true;
    }

    const int64_t dx = static_cast<int64_t>(to_x) - from_x;
    const int64_t dy = static_cast<int64_t>(to_y) - from_y;
    const int step_x = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    const int step_y = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    const uint64_t abs_dx = static_cast<uint64_t>(dx < 0 ? -dx : dx);
    const uint64_t abs_dy = static_cast<uint64_t>(dy < 0 ? -dy : dy);

    uint64_t next_x = 0;
    if (step_x > 0) {
        next_x = static_cast<uint64_t>((static_cast<int64_t>(cell_x + 1) * FX_ONE) - from_x);
    } else if (step_x < 0) {
        next_x = static_cast<uint64_t>(from_x - static_cast<int64_t>(cell_x) * FX_ONE);
    }

    uint64_t next_y = 0;
    if (step_y > 0) {
        next_y = static_cast<uint64_t>((static_cast<int64_t>(cell_y + 1) * FX_ONE) - from_y);
    } else if (step_y < 0) {
        next_y = static_cast<uint64_t>(from_y - static_cast<int64_t>(cell_y) * FX_ONE);
    }

    const int max_crossings = map.width + map.height + 2;
    for (int crossing = 0; crossing < max_crossings; ++crossing) {
        if (abs_dx == 0) {
            cell_y += step_y;
            next_y += FX_ONE;
        } else if (abs_dy == 0) {
            cell_x += step_x;
            next_x += FX_ONE;
        } else {
            const uint64_t cross_x = next_x * abs_dy;
            const uint64_t cross_y = next_y * abs_dx;
            if (cross_x < cross_y) {
                cell_x += step_x;
                next_x += FX_ONE;
            } else if (cross_y < cross_x) {
                cell_y += step_y;
                next_y += FX_ONE;
            } else {
                const int adjacent_x = cell_x + step_x;
                const int adjacent_y = cell_y + step_y;
                if (map.is_wall(adjacent_x, cell_y) || map.is_wall(cell_x, adjacent_y)) {
                    return false;
                }
                cell_x = adjacent_x;
                cell_y = adjacent_y;
                next_x += FX_ONE;
                next_y += FX_ONE;
            }
        }

        if (map.is_wall(cell_x, cell_y)) {
            return false;
        }
        if (cell_x == target_x && cell_y == target_y) {
            return true;
        }
    }
    return false;
}

uint64_t integer_sqrt(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = uint64_t{1} << 62;
    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

void move_with_wall_sliding(const Map &map, Entity &entity, fx move_x, fx move_y) {
    const fx next_x = entity.x + move_x;
    if (can_occupy(map, next_x, entity.y, EGG_RADIUS)) {
        entity.x = next_x;
    }

    const fx next_y = entity.y + move_y;
    if (can_occupy(map, entity.x, next_y, EGG_RADIUS)) {
        entity.y = next_y;
    }
}

void move_in_direction(const Map &map, Entity &entity, int64_t dx, int64_t dy, fx distance) {
    const uint64_t length_squared = static_cast<uint64_t>(dx * dx + dy * dy);
    const uint64_t length = integer_sqrt(length_squared);
    if (length == 0) {
        return;
    }

    const int64_t divisor = static_cast<int64_t>(length);
    fx move_x = static_cast<fx>((dx * distance) / divisor);
    fx move_y = static_cast<fx>((dy * distance) / divisor);
    if (move_x == 0 && dx != 0) {
        move_x = dx > 0 ? 1 : -1;
    }
    if (move_y == 0 && dy != 0) {
        move_y = dy > 0 ? 1 : -1;
    }
    move_with_wall_sliding(map, entity, move_x, move_y);
}

bool sees_player(const GameState &gs, const Entity &egg) {
    return within_range(egg.x, egg.y, gs.player.x, gs.player.y, EGG_SIGHT_RANGE) &&
           has_line_of_sight(gs.level.map, egg.x, egg.y, gs.player.x, gs.player.y);
}

void attack_player(GameState &gs, Entity &egg) {
    if (egg.timer > 0) {
        --egg.timer;
    }
    if (egg.timer != 0 || gs.player.health <= 0) {
        return;
    }

    gs.player.health =
        static_cast<int16_t>(std::max(0, static_cast<int>(gs.player.health) - EGG_DAMAGE));
    gs.player.hurt_flash = std::max(gs.player.hurt_flash, HURT_FLASH_TICKS);
    gs.events.push_back({EventType::PlayerHurt, egg.id});
    egg.timer = ATTACK_COOLDOWN_TICKS;
}

void tick_egg(GameState &gs, Entity &egg) {
    if (egg.ai == AiState::Dead) {
        return;
    }

    const bool player_visible = sees_player(gs, egg);
    if (egg.ai == AiState::Idle) {
        egg.timer = 0;
        if (!player_visible) {
            return;
        }
        egg.ai = AiState::Chase;
    }

    const bool in_attack_range =
        within_range(egg.x, egg.y, gs.player.x, gs.player.y, EGG_ATTACK_RANGE);
    if (egg.ai == AiState::Attack) {
        if (in_attack_range) {
            attack_player(gs, egg);
            return;
        }
        egg.ai = AiState::Chase;
        egg.timer = 0;
    }

    if (in_attack_range) {
        egg.ai = AiState::Attack;
        egg.timer = 0;
        attack_player(gs, egg);
        return;
    }

    if (player_visible) {
        egg.timer = 0;
    } else {
        if (egg.timer < std::numeric_limits<uint16_t>::max()) {
            ++egg.timer;
        }
        if (egg.timer > LOST_SIGHT_TICKS) {
            egg.ai = AiState::Idle;
            egg.timer = 0;
            return;
        }
    }

    move_in_direction(gs.level.map, egg, static_cast<int64_t>(gs.player.x) - egg.x,
                      static_cast<int64_t>(gs.player.y) - egg.y, EGG_STEP);
}

bool has_event(const GameState &gs, EventType type) {
    return std::any_of(gs.events.begin(), gs.events.end(),
                       [type](const GameEvent &event) { return event.type == type; });
}

void apply_hit_reactions(GameState &gs, std::size_t event_count) {
    for (std::size_t event_index = 0; event_index < event_count; ++event_index) {
        const GameEvent event = gs.events[event_index];
        if (event.type != EventType::EggHit) {
            continue;
        }

        for (Entity &entity : gs.entities) {
            if (entity.id != event.entity_id || entity.type != EntityType::Egg || !entity.alive ||
                entity.health <= 0) {
                continue;
            }

            entity.hit_flash = HIT_FLASH_TICKS;
            int64_t dx = static_cast<int64_t>(entity.x) - gs.player.x;
            int64_t dy = static_cast<int64_t>(entity.y) - gs.player.y;
            if (dx == 0 && dy == 0) {
                dx = (entity.id & 1u) != 0 ? FX_ONE : -FX_ONE;
            }
            move_in_direction(gs.level.map, entity, dx, dy, EGG_KNOCKBACK);
            break;
        }
    }
}

} // namespace

void entities_tick(GameState &gs) {
    for (Entity &entity : gs.entities) {
        if (entity.hit_flash > 0) {
            --entity.hit_flash;
        }
    }

    const std::size_t existing_event_count = gs.events.size();
    apply_hit_reactions(gs, existing_event_count);

    for (Entity &entity : gs.entities) {
        if (entity.alive && entity.type == EntityType::Egg && entity.health <= 0) {
            entity.alive = false;
            entity.ai = AiState::Dead;
            --gs.eggs_remaining;
            gs.events.push_back({EventType::EggDeath, entity.id});
        }
    }

    for (Entity &entity : gs.entities) {
        if (entity.alive && entity.type == EntityType::Egg) {
            tick_egg(gs, entity);
        }
    }

    if (gs.player.health <= 0) {
        gs.player.health = 0;
        if (!has_event(gs, EventType::Lose)) {
            gs.events.push_back({EventType::Lose, 0});
        }
        return;
    }

    for (Entity &entity : gs.entities) {
        if (!entity.alive ||
            !within_range(entity.x, entity.y, gs.player.x, gs.player.y, PICKUP_RANGE)) {
            continue;
        }

        if (entity.type == EntityType::Jellybean) {
            gs.player.ammo = static_cast<int16_t>(
                std::min(MAX_PLAYER_AMMO, static_cast<int>(gs.player.ammo) + 10));
        } else if (entity.type == EntityType::Carrot) {
            gs.player.health = static_cast<int16_t>(
                std::min(MAX_PLAYER_HEALTH, static_cast<int>(gs.player.health) + 25));
        } else {
            continue;
        }

        entity.alive = false;
        gs.events.push_back({EventType::Pickup, entity.id});
    }

    if (gs.eggs_remaining != 0 || has_event(gs, EventType::Win)) {
        return;
    }
    for (const Entity &entity : gs.entities) {
        if (entity.alive && entity.type == EntityType::Basket &&
            within_range(entity.x, entity.y, gs.player.x, gs.player.y, BASKET_RANGE)) {
            gs.events.push_back({EventType::Win, entity.id});
            return;
        }
    }
}

} // namespace eh
