#include "core/hud.h"

#include "core/framebuffer.h"
#include "core/state.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace eh {

namespace {

using Glyph = std::array<uint8_t, 7>;

struct GlyphEntry {
    char character;
    Glyph rows;
};

constexpr GlyphEntry FONT[] = {
    {'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'B', {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
    {'C', {0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111}},
    {'D', {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}},
    {'E', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
    {'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'G', {0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110}},
    {'H', {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'I', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111}},
    {'J', {0b00111, 0b00010, 0b00010, 0b00010, 0b10010, 0b10010, 0b01100}},
    {'K', {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},
    {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'M', {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
    {'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}},
    {'O', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'Q', {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}},
    {'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'U', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'V', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    {'W', {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}},
    {'X', {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}},
    {'Y', {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'Z', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}},
    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110}},
    {'6', {0b01110, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b01110}},
    {' ', {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}},
    {':', {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000}},
    {'/', {0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b00000, 0b00000}},
    {'!', {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100}},
    {'.', {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100}},
    {'-', {0b00000, 0b00000, 0b00000, 0b01110, 0b00000, 0b00000, 0b00000}},
};

constexpr Glyph FALLBACK = {
    0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100,
};

const uint32_t INK = rgba(247, 244, 229);
const uint32_t MUTED_INK = rgba(174, 181, 183);
const uint32_t SHADOW = rgba(6, 8, 10);
const uint32_t HUD_LINE = rgba(111, 101, 82);
const uint32_t CARROT = rgba(255, 145, 41);
const uint32_t EGG_YELLOW = rgba(255, 219, 91);
const uint32_t HEALTH_RED = rgba(214, 47, 56);
const uint32_t ALERT_RED = rgba(255, 101, 88);
const uint32_t BAR_EMPTY = rgba(68, 27, 31);

const Glyph &glyph_for(char character) {
    unsigned char code = static_cast<unsigned char>(character);
    if (code >= static_cast<unsigned char>('a') && code <= static_cast<unsigned char>('z')) {
        code = static_cast<unsigned char>(code - static_cast<unsigned char>('a') +
                                          static_cast<unsigned char>('A'));
    }

    for (const GlyphEntry &entry : FONT) {
        if (static_cast<unsigned char>(entry.character) == code) {
            return entry.rows;
        }
    }
    return FALLBACK;
}

void fill_rect(Framebuffer &fb, int x, int y, int width, int height, uint32_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(Framebuffer::W, x + width);
    const int bottom = std::min(Framebuffer::H, y + height);
    if (left >= right || top >= bottom) {
        return;
    }

    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            fb.pixels[py * Framebuffer::W + px] = color;
        }
    }
}

void blend_pixel(uint32_t &destination, uint32_t source) {
    const uint32_t alpha = (source >> 24) & 0xffu;
    if (alpha == 0) {
        return;
    }
    if (alpha == 255) {
        destination = source;
        return;
    }

    const uint32_t inverse = 255u - alpha;
    const auto channel = [alpha, inverse](uint32_t src, uint32_t dst) {
        return static_cast<uint8_t>((src * alpha + dst * inverse + 127u) / 255u);
    };

    const uint8_t red = channel(source & 0xffu, destination & 0xffu);
    const uint8_t green = channel((source >> 8) & 0xffu, (destination >> 8) & 0xffu);
    const uint8_t blue = channel((source >> 16) & 0xffu, (destination >> 16) & 0xffu);
    const uint8_t destination_alpha = static_cast<uint8_t>((destination >> 24) & 0xffu);
    const uint8_t output_alpha =
        static_cast<uint8_t>(alpha + (destination_alpha * inverse + 127u) / 255u);
    destination = rgba(red, green, blue, output_alpha);
}

void blend_rect(Framebuffer &fb, int x, int y, int width, int height, uint32_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(Framebuffer::W, x + width);
    const int bottom = std::min(Framebuffer::H, y + height);
    if (left >= right || top >= bottom) {
        return;
    }

    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            blend_pixel(fb.pixels[py * Framebuffer::W + px], color);
        }
    }
}

void draw_border(Framebuffer &fb, int x, int y, int width, int height, uint32_t color) {
    fill_rect(fb, x, y, width, 1, color);
    fill_rect(fb, x, y + height - 1, width, 1, color);
    fill_rect(fb, x, y, 1, height, color);
    fill_rect(fb, x + width - 1, y, 1, height, color);
}

void draw_glyph(Framebuffer &fb, int x, int y, const Glyph &glyph, int scale, uint32_t color) {
    for (int row = 0; row < static_cast<int>(glyph.size()); ++row) {
        for (int column = 0; column < 5; ++column) {
            const uint8_t mask = static_cast<uint8_t>(1u << (4 - column));
            if ((glyph[static_cast<std::size_t>(row)] & mask) != 0) {
                fill_rect(fb, x + column * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

int text_width(std::string_view text, int scale) {
    if (text.empty()) {
        return 0;
    }
    return (static_cast<int>(text.size()) * 6 - 1) * std::max(1, scale);
}

} // namespace

namespace hud_detail {

void draw_text(Framebuffer &fb, int x, int y, std::string_view text, int scale, uint32_t color) {
    scale = std::max(1, scale);
    int cursor = x;
    for (const char character : text) {
        const Glyph &glyph = glyph_for(character);
        draw_glyph(fb, cursor + 1, y + 1, glyph, scale, SHADOW);
        draw_glyph(fb, cursor, y, glyph, scale, color);
        cursor += 6 * scale;
    }
}

} // namespace hud_detail

namespace {

void draw_centered_text(Framebuffer &fb, int y, std::string_view text, int scale, uint32_t color) {
    const int x = (Framebuffer::W - text_width(text, scale)) / 2;
    hud_detail::draw_text(fb, x, y, text, scale, color);
}

bool warning_flash(uint32_t tick) { return ((tick / 8u) & 1u) == 0u; }

void draw_damage_vignette(const GameState &gs, Framebuffer &fb) {
    if (gs.player.hurt_flash == 0) {
        return;
    }

    constexpr int EDGE_WIDTH = 44;
    const int peak_alpha = std::min(180, static_cast<int>(gs.player.hurt_flash) * 15);
    for (int y = 0; y < Framebuffer::H; ++y) {
        for (int x = 0; x < Framebuffer::W; ++x) {
            const int distance =
                std::min(std::min(x, Framebuffer::W - 1 - x), std::min(y, Framebuffer::H - 1 - y));
            if (distance >= EDGE_WIDTH) {
                continue;
            }

            const int alpha = peak_alpha * (EDGE_WIDTH - distance) / EDGE_WIDTH;
            if (alpha > 0) {
                blend_pixel(fb.pixels[y * Framebuffer::W + x],
                            rgba(126, 3, 14, static_cast<uint8_t>(alpha)));
            }
        }
    }
}

void draw_crosshair(const GameState &gs, Framebuffer &fb) {
    constexpr int CENTER_X = Framebuffer::W / 2;
    constexpr int CENTER_Y = Framebuffer::H / 2;
    const uint32_t color = gs.muzzle_flash > 0 ? EGG_YELLOW : INK;
    fill_rect(fb, CENTER_X - 2, CENTER_Y - 2, 5, 5, SHADOW);
    fill_rect(fb, CENTER_X - 1, CENTER_Y - 1, 3, 3, color);
}

void shake_offset(const GameState &gs, int &x, int &y) {
    const int amount = std::min(3, static_cast<int>(gs.shake));
    if (amount == 0) {
        x = 0;
        y = 0;
        return;
    }

    const uint32_t span = static_cast<uint32_t>(amount * 2 + 1);
    x = static_cast<int>((gs.tick * 17u + 1u) % span) - amount;
    y = static_cast<int>((gs.tick * 29u + 3u) % span) - amount;
}

int total_eggs(const GameState &gs) {
    const int level_total = static_cast<int>(gs.level.eggs.size());
    return std::max(5, std::max(level_total, gs.eggs_remaining));
}

void draw_objective_prompt(const GameState &gs, Framebuffer &fb, int x_offset, int y_offset) {
    const uint32_t prompt_color = warning_flash(gs.tick) ? EGG_YELLOW : CARROT;
    constexpr int BANNER_X = 132;
    constexpr int BANNER_Y = 258;
    constexpr int BANNER_W = 376;
    constexpr int BANNER_H = 36;
    blend_rect(fb, BANNER_X, BANNER_Y, BANNER_W, BANNER_H, rgba(5, 8, 11, 230));
    draw_border(fb, BANNER_X, BANNER_Y, BANNER_W, BANNER_H, prompt_color);
    draw_centered_text(fb, BANNER_Y + 7, "GET TO THE BASKET!", 3, prompt_color);

    hud_detail::draw_text(fb, 332 + x_offset, 309 + y_offset, "OBJECTIVE", 1, MUTED_INK);
    hud_detail::draw_text(fb, 332 + x_offset, 327 + y_offset, "GET TO THE BASKET!", 2,
                          prompt_color);
}

void render_playing_hud(const GameState &gs, Framebuffer &fb) {
    draw_damage_vignette(gs, fb);
    draw_crosshair(gs, fb);

    constexpr int HUD_TOP = 302;
    blend_rect(fb, 0, HUD_TOP, Framebuffer::W, Framebuffer::H - HUD_TOP, rgba(6, 9, 12, 232));
    fill_rect(fb, 0, HUD_TOP, Framebuffer::W, 2, CARROT);

    int x_offset = 0;
    int y_offset = 0;
    shake_offset(gs, x_offset, y_offset);

    fill_rect(fb, 207 + x_offset, 311 + y_offset, 1, 37, HUD_LINE);
    fill_rect(fb, 312 + x_offset, 311 + y_offset, 1, 37, HUD_LINE);

    const int health = std::clamp(static_cast<int>(gs.player.health), 0, 100);
    const bool low_health = gs.player.health < 25;
    const bool flash = warning_flash(gs.tick);
    const uint32_t health_color = low_health && flash ? ALERT_RED : HEALTH_RED;
    const uint32_t health_number_color = low_health ? (flash ? EGG_YELLOW : ALERT_RED) : INK;

    hud_detail::draw_text(fb, 16 + x_offset, 309 + y_offset, "HEALTH", 1, MUTED_INK);
    hud_detail::draw_text(fb, 16 + x_offset, 328 + y_offset, std::to_string(health), 2,
                          health_number_color);

    constexpr int HEALTH_BAR_WIDTH = 128;
    constexpr int HEALTH_BAR_HEIGHT = 16;
    const int bar_x = 68 + x_offset;
    const int bar_y = 327 + y_offset;
    fill_rect(fb, bar_x, bar_y, HEALTH_BAR_WIDTH, HEALTH_BAR_HEIGHT, BAR_EMPTY);
    draw_border(fb, bar_x, bar_y, HEALTH_BAR_WIDTH, HEALTH_BAR_HEIGHT, HUD_LINE);
    const int health_fill = (HEALTH_BAR_WIDTH - 4) * health / 100;
    fill_rect(fb, bar_x + 2, bar_y + 2, health_fill, HEALTH_BAR_HEIGHT - 4, health_color);

    const int ammo = std::max(0, static_cast<int>(gs.player.ammo));
    const bool empty_ammo = ammo == 0;
    const uint32_t ammo_color = empty_ammo ? (flash ? EGG_YELLOW : ALERT_RED) : INK;
    hud_detail::draw_text(fb, 226 + x_offset, 309 + y_offset, "AMMO", 1, MUTED_INK);
    hud_detail::draw_text(fb, 226 + x_offset, 328 + y_offset, std::to_string(ammo), 2, ammo_color);

    if (gs.eggs_remaining == 0) {
        draw_objective_prompt(gs, fb, x_offset, y_offset);
    } else {
        const int remaining = std::max(0, gs.eggs_remaining);
        const std::string eggs =
            "EGGS: " + std::to_string(remaining) + "/" + std::to_string(total_eggs(gs));
        hud_detail::draw_text(fb, 332 + x_offset, 323 + y_offset, eggs, 2, EGG_YELLOW);
    }
}

void render_title_screen(Framebuffer &fb) {
    blend_rect(fb, 0, 0, Framebuffer::W, Framebuffer::H, rgba(3, 7, 10, 210));
    fill_rect(fb, 0, 0, Framebuffer::W, 4, CARROT);

    draw_centered_text(fb, 38, "EGG HUNT", 7, EGG_YELLOW);
    fill_rect(fb, 108, 101, 424, 2, CARROT);
    draw_centered_text(fb, 116, "YOU ARE THE BUNNY. CRACK FIVE HOSTILE EGGS.", 2, INK);

    draw_centered_text(fb, 164, "CONTROLS", 1, CARROT);
    draw_centered_text(fb, 184, "WASD MOVE  /  MOUSE LOOK", 2, INK);
    draw_centered_text(fb, 211, "CLICK FIRE  /  ESC RELEASE MOUSE", 2, INK);

    constexpr int PROMPT_X = 108;
    constexpr int PROMPT_Y = 275;
    constexpr int PROMPT_W = 424;
    constexpr int PROMPT_H = 49;
    blend_rect(fb, PROMPT_X, PROMPT_Y, PROMPT_W, PROMPT_H, rgba(10, 13, 16, 226));
    draw_border(fb, PROMPT_X, PROMPT_Y, PROMPT_W, PROMPT_H, HUD_LINE);
    draw_centered_text(fb, PROMPT_Y + 13, "PRESS ENTER TO START", 3, CARROT);
}

void render_end_screen(Framebuffer &fb, bool won) {
    blend_rect(fb, 0, 0, Framebuffer::W, Framebuffer::H, rgba(4, 7, 10, 218));
    const uint32_t accent = won ? EGG_YELLOW : ALERT_RED;
    fill_rect(fb, 0, 0, Framebuffer::W, 4, accent);

    if (won) {
        draw_centered_text(fb, 104, "ALL EGGS CRACKED!", 4, accent);
        draw_centered_text(fb, 164, "THE BASKET IS SAFE.", 2, INK);
    } else {
        draw_centered_text(fb, 96, "SCRAMBLED!", 6, accent);
        draw_centered_text(fb, 164, "THE EGGS WON THIS ROUND.", 2, INK);
    }

    fill_rect(fb, 142, 202, 356, 2, accent);
    constexpr int PROMPT_X = 112;
    constexpr int PROMPT_Y = 232;
    constexpr int PROMPT_W = 416;
    constexpr int PROMPT_H = 50;
    blend_rect(fb, PROMPT_X, PROMPT_Y, PROMPT_W, PROMPT_H, rgba(10, 13, 16, 226));
    draw_border(fb, PROMPT_X, PROMPT_Y, PROMPT_W, PROMPT_H, HUD_LINE);
    draw_centered_text(fb, PROMPT_Y + 14, "PRESS R TO PLAY AGAIN", 3, accent);
}

} // namespace

void render_hud(const GameState &gs, Framebuffer &fb) {
    switch (gs.screen) {
    case Screen::Title:
        render_title_screen(fb);
        break;
    case Screen::Playing:
        render_playing_hud(gs, fb);
        break;
    case Screen::Won:
        render_end_screen(fb, true);
        break;
    case Screen::Lost:
        render_end_screen(fb, false);
        break;
    }
}

} // namespace eh
