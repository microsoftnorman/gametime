#pragma once

#include <algorithm>
#include <cstdint>

namespace eh {

struct GameState;

struct Framebuffer {
    static constexpr int W = 640, H = 360;
    uint32_t *pixels; // RGBA8888: 0xAABBGGRR, so little-endian memory bytes are R, G, B, A.
    float *depth;     // W entries, perpendicular camera distance per column
};

// Horizontal field of view, shared by every renderer that projects world space
// onto the screen. Walls and sprites must use one camera: if these drift apart
// billboards detach from the geometry they stand on, which is why this lives
// beside the screen dimensions instead of being redeclared per translation
// unit. A 14 degree divergence between two private copies was verified to pass
// the entire test suite, so the duplication is removed rather than asserted.
inline constexpr float FOV_RADIANS = 66.0f * 3.14159265358979323846f / 180.0f;

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

inline uint32_t shade(uint32_t color, float factor) {
    const auto scale = [factor](uint8_t channel) {
        return static_cast<uint8_t>(
            std::clamp(static_cast<int>(static_cast<float>(channel) * factor), 0, 255));
    };

    const auto r = static_cast<uint8_t>(color & 0xffu);
    const auto g = static_cast<uint8_t>((color >> 8) & 0xffu);
    const auto b = static_cast<uint8_t>((color >> 16) & 0xffu);
    const auto a = static_cast<uint8_t>((color >> 24) & 0xffu);
    return rgba(scale(r), scale(g), scale(b), a);
}

void render_frame(const GameState &gs, Framebuffer &fb);

} // namespace eh
