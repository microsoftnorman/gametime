#include "core/framebuffer.h"
#include "core/hud.h"
#include "core/raycast.h"
#include "core/sprites.h"
#include "core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// render_frame is the only renderer the game actually calls, and until this
// file existed no test called it at all. Every layer was verified in isolation
// and nothing owned the assembly, so deleting render_sprites from render_frame
// -- drawing walls, weapon and HUD but never a single enemy -- passed all 71
// tests. The whole point of the game can go missing with CI green.

namespace {

struct Target {
    std::vector<uint32_t> pixels;
    std::vector<float> depth;
    eh::Framebuffer framebuffer;

    Target()
        : pixels(static_cast<std::size_t>(eh::Framebuffer::W) * eh::Framebuffer::H, 0u),
          depth(static_cast<std::size_t>(eh::Framebuffer::W), 0.0f),
          framebuffer{pixels.data(), depth.data()} {}
};

// A scene in which every layer has something to draw: real map geometry from
// reset(), an egg placed just ahead of wherever the player actually spawns, and
// a live muzzle flash so the weapon is in its most distinct state.
eh::GameState scene() {
    eh::GameState state;
    eh::reset(state, 0x5eed1234u);
    state.screen = eh::Screen::Playing;
    state.muzzle_flash = 3;

    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    const float angle = static_cast<float>(state.player.angle) * TWO_PI / 65536.0f;
    const float x = eh::fx_to_float(state.player.x) + std::cos(angle) * 1.2f;
    const float y = eh::fx_to_float(state.player.y) + std::sin(angle) * 1.2f;

    eh::Entity egg;
    egg.id = 9001;
    egg.type = eh::EntityType::Egg;
    egg.x = eh::fx_from_float(x);
    egg.y = eh::fx_from_float(y);
    egg.alive = true;
    state.entities.push_back(egg);
    return state;
}

} // namespace

TEST_CASE("render: the frame draws every layer, with the HUD last") {
    const eh::GameState state = scene();

    Target walls;
    eh::render_walls(state, walls.framebuffer);

    Target with_sprites;
    eh::render_walls(state, with_sprites.framebuffer);
    eh::render_sprites(state, with_sprites.framebuffer);

    Target with_weapon;
    eh::render_walls(state, with_weapon.framebuffer);
    eh::render_sprites(state, with_weapon.framebuffer);
    eh::render_weapon(state, with_weapon.framebuffer);

    Target composed;
    eh::render_walls(state, composed.framebuffer);
    eh::render_sprites(state, composed.framebuffer);
    eh::render_weapon(state, composed.framebuffer);
    eh::render_hud(state, composed.framebuffer);

    // Non-vacuity. Equality against a hand-built composition proves nothing if
    // some layer contributes no pixels, so each one must visibly change the
    // frame in this scene before its presence is worth asserting.
    CHECK(walls.pixels != with_sprites.pixels);
    CHECK(with_sprites.pixels != with_weapon.pixels);
    CHECK(with_weapon.pixels != composed.pixels);

    // The production orchestration must equal that composition exactly. This
    // is what fails if a layer is dropped, duplicated, or reordered.
    Target produced;
    eh::render_frame(state, produced.framebuffer);
    REQUIRE(produced.pixels == composed.pixels);
}

TEST_CASE("render: HUD-last ordering is observable, not an unenforced convention") {
    const eh::GameState state = scene();

    Target hud_last;
    eh::render_walls(state, hud_last.framebuffer);
    eh::render_sprites(state, hud_last.framebuffer);
    eh::render_weapon(state, hud_last.framebuffer);
    eh::render_hud(state, hud_last.framebuffer);

    Target hud_before_weapon;
    eh::render_walls(state, hud_before_weapon.framebuffer);
    eh::render_sprites(state, hud_before_weapon.framebuffer);
    eh::render_hud(state, hud_before_weapon.framebuffer);
    eh::render_weapon(state, hud_before_weapon.framebuffer);

    // The HUD session asked that render_hud stay last. That only means
    // something if the weapon would otherwise cover it, so prove the two
    // orders differ before asserting which one production uses.
    REQUIRE(hud_last.pixels != hud_before_weapon.pixels);

    Target produced;
    eh::render_frame(state, produced.framebuffer);
    CHECK(produced.pixels == hud_last.pixels);
    CHECK(produced.pixels != hud_before_weapon.pixels);
}
