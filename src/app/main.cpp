#include "core/audio.h"
#include "core/framebuffer.h"
#include "core/input.h"
#include "core/state.h"
#include "core/textures.h"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr double TICK_SECONDS = 1.0 / static_cast<double>(eh::TICKS_PER_SECOND);
constexpr int MAX_TICKS_PER_FRAME = 5;

int axis(bool positive, bool negative) {
    return static_cast<int>(positive) - static_cast<int>(negative);
}

void set_button(eh::InputFrame &input, eh::InputFrame::Button button) {
    input.buttons = static_cast<uint8_t>(input.buttons | static_cast<uint8_t>(button));
}

void capture_mouse(bool &captured) {
    DisableCursor();
    captured = true;
}

void release_mouse(bool &captured) {
    EnableCursor();
    captured = false;
}

float integer_scale(int screen_width, int screen_height) {
    const float available =
        std::min(static_cast<float>(screen_width) / static_cast<float>(eh::Framebuffer::W),
                 static_cast<float>(screen_height) / static_cast<float>(eh::Framebuffer::H));
    return available >= 1.0f ? std::floor(available) : available;
}

} // namespace

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "EGG HUNT");
    SetExitKey(KEY_NULL);
    SetTargetFPS(eh::TICKS_PER_SECOND);

    std::vector<uint32_t> pixels(static_cast<std::size_t>(eh::Framebuffer::W) *
                                 static_cast<std::size_t>(eh::Framebuffer::H));
    std::array<float, eh::Framebuffer::W> depth{};
    eh::Framebuffer framebuffer{pixels.data(), depth.data()};

    Image image{pixels.data(), eh::Framebuffer::W, eh::Framebuffer::H, 1,
                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    eh::init_textures();
    eh::init_audio_bank();

    eh::GameState game;
    bool mouse_captured = false;
    bool quit_requested = false;
    double accumulator = 0.0;
    int pending_mouse_dx = 0;
    uint8_t pending_edge_buttons = 0;

    while (!quit_requested && !WindowShouldClose()) {
        const bool focused = IsWindowFocused();
        if (!focused && mouse_captured) {
            release_mouse(mouse_captured);
            pending_mouse_dx = 0;
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            if (mouse_captured) {
                release_mouse(mouse_captured);
                pending_mouse_dx = 0;
            } else {
                quit_requested = true;
            }
        }

        bool recaptured_this_frame = false;
        if (game.screen == eh::Screen::Playing && focused && !mouse_captured &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            capture_mouse(mouse_captured);
            pending_mouse_dx = 0;
            recaptured_this_frame = true;
        }

        eh::InputFrame frame_input;
        frame_input.move_x = static_cast<int8_t>(axis(IsKeyDown(KEY_D), IsKeyDown(KEY_A)));
        frame_input.move_y = static_cast<int8_t>(axis(IsKeyDown(KEY_W), IsKeyDown(KEY_S)));
        frame_input.turn = static_cast<int8_t>(axis(IsKeyDown(KEY_RIGHT), IsKeyDown(KEY_LEFT)));

        if (mouse_captured) {
            const Vector2 mouse_delta = GetMouseDelta();
            pending_mouse_dx += static_cast<int>(std::lround(mouse_delta.x));
            pending_mouse_dx = std::clamp(pending_mouse_dx, static_cast<int>(INT16_MIN),
                                          static_cast<int>(INT16_MAX));
        }

        if ((mouse_captured && !recaptured_this_frame && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ||
            IsKeyPressed(KEY_LEFT_CONTROL) || IsKeyPressed(KEY_RIGHT_CONTROL)) {
            pending_edge_buttons = static_cast<uint8_t>(pending_edge_buttons |
                                                        static_cast<uint8_t>(eh::InputFrame::Fire));
        }
        if (IsKeyPressed(KEY_R)) {
            pending_edge_buttons = static_cast<uint8_t>(
                pending_edge_buttons | static_cast<uint8_t>(eh::InputFrame::Restart));
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            pending_edge_buttons = static_cast<uint8_t>(
                pending_edge_buttons | static_cast<uint8_t>(eh::InputFrame::Start));
        }
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            set_button(frame_input, eh::InputFrame::Sprint);
        }

        accumulator = std::min(accumulator + static_cast<double>(GetFrameTime()),
                               TICK_SECONDS * static_cast<double>(MAX_TICKS_PER_FRAME));

        int ticks_run = 0;
        while (accumulator >= TICK_SECONDS && ticks_run < MAX_TICKS_PER_FRAME) {
            eh::InputFrame tick_input = frame_input;
            tick_input.mouse_dx = static_cast<int16_t>(pending_mouse_dx);
            tick_input.buttons = static_cast<uint8_t>(tick_input.buttons | pending_edge_buttons);

            const eh::Screen previous_screen = game.screen;
            eh::tick(game, tick_input);

            pending_mouse_dx = 0;
            pending_edge_buttons = 0;
            accumulator -= TICK_SECONDS;
            ++ticks_run;

            if (previous_screen != eh::Screen::Playing && game.screen == eh::Screen::Playing &&
                focused && !mouse_captured) {
                capture_mouse(mouse_captured);
            } else if (previous_screen == eh::Screen::Playing &&
                       game.screen != eh::Screen::Playing && mouse_captured) {
                release_mouse(mouse_captured);
            }
        }

        eh::render_frame(game, framebuffer);
        UpdateTexture(texture, pixels.data());

        const int screen_width = GetScreenWidth();
        const int screen_height = GetScreenHeight();
        const float scale = integer_scale(screen_width, screen_height);
        const float draw_width = static_cast<float>(eh::Framebuffer::W) * scale;
        const float draw_height = static_cast<float>(eh::Framebuffer::H) * scale;
        const Rectangle source{0.0f, 0.0f, static_cast<float>(eh::Framebuffer::W),
                               static_cast<float>(eh::Framebuffer::H)};
        const Rectangle destination{(static_cast<float>(screen_width) - draw_width) * 0.5f,
                                    (static_cast<float>(screen_height) - draw_height) * 0.5f,
                                    draw_width, draw_height};

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(texture, source, destination, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        EndDrawing();
    }

    if (mouse_captured) {
        release_mouse(mouse_captured);
    }
    UnloadTexture(texture);
    CloseWindow();
    return 0;
}
