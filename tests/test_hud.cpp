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

// Red-channel difference between a hurt and a calm render at one pixel. render_hud
// draws over a uniform background, so this isolates the vignette's blend alpha at that
// pixel without reproducing the blend itself: any other HUD art appears identically in
// both images and subtracts away.
int vignette_delta(const std::vector<uint32_t> &hurt, const std::vector<uint32_t> &calm, int x,
                   int y) {
    const std::size_t index =
        static_cast<std::size_t>(y) * eh::Framebuffer::W + static_cast<std::size_t>(x);
    return static_cast<int>(hurt[index] & 0xffu) - static_cast<int>(calm[index] & 0xffu);
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

namespace {

// Identifies a screen by the words it prints, without reproducing any of hud.cpp's layout
// arithmetic: an ink mask is built from the shipped font and searched for in the render, so
// position and scale are *discovered* rather than asserted. Moving or resizing a banner keeps
// this passing; changing what it says does not.
//
// draw_text lays a one-pixel-offset shadow glyph under the coloured one, so "differs from the
// background" spans two colours. Matching only pixels equal to the probe colour isolates the
// top layer, which is the part the real screen draws in a single uniform colour.
struct GlyphMask {
    std::vector<int> ink_x, ink_y; // must all carry one colour
    std::vector<int> gap_x, gap_y; // must all carry a different one
    int w = 0;
    int h = 0;
};

constexpr int MASK_PAD = 4;
constexpr int MIN_TEXT_SCALE = 2;
constexpr int MAX_TEXT_SCALE = 7;

GlyphMask glyph_mask(std::string_view text, int scale) {
    constexpr uint32_t PROBE = 0xffffffffu;
    GuardedFramebuffer scratch;
    eh::hud_detail::draw_text(scratch.framebuffer, MASK_PAD, MASK_PAD, text, scale, PROBE);
    const std::vector<uint32_t> pixels = scratch.image();

    GlyphMask mask;
    int max_x = 0;
    int max_y = 0;
    for (int y = 0; y < eh::Framebuffer::H; ++y) {
        for (int x = 0; x < eh::Framebuffer::W; ++x) {
            if (pixels[static_cast<std::size_t>(y) * eh::Framebuffer::W + x] != PROBE) {
                continue;
            }
            mask.ink_x.push_back(x - MASK_PAD);
            mask.ink_y.push_back(y - MASK_PAD);
            max_x = std::max(max_x, x - MASK_PAD);
            max_y = std::max(max_y, y - MASK_PAD);
        }
    }
    if (mask.ink_x.empty() || max_x + MASK_PAD >= eh::Framebuffer::W - 1) {
        return {}; // empty, or clipped by the framebuffer edge and so not a whole word
    }

    mask.w = max_x + 1;
    mask.h = max_y + 1;
    for (int y = 0; y < mask.h; ++y) {
        for (int x = 0; x < mask.w; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y + MASK_PAD) * eh::Framebuffer::W + x + MASK_PAD;
            if (pixels[index] != PROBE) {
                mask.gap_x.push_back(x);
                mask.gap_y.push_back(y);
            }
        }
    }
    return mask;
}

// A match needs both halves: every glyph pixel carrying one colour, and no pixel between the
// strokes carrying it too. Without the second half any flat region matches, and the end screens
// are mostly flat overlay - which is also why the gaps are probed first.
bool mask_appears(const std::vector<uint32_t> &screen, const GlyphMask &mask) {
    if (mask.ink_x.empty() || mask.gap_x.empty()) {
        return false;
    }
    const std::size_t probes = std::min<std::size_t>(24, mask.gap_x.size());
    const std::size_t stride = mask.gap_x.size() / probes;
    const std::size_t middle = mask.ink_x.size() / 2;

    for (int y0 = 0; y0 + mask.h <= eh::Framebuffer::H; ++y0) {
        for (int x0 = 0; x0 + mask.w <= eh::Framebuffer::W; ++x0) {
            const auto at = [&](int dx, int dy) {
                return screen[static_cast<std::size_t>(y0 + dy) * eh::Framebuffer::W + x0 + dx];
            };
            const uint32_t ink = at(mask.ink_x[0], mask.ink_y[0]);
            if (at(mask.ink_x[middle], mask.ink_y[middle]) != ink) {
                continue;
            }
            bool ok = true;
            for (std::size_t i = 0; i < probes && ok; ++i) {
                const std::size_t j = i * stride;
                ok = at(mask.gap_x[j], mask.gap_y[j]) != ink;
            }
            for (std::size_t i = 1; i < mask.ink_x.size() && ok; ++i) {
                ok = at(mask.ink_x[i], mask.ink_y[i]) == ink;
            }
            for (std::size_t i = 0; i < mask.gap_x.size() && ok; ++i) {
                ok = at(mask.gap_x[i], mask.gap_y[i]) != ink;
            }
            if (ok) {
                return true;
            }
        }
    }
    return false;
}

bool screen_says(const std::vector<uint32_t> &screen, std::string_view text) {
    for (int scale = MIN_TEXT_SCALE; scale <= MAX_TEXT_SCALE; ++scale) {
        if (mask_appears(screen, glyph_mask(text, scale))) {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> screen_pixels(eh::Screen screen) {
    eh::GameState state = playing_state();
    state.screen = screen;
    return render_pixels(state);
}

} // namespace

// Rendering the winning end screen for Screen::Lost passed 119/119, and rendering the losing
// one for Screen::Won passed 119/119 as well: the suite asked only whether each screen "draws a
// visible overlay". That is reaction, not reporting - so the game could congratulate a player
// who had just been killed, and every test would still be green.
//
// The positive checks below are what keep the negative ones honest. The first version of this
// matcher was broken and reported every phrase absent from every screen, which would have
// satisfied a test made only of CHECK_FALSE.
TEST_CASE("hud: each screen says which screen it is") {
    constexpr std::string_view TITLE_BANNER = "EGG HUNT";
    constexpr std::string_view TITLE_PROMPT = "PRESS ENTER TO START";
    constexpr std::string_view WON_BANNER = "ALL EGGS CRACKED!";
    constexpr std::string_view LOST_BANNER = "SCRAMBLED!";
    constexpr std::string_view RETRY_PROMPT = "PRESS R TO PLAY AGAIN";

    SECTION("the title screen names the game and how to start it") {
        const std::vector<uint32_t> title = screen_pixels(eh::Screen::Title);
        CHECK(screen_says(title, TITLE_BANNER));
        CHECK(screen_says(title, TITLE_PROMPT));
        CHECK_FALSE(screen_says(title, WON_BANNER));
        CHECK_FALSE(screen_says(title, LOST_BANNER));
    }

    SECTION("winning reports the win, and only the win") {
        const std::vector<uint32_t> won = screen_pixels(eh::Screen::Won);
        CHECK(screen_says(won, WON_BANNER));
        CHECK(screen_says(won, RETRY_PROMPT));
        CHECK_FALSE(screen_says(won, LOST_BANNER));
        CHECK_FALSE(screen_says(won, TITLE_BANNER));
    }

    SECTION("losing reports the loss, and never the win") {
        const std::vector<uint32_t> lost = screen_pixels(eh::Screen::Lost);
        CHECK(screen_says(lost, LOST_BANNER));
        CHECK(screen_says(lost, RETRY_PROMPT));
        CHECK_FALSE(screen_says(lost, WON_BANNER));
        CHECK_FALSE(screen_says(lost, TITLE_BANNER));
    }

    SECTION("the playing HUD carries no end-of-game banner at all") {
        const std::vector<uint32_t> playing = screen_pixels(eh::Screen::Playing);
        CHECK_FALSE(screen_says(playing, TITLE_BANNER));
        CHECK_FALSE(screen_says(playing, WON_BANNER));
        CHECK_FALSE(screen_says(playing, LOST_BANNER));
        CHECK_FALSE(screen_says(playing, RETRY_PROMPT));
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
// mvp.md line 13 is "crack all five eggs, then reach the Basket to win", and the HUD's
// objective prompt is the game's only explicit statement of that second half -- a banner
// reading GET TO THE BASKET! plus an OBJECTIVE readout, both gated on eggs_remaining == 0.
// Deleting the call entirely passed 125/125. No HUD fixture in the suite had ever set
// eggs_remaining to 0, so every pixel of the prompt was drawn by code no test reached, and
// a player who had just cleared the level would be told nothing at all.
//
// screen_says discovers the ink colour from the render rather than being told it, so the
// first two sections hold in either phase of the prompt's blink. That keeps them
// independent of the third, which is the only one that owns the blink.
TEST_CASE("hud: the objective prompt appears only once the last egg is cracked") {
    constexpr std::string_view OBJECTIVE_PROMPT = "GET TO THE BASKET!";

    SECTION("while an egg is still loose the HUD does not send the player home") {
        eh::GameState hunting = playing_state();
        hunting.eggs_remaining = 1;
        CHECK_FALSE(screen_says(render_pixels(hunting), OBJECTIVE_PROMPT));
    }

    SECTION("cracking the last egg puts the objective on screen") {
        eh::GameState cleared = playing_state();
        cleared.eggs_remaining = 0;
        // The positive check is what keeps the negative one above honest: a matcher that
        // could not find anything would satisfy the CHECK_FALSE for the wrong reason.
        CHECK(screen_says(render_pixels(cleared), OBJECTIVE_PROMPT));
    }

    SECTION("the prompt blinks, so it reads as an instruction and not as furniture") {
        // warning_flash toggles every 8 ticks. At full health it has no other consumer --
        // the health colours only consult it below 25 -- and shake is always zero, so its
        // tick-driven jitter contributes nothing. Two renders differing only in gs.tick
        // therefore isolate the prompt's own colour.
        eh::GameState lit = playing_state();
        lit.eggs_remaining = 0;
        lit.tick = 0;
        eh::GameState alternate = lit;
        alternate.tick = 8;

        const std::vector<uint32_t> first = render_pixels(lit);
        const std::vector<uint32_t> second = render_pixels(alternate);
        CHECK(first != second);
        // Both phases must still say it: a "blink" that spends half its cycle invisible
        // would satisfy the inequality above while making the instruction unreadable.
        CHECK(screen_says(first, OBJECTIVE_PROMPT));
        CHECK(screen_says(second, OBJECTIVE_PROMPT));
    }
}

// The vignette is the game's only damage feedback outside the health bar, and finding
// #15 pinned that it appears, reddens the corners, spares the centre, and fades as the
// flash expires. It pins nothing about the shape of either curve, and five of six
// mutants to the curves survive the whole suite:
//
//   spatial falloff made flat (a hard-edged red border, no gradient)   130/130 passed
//   band depth 44 -> 22                                               130/130 passed
//   fade slope 15 -> 20 per tick                                      130/130 passed
//   alpha cap 180 -> 90                                               130/130 passed
//   hurt_flash assigned rather than max-ed                            130/130 passed
//   spatial falloff inverted                                          caught by #15
//
// Only the fully inverted gradient dies, because #15 samples the outermost corner and
// nothing else. Everything between the edge and the centre is unconstrained.
//
// The numbers below were read out of a running binary rather than derived. render_hud
// draws over a uniform background here, so the red delta between a calm and a hurt
// render is a clean function of the blend alpha at that pixel and this test does no
// arithmetic beyond that subtraction.
//
// Two honest limitations. The shipped alpha cap of 180 is unreachable: HURT_FLASH_TICKS
// is 9, so peak alpha never exceeds 135 and the std::min can never choose its first
// argument. The section below pins the effective peak and would catch a cap that starts
// binding, but the literal 180 is dead until the flash duration passes 12 ticks. And the
// std::max at entities.cpp:199 is provably inert -- hurt_flash is only ever written to
// HURT_FLASH_TICKS and only ever decremented, so it lives in [0, 9] and max(x, 9) is 9
// for every value it can hold. That mutant is recorded, not tested.
TEST_CASE("hud: the damage vignette is a gradient of a measured depth and intensity") {
    eh::GameState calm = playing_state();
    const std::vector<uint32_t> calm_pixels = render_pixels(calm);

    const int mid_x = eh::Framebuffer::W / 2;
    const int mid_y = eh::Framebuffer::H / 2;

    SECTION("the red falls off smoothly inward instead of stopping at a border") {
        eh::GameState hurt = playing_state();
        hurt.player.hurt_flash = 9;
        const std::vector<uint32_t> hurt_pixels = render_pixels(hurt);

        // Down from the top edge and in from the left edge, so both arms of the
        // distance term are exercised.
        const std::vector<int> depths = {0, 5, 11, 22, 33, 42};
        int previous_vertical = 1000;
        int previous_horizontal = 1000;
        for (int depth : depths) {
            const int vertical = vignette_delta(hurt_pixels, calm_pixels, mid_x, depth);
            const int horizontal = vignette_delta(hurt_pixels, calm_pixels, depth, mid_y);
            CAPTURE(depth, vertical, horizontal);
            CHECK(vertical < previous_vertical);
            CHECK(horizontal < previous_horizontal);
            previous_vertical = vertical;
            previous_horizontal = horizontal;
        }
        // Non-vacuity: a decreasing sequence of zeros would satisfy the loop above.
        // Deliberately far below the tuned peak -- intensity is section three's
        // contract, and this guard must not quietly duplicate it.
        CHECK(vignette_delta(hurt_pixels, calm_pixels, mid_x, 0) > 5);
    }

    SECTION("the band is exactly forty-four pixels deep on all four edges") {
        eh::GameState hurt = playing_state();
        hurt.player.hurt_flash = 9;
        const std::vector<uint32_t> hurt_pixels = render_pixels(hurt);

        CHECK(vignette_delta(hurt_pixels, calm_pixels, mid_x, 43) > 0);
        CHECK(vignette_delta(hurt_pixels, calm_pixels, mid_x, 44) == 0);
        CHECK(vignette_delta(hurt_pixels, calm_pixels, mid_x, eh::Framebuffer::H - 44) > 0);
        CHECK(vignette_delta(hurt_pixels, calm_pixels, mid_x, eh::Framebuffer::H - 45) == 0);
        CHECK(vignette_delta(hurt_pixels, calm_pixels, 43, mid_y) > 0);
        CHECK(vignette_delta(hurt_pixels, calm_pixels, 44, mid_y) == 0);
        CHECK(vignette_delta(hurt_pixels, calm_pixels, eh::Framebuffer::W - 44, mid_y) > 0);
        CHECK(vignette_delta(hurt_pixels, calm_pixels, eh::Framebuffer::W - 45, mid_y) == 0);

        // And nothing deeper than the band moves at all, so a wider vignette cannot
        // hide behind the edge samples above.
        int changed_inside = 0;
        for (int y = 44; y < eh::Framebuffer::H - 44; ++y) {
            for (int x = 44; x < eh::Framebuffer::W - 44; ++x) {
                if (vignette_delta(hurt_pixels, calm_pixels, x, y) != 0) {
                    ++changed_inside;
                }
            }
        }
        CHECK(changed_inside == 0);
    }

    SECTION("peak intensity is a fixed step per remaining flash tick") {
        // Measured at the outermost ring, where the spatial term is 1. Nine ticks is a
        // full flash; the rest are points along the fade.
        const std::vector<std::pair<uint16_t, int>> expected = {{9, 49}, {6, 33}, {3, 16}, {1, 5}};
        for (const auto &[flash, delta] : expected) {
            eh::GameState hurt = playing_state();
            hurt.player.hurt_flash = flash;
            const std::vector<uint32_t> hurt_pixels = render_pixels(hurt);
            const int measured = vignette_delta(hurt_pixels, calm_pixels, mid_x, 0);
            CAPTURE(flash, measured, delta);
            CHECK(measured == delta);
        }

        // An expired flash paints nothing at all.
        eh::GameState expired = playing_state();
        expired.player.hurt_flash = 0;
        CHECK(render_pixels(expired) == calm_pixels);
    }
}