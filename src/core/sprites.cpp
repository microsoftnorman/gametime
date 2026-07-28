#include "core/sprites.h"

#include "core/framebuffer.h"
#include "core/state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace eh {

namespace {

constexpr int SPRITE_TEXTURE_SIZE = 48;
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float NEAR_PLANE = 0.05f;
constexpr float CAMERA_HEIGHT = 0.5f;

struct SpriteDimensions {
    float width;
    float height;
};

struct ProjectedEntity {
    const Entity *entity;
    float depth;
    float lateral;
};

float ellipse_metric(float x, float y, float center_x, float center_y, float radius_x,
                     float radius_y) {
    const float dx = (x - center_x) / radius_x;
    const float dy = (y - center_y) / radius_y;
    return dx * dx + dy * dy;
}

bool inside_ellipse(float x, float y, float center_x, float center_y, float radius_x,
                    float radius_y) {
    return ellipse_metric(x, y, center_x, center_y, radius_x, radius_y) <= 1.0f;
}

float distance_to_segment(float x, float y, float x0, float y0, float x1, float y1) {
    const float segment_x = x1 - x0;
    const float segment_y = y1 - y0;
    const float length_squared = segment_x * segment_x + segment_y * segment_y;
    if (length_squared <= 0.0f) {
        return std::hypot(x - x0, y - y0);
    }

    const float amount =
        std::clamp(((x - x0) * segment_x + (y - y0) * segment_y) / length_squared, 0.0f, 1.0f);
    return std::hypot(x - (x0 + segment_x * amount), y - (y0 + segment_y * amount));
}

uint32_t blend_over(uint32_t destination, uint32_t source) {
    const uint32_t alpha = (source >> 24) & 0xffu;
    if (alpha == 0u) {
        return destination;
    }
    if (alpha == 255u) {
        return source;
    }

    const uint32_t inverse_alpha = 255u - alpha;
    const auto blend_channel = [alpha, inverse_alpha](uint32_t destination_channel,
                                                      uint32_t source_channel) {
        return static_cast<uint8_t>(
            (source_channel * alpha + destination_channel * inverse_alpha + 127u) / 255u);
    };

    return rgba(blend_channel(destination & 0xffu, source & 0xffu),
                blend_channel((destination >> 8) & 0xffu, (source >> 8) & 0xffu),
                blend_channel((destination >> 16) & 0xffu, (source >> 16) & 0xffu));
}

uint32_t toward_white(uint32_t color, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    const auto brighten = [amount](uint32_t channel) {
        return static_cast<uint8_t>(static_cast<float>(channel) +
                                    (255.0f - static_cast<float>(channel)) * amount);
    };

    return rgba(brighten(color & 0xffu), brighten((color >> 8) & 0xffu),
                brighten((color >> 16) & 0xffu), static_cast<uint8_t>((color >> 24) & 0xffu));
}

uint32_t hit_tint(uint32_t color) {
    const uint8_t red = static_cast<uint8_t>(color & 0xffu);
    const uint8_t green = static_cast<uint8_t>((color >> 8) & 0xffu);
    const uint8_t blue = static_cast<uint8_t>((color >> 16) & 0xffu);
    const uint8_t alpha = static_cast<uint8_t>((color >> 24) & 0xffu);
    return rgba(static_cast<uint8_t>(std::max(224, static_cast<int>(red))),
                static_cast<uint8_t>(green / 3u), static_cast<uint8_t>(blue / 3u), alpha);
}

uint32_t sample_egg(int texture_x, int texture_y) {
    const float x = (static_cast<float>(texture_x) + 0.5f) * (2.0f / SPRITE_TEXTURE_SIZE) - 1.0f;
    const float y =
        (static_cast<float>(texture_y) + 0.5f) / static_cast<float>(SPRITE_TEXTURE_SIZE);

    uint32_t color = 0u;
    const float shadow = ellipse_metric(x, y, 0.0f, 0.875f, 0.76f, 0.075f);
    if (shadow <= 1.0f) {
        const auto alpha = static_cast<uint8_t>(25.0f + (1.0f - shadow) * 70.0f);
        color = rgba(18, 12, 18, alpha);
    }

    const float normalized_y = (y - 0.48f) / 0.40f;
    if (std::abs(normalized_y) >= 1.0f) {
        return color;
    }

    const float taper = 0.78f + 0.26f * std::clamp((y - 0.08f) / 0.80f, 0.0f, 1.0f);
    const float half_width =
        0.62f * std::sqrt(std::max(0.0f, 1.0f - normalized_y * normalized_y)) * taper;
    if (std::abs(x) > half_width) {
        return color;
    }

    const bool shell_edge = half_width - std::abs(x) < 0.045f || std::abs(normalized_y) > 0.93f;
    if (shell_edge) {
        color = rgba(126, 93, 64);
    } else if (x < -0.22f && y < 0.45f) {
        color = rgba(255, 249, 218);
    } else if (x > 0.34f || y > 0.72f) {
        color = rgba(211, 187, 139);
    } else {
        color = rgba(241, 226, 184);
    }

    const float left_eye = ellipse_metric(x, y, -0.24f, 0.405f, 0.17f, 0.09f);
    const float right_eye = ellipse_metric(x, y, 0.24f, 0.405f, 0.17f, 0.09f);
    if (left_eye <= 1.0f || right_eye <= 1.0f) {
        color = (left_eye <= 0.72f || right_eye <= 0.72f) ? rgba(252, 238, 190) : rgba(72, 39, 30);
    }
    if (inside_ellipse(x, y, -0.19f, 0.42f, 0.055f, 0.067f) ||
        inside_ellipse(x, y, 0.19f, 0.42f, 0.055f, 0.067f)) {
        color = rgba(38, 18, 20);
    }
    if (inside_ellipse(x, y, -0.175f, 0.397f, 0.014f, 0.018f) ||
        inside_ellipse(x, y, 0.175f, 0.397f, 0.014f, 0.018f)) {
        color = rgba(255, 94, 64);
    }

    if (distance_to_segment(x, y, -0.43f, 0.30f, -0.08f, 0.37f) < 0.032f ||
        distance_to_segment(x, y, 0.08f, 0.37f, 0.43f, 0.30f) < 0.032f) {
        color = rgba(67, 31, 25);
    }

    if (distance_to_segment(x, y, -0.47f, 0.60f, -0.29f, 0.65f) < 0.020f ||
        distance_to_segment(x, y, -0.29f, 0.65f, -0.12f, 0.58f) < 0.020f ||
        distance_to_segment(x, y, -0.12f, 0.58f, 0.04f, 0.69f) < 0.020f ||
        distance_to_segment(x, y, 0.04f, 0.69f, 0.22f, 0.60f) < 0.020f ||
        distance_to_segment(x, y, 0.22f, 0.60f, 0.45f, 0.66f) < 0.020f ||
        distance_to_segment(x, y, -0.29f, 0.65f, -0.35f, 0.74f) < 0.016f ||
        distance_to_segment(x, y, 0.22f, 0.60f, 0.29f, 0.52f) < 0.016f) {
        color = rgba(91, 45, 31);
    }

    return color;
}

uint32_t sample_jellybean(const Entity &entity, int texture_x, int texture_y) {
    const float x = (static_cast<float>(texture_x) + 0.5f) * (2.0f / SPRITE_TEXTURE_SIZE) - 1.0f;
    const float y =
        (static_cast<float>(texture_y) + 0.5f) / static_cast<float>(SPRITE_TEXTURE_SIZE);

    uint32_t color = 0u;
    const float shadow = ellipse_metric(x, y, 0.0f, 0.845f, 0.66f, 0.07f);
    if (shadow <= 1.0f) {
        color = rgba(18, 12, 22, static_cast<uint8_t>(30.0f + 45.0f * (1.0f - shadow)));
    }

    const bool main_lobe = inside_ellipse(x, y, -0.10f, 0.57f, 0.58f, 0.30f);
    const bool notch = inside_ellipse(x, y, 0.36f, 0.43f, 0.30f, 0.18f);
    const bool lower_lobe = inside_ellipse(x, y, 0.29f, 0.65f, 0.31f, 0.21f);
    if ((!main_lobe || notch) && !lower_lobe) {
        return color;
    }

    static const std::array<uint32_t, 6> palette = {rgba(246, 65, 126), rgba(65, 209, 245),
                                                    rgba(174, 79, 238), rgba(255, 194, 51),
                                                    rgba(76, 220, 112), rgba(245, 91, 55)};
    const uint32_t base = palette[entity.id % palette.size()];

    const float main_metric = ellipse_metric(x, y, -0.10f, 0.57f, 0.58f, 0.30f);
    if ((main_metric > 0.82f && !lower_lobe) || y > 0.79f) {
        color = shade(base, 0.58f);
    } else if (x < -0.25f && y < 0.55f) {
        color = toward_white(base, 0.22f);
    } else {
        color = base;
    }

    if (inside_ellipse(x, y, -0.29f, 0.47f, 0.13f, 0.065f)) {
        color = rgba(255, 255, 244, 205);
    }
    return color;
}

uint32_t sample_carrot(int texture_x, int texture_y) {
    const float x = (static_cast<float>(texture_x) + 0.5f) * (2.0f / SPRITE_TEXTURE_SIZE) - 1.0f;
    const float y =
        (static_cast<float>(texture_y) + 0.5f) / static_cast<float>(SPRITE_TEXTURE_SIZE);

    uint32_t color = 0u;
    const float shadow = ellipse_metric(x, y, 0.0f, 0.89f, 0.58f, 0.055f);
    if (shadow <= 1.0f) {
        color = rgba(14, 12, 14, static_cast<uint8_t>(25.0f + 45.0f * (1.0f - shadow)));
    }

    if (distance_to_segment(x, y, 0.0f, 0.38f, -0.43f, 0.10f) < 0.065f ||
        distance_to_segment(x, y, 0.0f, 0.38f, -0.12f, 0.035f) < 0.068f ||
        distance_to_segment(x, y, 0.0f, 0.38f, 0.19f, 0.055f) < 0.070f ||
        distance_to_segment(x, y, 0.0f, 0.38f, 0.45f, 0.17f) < 0.060f) {
        color = (x < -0.10f) ? rgba(43, 139, 61) : rgba(66, 174, 68);
    }

    if (y < 0.32f || y > 0.91f) {
        return color;
    }

    const float amount = (y - 0.32f) / 0.59f;
    const float center = 0.035f * amount;
    const float half_width = 0.35f * (1.0f - amount) + 0.025f;
    if (std::abs(x - center) > half_width) {
        return color;
    }

    if (half_width - std::abs(x - center) < 0.035f) {
        color = rgba(151, 62, 22);
    } else if (x < center - half_width * 0.32f) {
        color = rgba(255, 164, 45);
    } else if (x > center + half_width * 0.45f) {
        color = rgba(201, 77, 23);
    } else {
        color = rgba(241, 106, 25);
    }

    if ((std::abs(y - 0.49f) < 0.014f && x > -0.20f && x < 0.12f) ||
        (std::abs(y - 0.62f) < 0.014f && x > -0.08f && x < 0.20f) ||
        (std::abs(y - 0.74f) < 0.014f && x > -0.10f && x < 0.10f)) {
        color = rgba(169, 62, 20);
    }
    return color;
}

uint32_t sample_basket(int texture_x, int texture_y, float pulse) {
    const float x = (static_cast<float>(texture_x) + 0.5f) * (2.0f / SPRITE_TEXTURE_SIZE) - 1.0f;
    const float y =
        (static_cast<float>(texture_y) + 0.5f) / static_cast<float>(SPRITE_TEXTURE_SIZE);

    uint32_t color = 0u;
    if (pulse >= 0.0f && inside_ellipse(x, y, 0.0f, 0.54f, 0.91f, 0.47f)) {
        color = rgba(255, 205, 62, static_cast<uint8_t>(22.0f + pulse * 42.0f));
    }

    const float shadow = ellipse_metric(x, y, 0.0f, 0.90f, 0.80f, 0.065f);
    if (shadow <= 1.0f) {
        color = rgba(18, 12, 12, static_cast<uint8_t>(35.0f + 50.0f * (1.0f - shadow)));
    }

    const bool handle_outer = inside_ellipse(x, y, 0.0f, 0.47f, 0.67f, 0.37f);
    const bool handle_inner = inside_ellipse(x, y, 0.0f, 0.48f, 0.49f, 0.27f);
    if (handle_outer && !handle_inner && y < 0.58f) {
        color = rgba(112, 65, 29);
    }

    if (y >= 0.43f && y <= 0.89f) {
        const float amount = (y - 0.43f) / 0.46f;
        const float half_width = 0.73f - amount * 0.14f;
        if (std::abs(x) <= half_width) {
            const bool outline = half_width - std::abs(x) < 0.035f || y > 0.855f;
            const bool vertical_reed = texture_x % 7 <= 1;
            const bool horizontal_reed = texture_y % 6 <= 1;
            if (outline) {
                color = rgba(91, 49, 23);
            } else if (vertical_reed && horizontal_reed) {
                color = rgba(105, 57, 25);
            } else if (vertical_reed) {
                color = rgba(182, 113, 44);
            } else if (horizontal_reed) {
                color = rgba(128, 72, 29);
            } else {
                color = rgba(213, 144, 62);
            }
        }
    }

    if (y >= 0.42f && y <= 0.53f && std::abs(x) <= 0.78f) {
        color = (y < 0.455f || y > 0.50f) ? rgba(91, 49, 23) : rgba(225, 154, 64);
    }

    if (pulse >= 0.0f && ((color >> 24) & 0xffu) == 255u) {
        color = toward_white(color, pulse * 0.20f);
    }
    return color;
}

uint32_t sample_sprite(const Entity &entity, int texture_x, int texture_y, float basket_pulse) {
    uint32_t color = 0u;
    switch (entity.type) {
    case EntityType::Egg:
        color = sample_egg(texture_x, texture_y);
        break;
    case EntityType::Jellybean:
        color = sample_jellybean(entity, texture_x, texture_y);
        break;
    case EntityType::Carrot:
        color = sample_carrot(texture_x, texture_y);
        break;
    case EntityType::Basket:
        color = sample_basket(texture_x, texture_y, basket_pulse);
        break;
    }

    if (entity.type == EntityType::Egg && entity.hit_flash > 0 && color != 0u) {
        color = hit_tint(color);
    }
    return color;
}

SpriteDimensions dimensions_for(EntityType type) {
    switch (type) {
    case EntityType::Egg:
        return {0.84f, 1.08f};
    case EntityType::Jellybean:
        return {0.42f, 0.52f};
    case EntityType::Carrot:
        return {0.48f, 0.70f};
    case EntityType::Basket:
        return {1.12f, 0.94f};
    }
    return {1.0f, 1.0f};
}

uint32_t sample_weapon(float x, float y, float flash_strength) {
    constexpr float muzzle_x = 58.0f;
    constexpr float muzzle_y = 42.0f;
    constexpr float axis_x = 0.882f;
    constexpr float axis_y = 0.471f;
    constexpr float perpendicular_x = -axis_y;
    constexpr float perpendicular_y = axis_x;
    constexpr float body_length = 178.0f;

    uint32_t color = 0u;

    if (flash_strength > 0.0f) {
        const float flash_x = muzzle_x - axis_x * 22.0f;
        const float flash_y = muzzle_y - axis_y * 22.0f;
        const float dx = x - flash_x;
        const float dy = y - flash_y;
        const float radius = std::hypot(dx, dy);
        if (radius < 36.0f) {
            color = rgba(255, 155, 24,
                         static_cast<uint8_t>((1.0f - radius / 36.0f) * 105.0f * flash_strength));
        }

        const bool long_ray = (std::abs(dx) < 3.5f && std::abs(dy) < 34.0f) ||
                              (std::abs(dy) < 3.5f && std::abs(dx) < 34.0f) ||
                              (std::abs(dx - dy) < 4.5f && radius < 30.0f) ||
                              (std::abs(dx + dy) < 4.5f && radius < 30.0f);
        if (long_ray || radius < 17.0f) {
            color = radius < 9.0f ? rgba(255, 255, 230) : rgba(255, 215, 55);
        }
    }

    const bool leaf = distance_to_segment(x, y, 211.0f, 127.0f, 270.0f, 105.0f) < 9.0f ||
                      distance_to_segment(x, y, 211.0f, 127.0f, 275.0f, 143.0f) < 10.0f ||
                      distance_to_segment(x, y, 211.0f, 127.0f, 253.0f, 176.0f) < 11.0f ||
                      distance_to_segment(x, y, 211.0f, 127.0f, 224.0f, 181.0f) < 9.0f ||
                      distance_to_segment(x, y, 211.0f, 127.0f, 241.0f, 88.0f) < 8.0f;
    if (leaf) {
        color = (static_cast<int>(x + y) % 11 < 4) ? rgba(38, 116, 52) : rgba(61, 157, 65);
    }

    if (inside_ellipse(x, y, 242.0f, 169.0f, 46.0f, 29.0f)) {
        color = y > 174.0f ? rgba(205, 176, 142) : rgba(243, 222, 183);
    }

    const float trigger_outer = ellipse_metric(x, y, 177.0f, 145.0f, 22.0f, 26.0f);
    const float trigger_inner = ellipse_metric(x, y, 177.0f, 145.0f, 14.0f, 18.0f);
    if (trigger_outer <= 1.0f && trigger_inner > 1.0f) {
        color = rgba(44, 68, 39);
    }

    const float grip_distance = distance_to_segment(x, y, 185.0f, 116.0f, 197.0f, 180.0f);
    if (grip_distance < 18.0f) {
        color = grip_distance > 14.0f ? rgba(50, 43, 31) : rgba(92, 82, 52);
        if (grip_distance < 13.0f && static_cast<int>(y) % 9 < 3) {
            color = rgba(57, 74, 40);
        }
    }

    const float relative_x = x - muzzle_x;
    const float relative_y = y - muzzle_y;
    const float along = relative_x * axis_x + relative_y * axis_y;
    const float side = relative_x * perpendicular_x + relative_y * perpendicular_y;
    const float amount = along / body_length;
    const float radius = 10.5f + 25.0f * amount + 2.0f * std::sin(amount * PI);
    if (along >= 0.0f && along <= body_length && std::abs(side) <= radius) {
        if (radius - std::abs(side) < 3.0f) {
            color = rgba(120, 46, 20);
        } else if (side < -radius * 0.34f) {
            color = rgba(255, 161, 42);
        } else if (side > radius * 0.56f) {
            color = rgba(179, 61, 20);
        } else {
            color = rgba(235, 91, 22);
        }

        const float ridge = std::fmod(along + 4.0f, 25.0f);
        if (ridge < 2.5f && side > -radius * 0.55f && side < radius * 0.72f) {
            color = rgba(153, 53, 20);
        }
        if (side < -radius * 0.58f && along > 38.0f && along < 142.0f) {
            color = rgba(255, 192, 66);
        }
    }

    if (distance_to_segment(x, y, 125.0f, 70.0f, 158.0f, 88.0f) < 4.0f) {
        color = rgba(48, 111, 49);
    }
    if (inside_ellipse(x, y, 146.0f, 75.0f, 6.0f, 8.0f)) {
        color = rgba(198, 231, 94);
    }

    const float muzzle_ring = (along * along) / 32.0f + (side * side) / 225.0f;
    if (muzzle_ring <= 1.25f) {
        color = rgba(86, 48, 29);
    }
    if (muzzle_ring <= 0.76f) {
        color = rgba(230, 125, 37);
    }
    if (muzzle_ring <= 0.34f) {
        color = rgba(38, 28, 25);
    }
    if (muzzle_ring <= 0.11f && flash_strength > 0.0f) {
        color = rgba(255, 246, 161);
    }

    if (inside_ellipse(x, y, 207.0f, 151.0f, 22.0f, 15.0f) ||
        inside_ellipse(x, y, 218.0f, 158.0f, 18.0f, 13.0f)) {
        color = rgba(244, 222, 184);
    }
    if (inside_ellipse(x, y, 218.0f, 158.0f, 7.0f, 5.0f)) {
        color = rgba(238, 157, 158);
    }

    return color;
}

} // namespace

void render_sprites(const GameState &gs, Framebuffer &fb) {
    const float player_x = fx_to_float(gs.player.x);
    const float player_y = fx_to_float(gs.player.y);
    const float direction_x = fx_to_float(fx_cos(gs.player.angle));
    const float direction_y = fx_to_float(fx_sin(gs.player.angle));
    const float right_x = -direction_y;
    const float right_y = direction_x;

    std::vector<ProjectedEntity> visible;
    visible.reserve(gs.entities.size());
    for (const Entity &entity : gs.entities) {
        if (!entity.alive) {
            continue;
        }

        const float relative_x = fx_to_float(entity.x) - player_x;
        const float relative_y = fx_to_float(entity.y) - player_y;
        const float depth = relative_x * direction_x + relative_y * direction_y;
        if (!std::isfinite(depth) || depth <= NEAR_PLANE) {
            continue;
        }

        const float lateral = relative_x * right_x + relative_y * right_y;
        if (std::isfinite(lateral)) {
            visible.push_back({&entity, depth, lateral});
        }
    }

    std::stable_sort(visible.begin(), visible.end(),
                     [](const ProjectedEntity &left, const ProjectedEntity &right) {
                         if (left.depth != right.depth) {
                             return left.depth > right.depth;
                         }
                         return left.entity->id < right.entity->id;
                     });

    const float projection =
        (static_cast<float>(Framebuffer::W) * 0.5f) / std::tan(FOV_RADIANS * 0.5f);
    const float horizon = static_cast<float>(Framebuffer::H) * 0.5f;

    const bool basket_active = gs.eggs_remaining == 0;
    const float pulse_wave = std::sin(static_cast<float>(gs.tick) * 0.18f);
    const float basket_pulse = basket_active ? 0.5f + 0.5f * pulse_wave : -1.0f;
    const float basket_scale = basket_active ? 1.0f + pulse_wave * 0.08f : 1.0f;

    for (const ProjectedEntity &sprite : visible) {
        SpriteDimensions dimensions = dimensions_for(sprite.entity->type);
        if (sprite.entity->type == EntityType::Basket) {
            dimensions.width *= basket_scale;
            dimensions.height *= basket_scale;
        }

        const float projected_height = dimensions.height * projection / sprite.depth;
        const float projected_width = dimensions.width * projection / sprite.depth;
        const float center_x =
            static_cast<float>(Framebuffer::W) * 0.5f + sprite.lateral * projection / sprite.depth;
        const float bottom = horizon + CAMERA_HEIGHT * projection / sprite.depth;
        const float left = center_x - projected_width * 0.5f;
        const float right = center_x + projected_width * 0.5f;
        const float top = bottom - projected_height;

        if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) ||
            !std::isfinite(bottom) || projected_width <= 0.0f || projected_height <= 0.0f ||
            right <= 0.0f || left >= static_cast<float>(Framebuffer::W) || bottom <= 0.0f ||
            top >= static_cast<float>(Framebuffer::H)) {
            continue;
        }

        const int draw_left = static_cast<int>(std::max(0.0f, std::floor(left)));
        const int draw_right =
            static_cast<int>(std::min(static_cast<float>(Framebuffer::W), std::ceil(right))) - 1;
        const int draw_top = static_cast<int>(std::max(0.0f, std::floor(top)));
        const int draw_bottom =
            static_cast<int>(std::min(static_cast<float>(Framebuffer::H), std::ceil(bottom))) - 1;

        const float brightness = std::clamp(1.0f - sprite.depth / 16.0f, 0.25f, 1.0f);
        for (int screen_x = draw_left; screen_x <= draw_right; ++screen_x) {
            if (sprite.depth >= fb.depth[screen_x]) {
                continue;
            }

            const float u = (static_cast<float>(screen_x) + 0.5f - left) / projected_width;
            const int texture_x =
                std::clamp(static_cast<int>(u * static_cast<float>(SPRITE_TEXTURE_SIZE)), 0,
                           SPRITE_TEXTURE_SIZE - 1);

            for (int screen_y = draw_top; screen_y <= draw_bottom; ++screen_y) {
                const float v = (static_cast<float>(screen_y) + 0.5f - top) / projected_height;
                const int texture_y =
                    std::clamp(static_cast<int>(v * static_cast<float>(SPRITE_TEXTURE_SIZE)), 0,
                               SPRITE_TEXTURE_SIZE - 1);
                uint32_t color = sample_sprite(*sprite.entity, texture_x, texture_y, basket_pulse);
                if (((color >> 24) & 0xffu) == 0u) {
                    continue;
                }

                color = shade(color, brightness);
                const std::size_t pixel_index =
                    static_cast<std::size_t>(screen_y) * Framebuffer::W +
                    static_cast<std::size_t>(screen_x);
                fb.pixels[pixel_index] = blend_over(fb.pixels[pixel_index], color);
            }
        }
    }
}

void render_weapon(const GameState &gs, Framebuffer &fb) {
    constexpr int WEAPON_WIDTH = 286;
    constexpr int WEAPON_HEIGHT = 184;

    const float bob_phase = fx_to_float(gs.player.bob) * TWO_PI;
    const float bob_x = std::sin(bob_phase) * 5.0f;
    const float bob_y = std::abs(std::sin(bob_phase * 2.0f)) * 4.0f;

    float recoil = 0.0f;
    float flash_strength = 0.0f;
    if (gs.muzzle_flash > 0) {
        const float initial_strength = std::min(static_cast<float>(gs.muzzle_flash) / 4.0f, 1.0f);
        recoil = 0.45f + initial_strength * 0.55f;
        flash_strength = 0.65f + initial_strength * 0.35f;
    }

    const int origin_x =
        Framebuffer::W / 2 - 112 + static_cast<int>(std::lround(bob_x + recoil * 9.0f));
    const int origin_y =
        Framebuffer::H - WEAPON_HEIGHT + static_cast<int>(std::lround(bob_y + recoil * 11.0f));
    const int draw_left = std::max(0, origin_x);
    const int draw_right = std::min(Framebuffer::W, origin_x + WEAPON_WIDTH);
    const int draw_top = std::max(0, origin_y);
    const int draw_bottom = std::min(Framebuffer::H, origin_y + WEAPON_HEIGHT);

    for (int screen_y = draw_top; screen_y < draw_bottom; ++screen_y) {
        const float local_y = static_cast<float>(screen_y - origin_y) + 0.5f;
        for (int screen_x = draw_left; screen_x < draw_right; ++screen_x) {
            const float local_x = static_cast<float>(screen_x - origin_x) + 0.5f;
            const uint32_t color = sample_weapon(local_x, local_y, flash_strength);
            if (((color >> 24) & 0xffu) == 0u) {
                continue;
            }

            const std::size_t pixel_index = static_cast<std::size_t>(screen_y) * Framebuffer::W +
                                            static_cast<std::size_t>(screen_x);
            fb.pixels[pixel_index] = blend_over(fb.pixels[pixel_index], color);
        }
    }
}

} // namespace eh
