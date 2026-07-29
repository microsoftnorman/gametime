#include "core/raycast.h"

#include "core/framebuffer.h"
#include "core/state.h"
#include "core/textures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eh {

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float MIN_DEPTH = 0.0001f;
constexpr float FAR_DEPTH = 1.0e6f;

struct RayHit {
    int map_x = 0;
    int map_y = 0;
    int side = 0; // 0 crosses an X grid line, 1 crosses a Y grid line.
    float distance = FAR_DEPTH;
    bool hit = false;
};

uint32_t blend_color(uint32_t from, uint32_t to, float amount) {
    const auto blend_channel = [from, to, amount](int shift) {
        const float start = static_cast<float>((from >> shift) & 0xffu);
        const float end = static_cast<float>((to >> shift) & 0xffu);
        return static_cast<uint8_t>(start + (end - start) * amount);
    };
    return rgba(blend_channel(0), blend_channel(8), blend_channel(16));
}

void fill_background(Framebuffer &fb) {
    constexpr uint32_t CEILING_TOP = 0xff342c24u;
    constexpr uint32_t CEILING_HORIZON = 0xff6c6254u;
    constexpr uint32_t FLOOR_HORIZON = 0xff4d596eu;
    constexpr uint32_t FLOOR_BOTTOM = 0xff181d2du;
    constexpr int HORIZON = Framebuffer::H / 2;

    for (int y = 0; y < Framebuffer::H; ++y) {
        const bool ceiling = y < HORIZON;
        const float amount = ceiling ? static_cast<float>(y) / static_cast<float>(HORIZON - 1)
                                     : static_cast<float>(y - HORIZON) /
                                           static_cast<float>(Framebuffer::H - HORIZON - 1);
        const uint32_t color = ceiling ? blend_color(CEILING_TOP, CEILING_HORIZON, amount)
                                       : blend_color(FLOOR_HORIZON, FLOOR_BOTTOM, amount);

        for (int x = 0; x < Framebuffer::W; ++x) {
            fb.pixels[y * Framebuffer::W + x] = color;
        }
    }
}

RayHit cast_ray(const Map &map, float position_x, float position_y, float ray_direction_x,
                float ray_direction_y) {
    int map_x = static_cast<int>(std::floor(position_x));
    int map_y = static_cast<int>(std::floor(position_y));

    const float delta_x = ray_direction_x == 0.0f ? FAR_DEPTH : std::abs(1.0f / ray_direction_x);
    const float delta_y = ray_direction_y == 0.0f ? FAR_DEPTH : std::abs(1.0f / ray_direction_y);

    const int step_x = ray_direction_x < 0.0f ? -1 : 1;
    const int step_y = ray_direction_y < 0.0f ? -1 : 1;
    float side_distance_x = ray_direction_x < 0.0f
                                ? (position_x - static_cast<float>(map_x)) * delta_x
                                : (static_cast<float>(map_x + 1) - position_x) * delta_x;
    float side_distance_y = ray_direction_y < 0.0f
                                ? (position_y - static_cast<float>(map_y)) * delta_y
                                : (static_cast<float>(map_y + 1) - position_y) * delta_y;

    const int max_steps = std::max(1, map.width + map.height + 2);
    for (int step = 0; step < max_steps; ++step) {
        RayHit result;
        if (side_distance_x < side_distance_y) {
            result.distance = side_distance_x;
            side_distance_x += delta_x;
            map_x += step_x;
            result.side = 0;
        } else {
            result.distance = side_distance_y;
            side_distance_y += delta_y;
            map_y += step_y;
            result.side = 1;
        }

        if (map.is_wall(map_x, map_y)) {
            result.map_x = map_x;
            result.map_y = map_y;
            result.hit = true;
            return result;
        }
    }

    return {};
}

} // namespace

void render_walls(const GameState &gs, Framebuffer &fb) {
    fill_background(fb);

    const float position_x = fx_to_float(gs.player.x);
    const float position_y = fx_to_float(gs.player.y);
    const float angle =
        static_cast<float>(gs.player.angle) * TWO_PI / static_cast<float>(UINT16_MAX + 1u);
    const float direction_x = std::cos(angle);
    const float direction_y = std::sin(angle);
    const float camera_plane_scale = std::tan(FOV_RADIANS * 0.5f);
    const float plane_x = -direction_y * camera_plane_scale;
    const float plane_y = direction_x * camera_plane_scale;

    for (int column = 0; column < Framebuffer::W; ++column) {
        const float camera_x =
            2.0f * (static_cast<float>(column) + 0.5f) / static_cast<float>(Framebuffer::W) - 1.0f;
        const float ray_direction_x = direction_x + plane_x * camera_x;
        const float ray_direction_y = direction_y + plane_y * camera_x;
        const RayHit hit =
            cast_ray(gs.level.map, position_x, position_y, ray_direction_x, ray_direction_y);

        float perpendicular_distance = hit.distance;
        if (!hit.hit || !std::isfinite(perpendicular_distance)) {
            perpendicular_distance = FAR_DEPTH;
        }
        perpendicular_distance = std::max(perpendicular_distance, MIN_DEPTH);
        fb.depth[column] = perpendicular_distance;

        if (!hit.hit) {
            continue;
        }

        const float projected_height =
            std::min(static_cast<float>(Framebuffer::H) / perpendicular_distance,
                     static_cast<float>(Framebuffer::H * 4096));
        const int wall_height = std::max(1, static_cast<int>(projected_height));
        const int wall_top = Framebuffer::H / 2 - wall_height / 2;
        const int wall_bottom = wall_top + wall_height;
        const int draw_start = std::max(0, wall_top);
        const int draw_end = std::min(Framebuffer::H, wall_bottom);

        float wall_position = hit.side == 0 ? position_y + perpendicular_distance * ray_direction_y
                                            : position_x + perpendicular_distance * ray_direction_x;
        wall_position -= std::floor(wall_position);
        int texture_x = std::clamp(static_cast<int>(wall_position * WALL_TEXTURE_SIZE), 0,
                                   WALL_TEXTURE_SIZE - 1);
        if ((hit.side == 0 && ray_direction_x > 0.0f) ||
            (hit.side == 1 && ray_direction_y < 0.0f)) {
            texture_x = WALL_TEXTURE_SIZE - texture_x - 1;
        }

        const float texture_step =
            static_cast<float>(WALL_TEXTURE_SIZE) / static_cast<float>(wall_height);
        float texture_position = static_cast<float>(draw_start - wall_top) * texture_step;
        float brightness = distance_brightness(perpendicular_distance);
        if (hit.side == 1) {
            brightness *= 0.7f;
        }

        const Tile tile = gs.level.map.at(hit.map_x, hit.map_y);
        for (int y = draw_start; y < draw_end; ++y) {
            const int texture_y =
                std::clamp(static_cast<int>(texture_position), 0, WALL_TEXTURE_SIZE - 1);
            texture_position += texture_step;
            fb.pixels[y * Framebuffer::W + column] =
                shade(sample_wall(tile, texture_x, texture_y), brightness);
        }
    }
}

} // namespace eh
