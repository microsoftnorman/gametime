#include "core/framebuffer.h"
#include "core/hud.h"
#include "core/state.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

TEST_CASE("hud: taking damage reddens the screen edges and fades as the flash expires") {
    // hurt_flash is set by entities_tick and decremented by player_tick, and finding #4
    // pinned its nine-tick duration. Nothing asserted it was ever drawn: making
    // draw_damage_vignette return unconditionally left every test green.
    eh::GameState calm = playing_state();
    eh::GameState hurt = playing_state();
    hurt.player.hurt_flash = 9;

    const std::vector<uint32_t> calm_pixels = render_pixels(calm);
    const std::vector<uint32_t> hurt_pixels = render_pixels(hurt);
    REQUIRE(calm_pixels != hurt_pixels);

    // The vignette is an edge effect: corners redden, the centre of the view does not.
    const auto at = [](const std::vector<uint32_t> &pixels, int x, int y) {
        return pixels[static_cast<std::size_t>(y) * eh::Framebuffer::W +
                      static_cast<std::size_t>(x)];
    };
    const auto red_of = [](uint32_t color) { return static_cast<int>(color & 0xffu); };

    REQUIRE(red_of(at(hurt_pixels, 0, 0)) > red_of(at(calm_pixels, 0, 0)));
    REQUIRE(red_of(at(hurt_pixels, eh::Framebuffer::W - 1, eh::Framebuffer::H - 1)) >
            red_of(at(calm_pixels, eh::Framebuffer::W - 1, eh::Framebuffer::H - 1)));
    REQUIRE(at(hurt_pixels, eh::Framebuffer::W / 2, eh::Framebuffer::H / 2) ==
            at(calm_pixels, eh::Framebuffer::W / 2, eh::Framebuffer::H / 2));

    // Intensity tracks the remaining ticks, so the flash visibly fades out.
    eh::GameState fading = playing_state();
    fading.player.hurt_flash = 2;
    const std::vector<uint32_t> fading_pixels = render_pixels(fading);
    REQUIRE(red_of(at(fading_pixels, 0, 0)) > red_of(at(calm_pixels, 0, 0)));
    REQUIRE(red_of(at(fading_pixels, 0, 0)) < red_of(at(hurt_pixels, 0, 0)));
}

TEST_CASE("hud: firing lights up the crosshair") {
    // muzzle_flash drives both the weapon sprite and the crosshair colour. Only the
    // weapon side was asserted, so the crosshair could stop responding unnoticed.
    eh::GameState idle = playing_state();
    eh::GameState firing = playing_state();
    firing.muzzle_flash = 4;

    GuardedFramebuffer idle_target;
    GuardedFramebuffer firing_target;
    eh::render_hud(idle, idle_target.framebuffer);
    eh::render_hud(firing, firing_target.framebuffer);
    REQUIRE(idle_target.guards_intact());
    REQUIRE(firing_target.guards_intact());

    const int center_x = eh::Framebuffer::W / 2;
    const int center_y = eh::Framebuffer::H / 2;
    REQUIRE(idle_target.pixel(center_x, center_y) != firing_target.pixel(center_x, center_y));

    // The lit crosshair is the bright egg yellow, not merely a different colour.
    const uint32_t lit = firing_target.pixel(center_x, center_y);
    CAPTURE(lit, idle_target.pixel(center_x, center_y));
    REQUIRE((lit & 0xffu) > 200u);
    REQUIRE(((lit >> 8) & 0xffu) > 150u);
}

namespace {

// The readouts below live at fixed spots in hud.cpp, and this test has to know them in order
// to predict which glyph pixels a value should produce. That coupling is deliberate and
// visible: the alternative - asserting only that "the number region changed" - is exactly the
// granularity that let a readout frozen at a constant pass the whole suite.
struct Readout {
    int x;
    int y;
    int scale;
};

constexpr Readout HEALTH_NUMBER{16, 328, 2};
constexpr Readout AMMO_NUMBER{226, 328, 2};
constexpr Readout EGG_COUNTER{332, 323, 2};

// Pixels that change when `before` is drawn instead of `after` at the same spot. Colours are
// irrelevant here - only which pixels the glyph strokes cover - so the probe colour need not
// match the one the HUD actually uses.
std::vector<std::size_t> glyph_difference(const Readout &at, std::string_view before,
                                          std::string_view after) {
    const uint32_t probe = eh::rgba(255, 255, 255);
    GuardedFramebuffer first;
    GuardedFramebuffer second;
    eh::hud_detail::draw_text(first.framebuffer, at.x, at.y, before, at.scale, probe);
    eh::hud_detail::draw_text(second.framebuffer, at.x, at.y, after, at.scale, probe);

    const std::vector<uint32_t> a = first.image();
    const std::vector<uint32_t> b = second.image();
    std::vector<std::size_t> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            changed.push_back(i);
        }
    }
    return changed;
}

// Every pixel the two spellings disagree on must also disagree in the real HUD. Anything less
// than that - a count, a bounding box, "some pixel differs" - is satisfied by a readout that
// merely reacts to the value rather than reporting it.
void require_readout_reports(const eh::GameState &before, const eh::GameState &after,
                             const Readout &at, std::string_view before_text,
                             std::string_view after_text) {
    const std::vector<std::size_t> predicted = glyph_difference(at, before_text, after_text);
    INFO("comparing \"" << before_text << "\" against \"" << after_text << "\"");
    REQUIRE(predicted.size() > 8);

    const std::vector<uint32_t> rendered_before = render_pixels(before);
    const std::vector<uint32_t> rendered_after = render_pixels(after);
    std::size_t stuck = 0;
    for (const std::size_t index : predicted) {
        if (rendered_before[index] == rendered_after[index]) {
            ++stuck;
        }
    }
    REQUIRE(stuck == 0);
}

} // namespace

// Freezing the health number at 100, the ammo at 60 and the egg counter at 5 - so the HUD
// reported constants while the game ran underneath it - passed 97/97. The bar was pinned in
// proportion, but the number printed beside it was not, and neither were the other two.
TEST_CASE("hud: the readouts report the values, not merely react to them") {
    SECTION("health, across a digit-count change") {
        eh::GameState full = playing_state();
        eh::GameState hurt = playing_state();
        hurt.player.health = 42;
        require_readout_reports(full, hurt, HEALTH_NUMBER, "100", "42");
    }

    SECTION("health, one digit apart") {
        // 42 against 43 shares its first digit, so only a per-digit readout can pass.
        eh::GameState lower = playing_state();
        lower.player.health = 42;
        eh::GameState higher = playing_state();
        higher.player.health = 43;
        require_readout_reports(lower, higher, HEALTH_NUMBER, "42", "43");
    }

    SECTION("ammo") {
        eh::GameState many = playing_state();
        eh::GameState few = playing_state();
        few.player.ammo = 7;
        require_readout_reports(many, few, AMMO_NUMBER, "24", "7");
    }

    SECTION("eggs remaining") {
        eh::GameState three = playing_state();
        eh::GameState one = playing_state();
        one.eggs_remaining = 1;
        require_readout_reports(three, one, EGG_COUNTER, "EGGS: 3/5", "EGGS: 1/5");
    }
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

// GameState::shake is read only here, and no simulation path writes it (see
// "state: nothing in the simulation raises the screen shake timer" in
// test_replay.cpp). That makes render-side coverage the only thing standing
// between this code and silent rot: replacing the shake amplitude with a
// constant 0 was verified to pass all 117 tests.
//
// The displacement is recovered from pixels rather than recomputed. Rendering
// the same tick with and without shake gives two images that differ only by a
// rigid translation of the HUD content, so the shift is whichever offset best
// reconstructs one from the other. Nothing here reproduces the offset formula
// or names a single HUD layout coordinate, and the match margin is asserted so
// a recovered shift cannot be noise.
namespace {

struct Displacement {
    int dx = 0;
    int dy = 0;
    int margin = 0;
};

Displacement recover_displacement(const std::vector<uint32_t> &shaken,
                                  const std::vector<uint32_t> &still) {
    constexpr int W = eh::Framebuffer::W;
    constexpr int H = eh::Framebuffer::H;
    // Skip the panel's top border rows, which are drawn unshifted, and inset the
    // edges so a shifted lookup stays inside the image.
    constexpr int FIRST_ROW = 306;
    constexpr int SEARCH = 8;

    Displacement best;
    int best_score = -1;
    int runner_up = -1;
    for (int dy = -SEARCH; dy <= SEARCH; ++dy) {
        for (int dx = -SEARCH; dx <= SEARCH; ++dx) {
            int score = 0;
            for (int y = FIRST_ROW; y < H - SEARCH; ++y) {
                for (int x = SEARCH; x < W - SEARCH; ++x) {
                    if (shaken[static_cast<std::size_t>(y) * W + x] ==
                        still[static_cast<std::size_t>(y - dy) * W + (x - dx)]) {
                        ++score;
                    }
                }
            }
            if (score > best_score) {
                runner_up = best_score;
                best_score = score;
                best.dx = dx;
                best.dy = dy;
            } else if (score > runner_up) {
                runner_up = score;
            }
        }
    }
    best.margin = best_score - runner_up;
    return best;
}

} // namespace

TEST_CASE("hud: screen shake jitters the readouts within a bounded amplitude") {
    eh::GameState state = playing_state();

    SECTION("a still HUD does not move on its own") {
        state.shake = 0;
        state.tick = 0;
        const std::vector<uint32_t> first = render_pixels(state);
        state.tick = 7;
        const std::vector<uint32_t> later = render_pixels(state);
        CHECK(first == later);
    }

    SECTION("amplitude tracks the timer and saturates") {
        // Expected peak offset per timer value. hud.cpp clamps the amplitude, so
        // a long timer must not throw the HUD further than a short one.
        const std::array<std::pair<int, int>, 5> cases{{{1, 1}, {2, 2}, {3, 3}, {5, 3}, {9, 3}}};

        for (const auto &[timer, expected_peak] : cases) {
            int peak_dx = 0;
            int peak_dy = 0;
            int worst_margin = std::numeric_limits<int>::max();
            std::vector<std::pair<int, int>> distinct;
            for (std::uint32_t t = 0; t < 24; ++t) {
                state.tick = t;
                state.shake = 0;
                const std::vector<uint32_t> still = render_pixels(state);
                state.shake = static_cast<uint16_t>(timer);
                const std::vector<uint32_t> shaken = render_pixels(state);

                const Displacement shift = recover_displacement(shaken, still);
                peak_dx = std::max(peak_dx, std::abs(shift.dx));
                peak_dy = std::max(peak_dy, std::abs(shift.dy));
                worst_margin = std::min(worst_margin, shift.margin);
                const std::pair<int, int> offset{shift.dx, shift.dy};
                if (std::find(distinct.begin(), distinct.end(), offset) == distinct.end()) {
                    distinct.push_back(offset);
                }
            }

            CAPTURE(timer, peak_dx, peak_dy, worst_margin, distinct.size());
            // The recovered shift must be decisive, or the numbers below mean
            // nothing. Measured margin is over 1100 matching pixels.
            CHECK(worst_margin > 200);
            CHECK(peak_dx == expected_peak);
            CHECK(peak_dy == expected_peak);
            // A shake that lands on one fixed offset is a nudge, not a shake.
            CHECK(distinct.size() > 1);
        }
    }
}