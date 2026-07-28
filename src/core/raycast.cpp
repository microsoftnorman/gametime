#include "core/raycast.h"

#include "core/framebuffer.h"

namespace eh {

void render_walls(const GameState &, Framebuffer &fb) {
    const uint32_t ceiling_top = rgba(20, 28, 48);
    const uint32_t ceiling_horizon = rgba(72, 88, 112);
    const uint32_t floor_horizon = rgba(92, 68, 48);
    const uint32_t floor_bottom = rgba(36, 24, 20);

    for (int y = 0; y < Framebuffer::H; ++y) {
        const bool ceiling = y < Framebuffer::H / 2;
        const float t = ceiling ? static_cast<float>(y) / static_cast<float>(Framebuffer::H / 2 - 1)
                                : static_cast<float>(y - Framebuffer::H / 2) /
                                      static_cast<float>(Framebuffer::H / 2 - 1);
        const uint32_t from = ceiling ? ceiling_top : floor_horizon;
        const uint32_t to = ceiling ? ceiling_horizon : floor_bottom;

        const auto blend = [from, to, t](int shift) {
            const float a = static_cast<float>((from >> shift) & 0xffu);
            const float b = static_cast<float>((to >> shift) & 0xffu);
            return static_cast<uint8_t>(a + (b - a) * t);
        };
        const uint32_t color = rgba(blend(0), blend(8), blend(16));

        for (int x = 0; x < Framebuffer::W; ++x) {
            fb.pixels[y * Framebuffer::W + x] = color;
        }
    }

    for (int x = 0; x < Framebuffer::W; ++x) {
        fb.depth[x] = 1.0e9f;
    }
}

} // namespace eh
