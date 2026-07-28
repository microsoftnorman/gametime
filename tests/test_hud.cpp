#include "core/framebuffer.h"
#include "core/hud.h"
#include "core/state.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eh::hud_detail {

void draw_text(Framebuffer &, int, int, std::string_view, int, uint32_t);

} // namespace eh::hud_detail

namespace {

constexpr std::size_t GUARD_SIZE = 64;
constexpr uint32_t GUARD_VALUE = 0xdeadc0deu;
constexpr uint32_t BACKGROUND = 0xff352b21u;
constexpr float DEPTH_VALUE = 12345.0f;

struct GuardedFramebuffer {
    std::vector<uint32_t> storage;
    std::vector<float> depth_storage;
    eh::Framebuffer framebuffer;

    GuardedFramebuffer()
        : storage(GUARD_SIZE + static_cast<std::size_t>(eh::Framebuffer::W) * eh::Framebuffer::H +
                      GUARD_SIZE,
                  GUARD_VALUE),
          depth_storage(GUARD_SIZE + eh::Framebuffer::W + GUARD_SIZE, DEPTH_VALUE),
          framebuffer{storage.data() + GUARD_SIZE, depth_storage.data() + GUARD_SIZE} {
        std::fill(storage.begin() + static_cast<std::ptrdiff_t>(GUARD_SIZE),
                  storage.end() - static_cast<std::ptrdiff_t>(GUARD_SIZE), BACKGROUND);
    }

    bool guards_intact() const {
        const bool leading =
            std::all_of(storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(GUARD_SIZE),
                        [](uint32_t pixel) { return pixel == GUARD_VALUE; });
        const bool trailing =
            std::all_of(storage.end() - static_cast<std::ptrdiff_t>(GUARD_SIZE), storage.end(),
                        [](uint32_t pixel) { return pixel == GUARD_VALUE; });
        const bool depth_untouched = std::all_of(depth_storage.begin(), depth_storage.end(),
                                                 [](float depth) { return depth == DEPTH_VALUE; });
        return leading && trailing && depth_untouched;
    }

    std::vector<uint32_t> image() const {
        const auto first = storage.begin() + static_cast<std::ptrdiff_t>(GUARD_SIZE);
        const auto last = storage.end() - static_cast<std::ptrdiff_t>(GUARD_SIZE);
        return {first, last};
    }

    uint32_t pixel(int x, int y) const { return framebuffer.pixels[y * eh::Framebuffer::W + x]; }
};

eh::GameState playing_state() {
    eh::GameState state;
    state.screen = eh::Screen::Playing;
    state.player.health = 100;
    state.player.ammo = 24;
    state.eggs_remaining = 3;
    return state;
}

std::vector<uint32_t> render_pixels(const eh::GameState &state) {
    GuardedFramebuffer target;
    eh::render_hud(state, target.framebuffer);
    return target.image();
}

// Length of the longest contiguous horizontal run of pixels that differ from a
// baseline image. Used to locate the health bar rather than hardcoding its
// geometry: against the zero-health render the bar fill is the only long
// contiguous run of changed pixels, since glyph strokes are a few pixels wide.
// That keeps HUD layout constants out of this file.
int longest_changed_run(const std::vector<uint32_t> &image, const std::vector<uint32_t> &baseline) {
    int longest = 0;
    for (int y = 0; y < eh::Framebuffer::H; ++y) {
        int run = 0;
        for (int x = 0; x < eh::Framebuffer::W; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * eh::Framebuffer::W + static_cast<std::size_t>(x);
            run = image[index] != baseline[index] ? run + 1 : 0;
            longest = std::max(longest, run);
        }
    }
    return longest;
}

} // namespace

TEST_CASE("hud: rendering never writes outside the framebuffer") {
    constexpr std::array SCREENS{
        eh::Screen::Title,
        eh::Screen::Playing,
        eh::Screen::Won,
        eh::Screen::Lost,
    };

    for (const eh::Screen screen : SCREENS) {
        CAPTURE(static_cast<int>(screen));
        GuardedFramebuffer target;
        eh::GameState state = playing_state();
        state.screen = screen;

        eh::render_hud(state, target.framebuffer);

        REQUIRE(target.guards_intact());
    }
}

TEST_CASE("hud: text drawing clips at every framebuffer edge") {
    const uint32_t text_color = eh::rgba(250, 240, 210);
    GuardedFramebuffer reference;
    eh::hud_detail::draw_text(reference.framebuffer, 10, 10, "A", 1, text_color);

    SECTION("negative coordinates") {
        GuardedFramebuffer clipped;
        eh::hud_detail::draw_text(clipped.framebuffer, -2, -3, "A", 1, text_color);

        std::vector<uint32_t> expected(
            static_cast<std::size_t>(eh::Framebuffer::W) * eh::Framebuffer::H, BACKGROUND);
        for (int y = 0; y <= 4; ++y) {
            for (int x = 0; x <= 3; ++x) {
                expected[static_cast<std::size_t>(y) * eh::Framebuffer::W + x] =
                    reference.pixel(x + 12, y + 13);
            }
        }

        REQUIRE(clipped.guards_intact());
        REQUIRE(clipped.image() == expected);
    }

    SECTION("bottom-right coordinates") {
        GuardedFramebuffer clipped;
        constexpr int LEFT = eh::Framebuffer::W - 3;
        constexpr int TOP = eh::Framebuffer::H - 4;
        eh::hud_detail::draw_text(clipped.framebuffer, LEFT, TOP, "A", 1, text_color);

        std::vector<uint32_t> expected(
            static_cast<std::size_t>(eh::Framebuffer::W) * eh::Framebuffer::H, BACKGROUND);
        for (int y = TOP; y < eh::Framebuffer::H; ++y) {
            for (int x = LEFT; x < eh::Framebuffer::W; ++x) {
                expected[static_cast<std::size_t>(y) * eh::Framebuffer::W + x] =
                    reference.pixel(10 + x - LEFT, 10 + y - TOP);
            }
        }

        REQUIRE(clipped.guards_intact());
        REQUIRE(clipped.image() == expected);
    }
}

TEST_CASE("hud: every screen draws a visible overlay") {
    constexpr std::array SCREENS{
        eh::Screen::Title,
        eh::Screen::Playing,
        eh::Screen::Won,
        eh::Screen::Lost,
    };

    for (const eh::Screen screen : SCREENS) {
        CAPTURE(static_cast<int>(screen));
        GuardedFramebuffer target;
        const std::vector<uint32_t> before = target.image();
        eh::GameState state = playing_state();
        state.screen = screen;

        eh::render_hud(state, target.framebuffer);

        REQUIRE(target.image() != before);
    }
}

TEST_CASE("hud: low health and empty ammo flash without affecting healthy output") {
    eh::GameState healthy = playing_state();
    const std::vector<uint32_t> healthy_phase_one = render_pixels(healthy);
    healthy.tick = 8;
    const std::vector<uint32_t> healthy_phase_two = render_pixels(healthy);
    REQUIRE(healthy_phase_one == healthy_phase_two);

    eh::GameState low_health = playing_state();
    low_health.player.health = 18;
    const std::vector<uint32_t> low_phase_one = render_pixels(low_health);
    low_health.tick = 8;
    const std::vector<uint32_t> low_phase_two = render_pixels(low_health);
    REQUIRE(low_phase_one != healthy_phase_one);
    REQUIRE(low_phase_one != low_phase_two);

    eh::GameState empty_ammo = playing_state();
    empty_ammo.player.ammo = 0;
    const std::vector<uint32_t> empty_phase_one = render_pixels(empty_ammo);
    empty_ammo.tick = 8;
    const std::vector<uint32_t> empty_phase_two = render_pixels(empty_ammo);
    REQUIRE(empty_phase_one != healthy_phase_one);
    REQUIRE(empty_phase_one != empty_phase_two);
}

// The health bar must actually deplete.
//
// Replacing its fill width with a constant -- a bar that stays full while the
// player is being killed -- passed all 70 tests. The HUD suite asserts bounds,
// clipping, presence and flash colour, which are safety and relational
// properties that hold at any fill width. Nothing tied the widget to the value
// it exists to display, so the one thing a player reads mid-fight to decide
// whether to retreat could have been frozen without a single test objecting.
TEST_CASE("hud: the health bar depletes in proportion to health") {
    eh::GameState state = playing_state();
    state.player.health = 0;
    const std::vector<uint32_t> empty_bar = render_pixels(state);

    state.player.health = 100;
    const int full = longest_changed_run(render_pixels(state), empty_bar);
    state.player.health = 50;
    const int half = longest_changed_run(render_pixels(state), empty_bar);
    state.player.health = 25;
    const int quarter = longest_changed_run(render_pixels(state), empty_bar);

    // The bar was found, and is far wider than any glyph stroke.
    REQUIRE(full > 100);

    CHECK(full > half);
    CHECK(half > quarter);
    CHECK(half * 2 == Catch::Approx(full).margin(6));
    CHECK(quarter * 4 == Catch::Approx(full).margin(8));
}

TEST_CASE("hud: unsupported font characters use a safe fallback glyph") {
    GuardedFramebuffer target;
    const std::string unsupported(1, static_cast<char>(0xff));

    REQUIRE_NOTHROW(eh::hud_detail::draw_text(target.framebuffer, -2, -2, unsupported, 2,
                                              eh::rgba(255, 255, 255)));

    const std::vector<uint32_t> image = target.image();
    REQUIRE(target.guards_intact());
    REQUIRE(std::any_of(image.begin(), image.end(),
                        [](uint32_t pixel) { return pixel != BACKGROUND; }));
}
