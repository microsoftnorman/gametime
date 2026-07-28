#include "core/player.h"

#include "core/input.h"
#include "core/map.h"
#include "core/state.h"

#include <cstdint>
#include <limits>

namespace eh {

namespace {

constexpr fx PLAYER_RADIUS = FX_ONE / 4;
constexpr fx COLLISION_STEP = PLAYER_RADIUS;

constexpr fx FORWARD_STEP = FX_ONE * 32 / (10 * TICKS_PER_SECOND);
constexpr fx STRAFE_STEP = FX_ONE * 26 / (10 * TICKS_PER_SECOND);
constexpr fx BACKWARD_STEP = FX_ONE * 20 / (10 * TICKS_PER_SECOND);
constexpr int SPRINT_NUMERATOR = 3;
constexpr int SPRINT_DENOMINATOR = 2;

constexpr int32_t MOUSE_TURN_UNITS = 23;
constexpr int32_t KEYBOARD_TURN_UNITS = 452;
constexpr fx BOB_PHASE_STEP = FX_ONE / 12;

constexpr int DAMAGE = 34;
constexpr uint16_t FIRE_COOLDOWN_TICKS = 18;
constexpr uint16_t MUZZLE_FLASH_TICKS = 4;
constexpr uint16_t HIT_FLASH_TICKS = 9;
constexpr fx MAX_RANGE = FX_ONE * 20;
constexpr fx EGG_HIT_RADIUS = FX_ONE * 3 / 10;

int tile_coordinate(int64_t value) {
    if (value >= 0) {
        return static_cast<int>(value / FX_ONE);
    }
    return -static_cast<int>((-value + FX_ONE - 1) / FX_ONE);
}

bool overlaps_wall(const Map &map, fx x, fx y) {
    const int min_x = tile_coordinate(static_cast<int64_t>(x) - PLAYER_RADIUS);
    const int max_x = tile_coordinate(static_cast<int64_t>(x) + PLAYER_RADIUS - 1);
    const int min_y = tile_coordinate(static_cast<int64_t>(y) - PLAYER_RADIUS);
    const int max_y = tile_coordinate(static_cast<int64_t>(y) + PLAYER_RADIUS - 1);

    for (int tile_y = min_y; tile_y <= max_y; ++tile_y) {
        for (int tile_x = min_x; tile_x <= max_x; ++tile_x) {
            if (map.is_wall(tile_x, tile_y)) {
                return true;
            }
        }
    }
    return false;
}

void move_axis(const Map &map, fx &axis, fx other_axis, fx displacement, bool moving_x) {
    fx remaining = displacement;
    while (remaining != 0) {
        fx step = remaining;
        if (step > COLLISION_STEP) {
            step = COLLISION_STEP;
        } else if (step < -COLLISION_STEP) {
            step = -COLLISION_STEP;
        }

        const auto blocked = [&](fx candidate) {
            return moving_x ? overlaps_wall(map, candidate, other_axis)
                            : overlaps_wall(map, other_axis, candidate);
        };

        if (!blocked(static_cast<fx>(axis + step))) {
            axis = static_cast<fx>(axis + step);
            remaining = static_cast<fx>(remaining - step);
            continue;
        }

        const fx direction = step > 0 ? 1 : -1;
        fx low = 0;
        fx high = step > 0 ? step : static_cast<fx>(-step);
        while (low < high) {
            const fx middle = static_cast<fx>(low + (high - low + 1) / 2);
            if (blocked(static_cast<fx>(axis + direction * middle))) {
                high = static_cast<fx>(middle - 1);
            } else {
                low = middle;
            }
        }
        axis = static_cast<fx>(axis + direction * low);
        break;
    }
}

fx movement_step(fx base_step, int8_t axis, bool sprinting) {
    int64_t step = static_cast<int64_t>(base_step) * static_cast<int64_t>(axis);
    if (sprinting) {
        step = step * SPRINT_NUMERATOR / SPRINT_DENOMINATOR;
    }
    return static_cast<fx>(step);
}

void decrement(uint16_t &timer) {
    if (timer > 0) {
        --timer;
    }
}

fx distance_to_wall(const Map &map, fx origin_x, fx origin_y, fx direction_x,
                    fx direction_y) {
    constexpr fx FAR_DISTANCE = MAX_RANGE + 1;
    constexpr fx INFINITE_DISTANCE = std::numeric_limits<fx>::max() / 2;

    int tile_x = tile_coordinate(origin_x);
    int tile_y = tile_coordinate(origin_y);
    if (map.is_wall(tile_x, tile_y)) {
        return 0;
    }

    int step_x = 0;
    int step_y = 0;
    fx next_x = INFINITE_DISTANCE;
    fx next_y = INFINITE_DISTANCE;
    fx delta_x = INFINITE_DISTANCE;
    fx delta_y = INFINITE_DISTANCE;

    if (direction_x > 0) {
        step_x = 1;
        next_x = fx_div(fx_from_int(tile_x + 1) - origin_x, direction_x);
        delta_x = fx_div(FX_ONE, direction_x);
    } else if (direction_x < 0) {
        step_x = -1;
        next_x = fx_div(origin_x - fx_from_int(tile_x), static_cast<fx>(-direction_x));
        delta_x = fx_div(FX_ONE, static_cast<fx>(-direction_x));
    }

    if (direction_y > 0) {
        step_y = 1;
        next_y = fx_div(fx_from_int(tile_y + 1) - origin_y, direction_y);
        delta_y = fx_div(FX_ONE, direction_y);
    } else if (direction_y < 0) {
        step_y = -1;
        next_y = fx_div(origin_y - fx_from_int(tile_y), static_cast<fx>(-direction_y));
        delta_y = fx_div(FX_ONE, static_cast<fx>(-direction_y));
    }

    const int iteration_limit = map.width + map.height + 4;
    for (int iteration = 0; iteration < iteration_limit; ++iteration) {
        fx distance = 0;
        if (next_x == next_y) {
            distance = next_x;
            if (distance > MAX_RANGE) {
                return FAR_DISTANCE;
            }

            const int adjacent_x = tile_x + step_x;
            const int adjacent_y = tile_y + step_y;
            if (map.is_wall(adjacent_x, tile_y) || map.is_wall(tile_x, adjacent_y) ||
                map.is_wall(adjacent_x, adjacent_y)) {
                return distance;
            }

            tile_x = adjacent_x;
            tile_y = adjacent_y;
            next_x = static_cast<fx>(next_x + delta_x);
            next_y = static_cast<fx>(next_y + delta_y);
            continue;
        }

        if (next_x < next_y) {
            distance = next_x;
            if (distance > MAX_RANGE) {
                return FAR_DISTANCE;
            }
            tile_x += step_x;
            next_x = static_cast<fx>(next_x + delta_x);
        } else {
            distance = next_y;
            if (distance > MAX_RANGE) {
                return FAR_DISTANCE;
            }
            tile_y += step_y;
            next_y = static_cast<fx>(next_y + delta_y);
        }

        if (map.is_wall(tile_x, tile_y)) {
            return distance;
        }
    }
    return FAR_DISTANCE;
}

} // namespace

void player_tick(GameState &gs, const InputFrame &in) {
    decrement(gs.player.fire_cooldown);
    decrement(gs.player.hurt_flash);
    decrement(gs.muzzle_flash);

    const int32_t turn_delta = static_cast<int32_t>(in.mouse_dx) * MOUSE_TURN_UNITS +
                               static_cast<int32_t>(in.turn) * KEYBOARD_TURN_UNITS;
    gs.player.angle =
        static_cast<angle_t>(static_cast<int32_t>(gs.player.angle) + turn_delta);

    const bool sprinting = in.held(InputFrame::Sprint);
    const fx forward = in.move_y >= 0 ? movement_step(FORWARD_STEP, in.move_y, sprinting)
                                     : movement_step(BACKWARD_STEP, in.move_y, sprinting);
    const fx strafe = movement_step(STRAFE_STEP, in.move_x, sprinting);
    const fx facing_x = fx_cos(gs.player.angle);
    const fx facing_y = fx_sin(gs.player.angle);
    const fx displacement_x =
        static_cast<fx>(fx_mul(facing_x, forward) - fx_mul(facing_y, strafe));
    const fx displacement_y =
        static_cast<fx>(fx_mul(facing_y, forward) + fx_mul(facing_x, strafe));

    const fx previous_x = gs.player.x;
    const fx previous_y = gs.player.y;
    move_axis(gs.level.map, gs.player.x, gs.player.y, displacement_x, true);
    move_axis(gs.level.map, gs.player.y, gs.player.x, displacement_y, false);

    if (gs.player.x != previous_x || gs.player.y != previous_y) {
        gs.player.bob = static_cast<fx>((gs.player.bob + BOB_PHASE_STEP) % FX_ONE);
    }

    if (in.held(InputFrame::Fire) && gs.player.fire_cooldown == 0 && gs.player.ammo > 0) {
        fire(gs);
    }
}

void fire(GameState &gs) {
    if (gs.player.fire_cooldown != 0 || gs.player.ammo <= 0) {
        return;
    }

    --gs.player.ammo;
    gs.player.fire_cooldown = FIRE_COOLDOWN_TICKS;
    gs.muzzle_flash = MUZZLE_FLASH_TICKS;
    gs.events.push_back({EventType::Shot, 0});

    const fx direction_x = fx_cos(gs.player.angle);
    const fx direction_y = fx_sin(gs.player.angle);
    const fx wall_distance =
        distance_to_wall(gs.level.map, gs.player.x, gs.player.y, direction_x, direction_y);
    const int64_t max_range_squared = static_cast<int64_t>(MAX_RANGE) * MAX_RANGE;

    Entity *nearest = nullptr;
    fx nearest_distance = MAX_RANGE + 1;
    for (Entity &entity : gs.entities) {
        if (!entity.alive || entity.type != EntityType::Egg) {
            continue;
        }

        const fx offset_x = static_cast<fx>(entity.x - gs.player.x);
        const fx offset_y = static_cast<fx>(entity.y - gs.player.y);
        const int64_t distance_squared = static_cast<int64_t>(offset_x) * offset_x +
                                         static_cast<int64_t>(offset_y) * offset_y;
        if (distance_squared > max_range_squared) {
            continue;
        }

        const int64_t dot = static_cast<int64_t>(offset_x) * direction_x +
                            static_cast<int64_t>(offset_y) * direction_y;
        const fx along_ray = static_cast<fx>(dot >> FX_SHIFT);
        if (along_ray < 0 || along_ray > MAX_RANGE || along_ray > wall_distance ||
            along_ray >= nearest_distance) {
            continue;
        }

        int64_t cross = static_cast<int64_t>(offset_x) * direction_y -
                        static_cast<int64_t>(offset_y) * direction_x;
        if (cross < 0) {
            cross = -cross;
        }
        const fx lateral_distance = static_cast<fx>(cross >> FX_SHIFT);
        if (lateral_distance > EGG_HIT_RADIUS) {
            continue;
        }

        nearest = &entity;
        nearest_distance = along_ray;
    }

    if (nearest != nullptr) {
        nearest->health = static_cast<int16_t>(nearest->health - DAMAGE);
        nearest->hit_flash = HIT_FLASH_TICKS;
        gs.events.push_back({EventType::EggHit, nearest->id});
    }
}

} // namespace eh
