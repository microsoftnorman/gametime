#include "core/framebuffer.h"
#include "core/sprites.h"
#include "core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
