#include "core/framebuffer.h"
#include "core/sprites.h"
#include "core/state.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

constexpr uint32_t BACKGROUND = 0xff171311u;
constexpr uint32_t GUARD = 0x5aa55aa5u;

struct GuardedFramebuffer {
    static constexpr std::size_t GUARD_SIZE = 128;
    static constexpr std::size_t PIXEL_COUNT =
        static_cast<std::size_t>(eh::Framebuffer::W) * eh::Framebuffer::H;

    std::vector<uint32_t> storage;
    std::array<float, eh::Framebuffer::W> depth;
    eh::Framebuffer framebuffer;

    explicit GuardedFramebuffer(float wall_depth = 1000.0f)
        : storage(PIXEL_COUNT + GUARD_SIZE * 2, GUARD), depth{},
          framebuffer{storage.data() + GUARD_SIZE, depth.data()} {
        std::fill(storage.begin() + static_cast<std::ptrdiff_t>(GUARD_SIZE),
                  storage.end() - static_cast<std::ptrdiff_t>(GUARD_SIZE), BACKGROUND);
        depth.fill(wall_depth);
    }

    bool guards_intact() const {
        return std::all_of(storage.begin(),
                           storage.begin() + static_cast<std::ptrdiff_t>(GUARD_SIZE),
                           [](uint32_t value) { return value == GUARD; }) &&
               std::all_of(storage.end() - static_cast<std::ptrdiff_t>(GUARD_SIZE), storage.end(),
                           [](uint32_t value) { return value == GUARD; });
    }

    bool pixels_unchanged() const {
        return std::all_of(storage.begin() + static_cast<std::ptrdiff_t>(GUARD_SIZE),
                           storage.end() - static_cast<std::ptrdiff_t>(GUARD_SIZE),
                           [](uint32_t value) { return value == BACKGROUND; });
    }

    std::vector<uint32_t> pixels() const {
        return {storage.begin() + static_cast<std::ptrdiff_t>(GUARD_SIZE),
                storage.end() - static_cast<std::ptrdiff_t>(GUARD_SIZE)};
    }
};

eh::GameState state_facing_east() {
    eh::GameState state;
    state.screen = eh::Screen::Playing;
    state.player.x = eh::fx_from_int(0);
    state.player.y = eh::fx_from_int(0);
    state.player.angle = eh::angle_from_deg(0.0);
    state.eggs_remaining = 1;
    return state;
}

eh::Entity entity_at(uint32_t id, eh::EntityType type, float x, float y) {
    eh::Entity entity;
    entity.id = id;
    entity.type = type;
    entity.x = eh::fx_from_float(x);
    entity.y = eh::fx_from_float(y);
    entity.alive = true;
    return entity;
}

bool is_flash_pixel(uint32_t color) {
    const uint32_t red = color & 0xffu;
    const uint32_t green = (color >> 8) & 0xffu;
    const uint32_t blue = (color >> 16) & 0xffu;
    return red > 245u && green > 195u && blue < 120u;
}

struct PixelBounds {
    int left = eh::Framebuffer::W;
    int top = eh::Framebuffer::H;
    int right = -1;
    int bottom = -1;

    bool valid() const { return right >= left && bottom >= top; }
};

PixelBounds modified_bounds(const GuardedFramebuffer &buffer) {
    PixelBounds bounds;
    for (int y = 0; y < eh::Framebuffer::H; ++y) {
        for (int x = 0; x < eh::Framebuffer::W; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * eh::Framebuffer::W + static_cast<std::size_t>(x);
            if (buffer.framebuffer.pixels[index] == BACKGROUND) {
                continue;
            }

            bounds.left = std::min(bounds.left, x);
            bounds.top = std::min(bounds.top, y);
            bounds.right = std::max(bounds.right, x);
            bounds.bottom = std::max(bounds.bottom, y);
        }
    }
    return bounds;
}

} // namespace

TEST_CASE("sprites: close billboard writes only inside the framebuffer") {
    eh::GameState state = state_facing_east();
    state.entities.push_back(entity_at(1, eh::EntityType::Egg, 0.06f, 0.0f));
    GuardedFramebuffer buffer;

    eh::render_sprites(state, buffer.framebuffer);

    REQUIRE(buffer.guards_intact());
}

TEST_CASE("sprites: wall depth occludes a billboard") {
    eh::GameState state = state_facing_east();
    state.entities.push_back(entity_at(1, eh::EntityType::Egg, 2.0f, 0.0f));
    GuardedFramebuffer buffer(1.0f);

    eh::render_sprites(state, buffer.framebuffer);

    REQUIRE(buffer.pixels_unchanged());
    REQUIRE(buffer.guards_intact());
}

TEST_CASE("sprites: billboard in open space modifies pixels") {
    eh::GameState state = state_facing_east();
    state.entities.push_back(entity_at(1, eh::EntityType::Egg, 2.0f, 0.0f));
    GuardedFramebuffer buffer;

    eh::render_sprites(state, buffer.framebuffer);

    REQUIRE_FALSE(buffer.pixels_unchanged());
    REQUIRE(buffer.guards_intact());
}

// A wall and a billboard must project through the same camera. FOV_RADIANS was
// declared privately in both raycast.cpp and sprites.cpp; setting the sprite
// copy to 75 degrees while the wall copy stayed at 66 was verified to pass all
// 68 tests, which would have put every enemy at the wrong screen position
// relative to the geometry it stands on. The constant is now shared, and this
// pins sprite projection to it so a private copy cannot quietly return.
//
// An entity 4.0 tiles ahead and 1.0 tile to the side subtends
// atan(1/4) = 14.04 degrees. With a 66 degree horizontal FOV across 640
// columns that lands 320 / tan(33 deg) * (1/4) = 123.2 columns from the centre
// line; at 75 degrees it would be 104.3. The sign of the lateral axis is left
// unasserted so this tests the camera, not a handedness convention.
TEST_CASE("sprites: billboard projection matches the shared camera FOV") {
    eh::GameState state = state_facing_east();
    state.entities.push_back(entity_at(1, eh::EntityType::Egg, 4.0f, 1.0f));
    GuardedFramebuffer buffer;

    eh::render_sprites(state, buffer.framebuffer);

    const PixelBounds bounds = modified_bounds(buffer);
    REQUIRE(bounds.valid());
    REQUIRE(buffer.guards_intact());

    const float centre = 0.5f * static_cast<float>(bounds.left + bounds.right);
    const float expected_offset = (static_cast<float>(eh::Framebuffer::W) * 0.5f) /
                                  std::tan(eh::FOV_RADIANS * 0.5f) * (1.0f / 4.0f);
    const float actual_offset = std::fabs(centre - static_cast<float>(eh::Framebuffer::W) * 0.5f);

    CHECK(expected_offset == Catch::Approx(123.2f).margin(0.5f));
    CHECK(actual_offset == Catch::Approx(expected_offset).margin(4.0f));
}

TEST_CASE("sprites: billboard behind the camera is rejected") {
    eh::GameState state = state_facing_east();
    state.entities.push_back(entity_at(1, eh::EntityType::Egg, -1.0f, 0.0f));
    GuardedFramebuffer buffer;

    eh::render_sprites(state, buffer.framebuffer);

    REQUIRE(buffer.pixels_unchanged());
    REQUIRE(buffer.guards_intact());
}

TEST_CASE("sprites: equal camera depth uses stable entity id ordering") {
    eh::GameState first = state_facing_east();
    first.entities.push_back(entity_at(20, eh::EntityType::Egg, 2.0f, 0.0f));
    first.entities.push_back(entity_at(10, eh::EntityType::Basket, 2.0f, 0.0f));

    eh::GameState second = state_facing_east();
    second.entities.push_back(entity_at(10, eh::EntityType::Basket, 2.0f, 0.0f));
    second.entities.push_back(entity_at(20, eh::EntityType::Egg, 2.0f, 0.0f));

    GuardedFramebuffer first_buffer;
    GuardedFramebuffer second_buffer;
    eh::render_sprites(first, first_buffer.framebuffer);
    eh::render_sprites(second, second_buffer.framebuffer);

    REQUIRE(first_buffer.pixels() == second_buffer.pixels());
    REQUIRE(first_buffer.guards_intact());
    REQUIRE(second_buffer.guards_intact());
}

TEST_CASE("sprites: weapon bob is continuous across normalized phase wrap") {
    eh::GameState start = state_facing_east();
    start.player.bob = 0;
    eh::GameState end = start;
    end.player.bob = eh::FX_ONE - 1;

    GuardedFramebuffer start_buffer;
    GuardedFramebuffer end_buffer;
    eh::render_weapon(start, start_buffer.framebuffer);
    eh::render_weapon(end, end_buffer.framebuffer);

    const PixelBounds start_bounds = modified_bounds(start_buffer);
    const PixelBounds end_bounds = modified_bounds(end_buffer);
    REQUIRE(start_bounds.valid());
    REQUIRE(end_bounds.valid());
    REQUIRE(std::abs(start_bounds.left - end_bounds.left) <= 1);
    REQUIRE(std::abs(start_bounds.top - end_bounds.top) <= 1);
}

TEST_CASE("sprites: weapon bob traverses both horizontal extremes in one normalized cycle") {
    eh::GameState state = state_facing_east();
    GuardedFramebuffer rest_buffer;
    eh::render_weapon(state, rest_buffer.framebuffer);
    const PixelBounds rest_bounds = modified_bounds(rest_buffer);
    REQUIRE(rest_bounds.valid());

    int minimum_offset = 0;
    int maximum_offset = 0;
    for (int step = 0; step < 12; ++step) {
        state.player.bob = static_cast<eh::fx>((static_cast<int64_t>(eh::FX_ONE) * step) / 12);
        GuardedFramebuffer buffer;
        eh::render_weapon(state, buffer.framebuffer);
        const PixelBounds bounds = modified_bounds(buffer);
        REQUIRE(bounds.valid());
        minimum_offset = std::min(minimum_offset, bounds.left - rest_bounds.left);
        maximum_offset = std::max(maximum_offset, bounds.left - rest_bounds.left);
    }

    REQUIRE(minimum_offset <= -4);
    REQUIRE(maximum_offset >= 4);
}

TEST_CASE("sprites: weapon stays in bounds and reacts to muzzle flash") {
    eh::GameState idle = state_facing_east();
    idle.player.bob = eh::fx_from_float(1.25f);
    eh::GameState firing = idle;
    firing.muzzle_flash = 4;

    GuardedFramebuffer idle_buffer;
    GuardedFramebuffer firing_buffer;
    eh::render_weapon(idle, idle_buffer.framebuffer);
    eh::render_weapon(firing, firing_buffer.framebuffer);

    const std::vector<uint32_t> idle_pixels = idle_buffer.pixels();
    const std::vector<uint32_t> firing_pixels = firing_buffer.pixels();
    const auto idle_flash_pixels =
        std::count_if(idle_pixels.begin(), idle_pixels.end(), is_flash_pixel);
    const auto firing_flash_pixels =
        std::count_if(firing_pixels.begin(), firing_pixels.end(), is_flash_pixel);

    REQUIRE_FALSE(idle_buffer.pixels_unchanged());
    REQUIRE(idle_pixels != firing_pixels);
    REQUIRE(firing_flash_pixels > idle_flash_pixels);
    REQUIRE(idle_buffer.guards_intact());
    REQUIRE(firing_buffer.guards_intact());
}
