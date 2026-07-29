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
#include <limits>
#include <memory>
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

int drawn_pixel_count(const GuardedFramebuffer &buffer) {
    int count = 0;
    for (std::size_t index = 0; index < GuardedFramebuffer::PIXEL_COUNT; ++index) {
        if (buffer.framebuffer.pixels[index] != BACKGROUND) {
            ++count;
        }
    }
    return count;
}

double drawn_centroid_x(const GuardedFramebuffer &buffer) {
    long long total = 0;
    long long weighted = 0;
    for (int y = 0; y < eh::Framebuffer::H; ++y) {
        for (int x = 0; x < eh::Framebuffer::W; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * eh::Framebuffer::W + static_cast<std::size_t>(x);
            if (buffer.framebuffer.pixels[index] != BACKGROUND) {
                ++total;
                weighted += x;
            }
        }
    }
    return total == 0 ? -1.0 : static_cast<double>(weighted) / static_cast<double>(total);
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

namespace {

// Which screen columns did the billboard actually touch? Answered by reading pixels back
// rather than by recomputing the projection, so this cannot agree with a broken renderer
// by repeating its arithmetic.
std::vector<bool> drawn_columns(const GuardedFramebuffer &buffer) {
    const std::vector<uint32_t> pixels = buffer.pixels();
    std::vector<bool> touched(static_cast<std::size_t>(eh::Framebuffer::W), false);
    for (std::size_t y = 0; y < static_cast<std::size_t>(eh::Framebuffer::H); ++y) {
        for (std::size_t x = 0; x < static_cast<std::size_t>(eh::Framebuffer::W); ++x) {
            if (pixels[y * static_cast<std::size_t>(eh::Framebuffer::W) + x] != BACKGROUND) {
                touched[x] = true;
            }
        }
    }
    return touched;
}

} // namespace

// Occlusion is decided per column against fb.depth[screen_x]. Both occlusion tests above
// fill the depth buffer with a single value, so they can only tell "wholly hidden" from
// "wholly visible" - deciding the entire billboard from one column's depth passed 95/95.
// The case that breaks is an egg peeking around a corner, which is ordinary play.
TEST_CASE("sprites: a wall hides only the columns it really covers") {
    eh::GameState state = state_facing_east();
    state.entities.push_back(entity_at(1, eh::EntityType::Egg, 2.0f, 0.0f));
    constexpr std::size_t SPLIT = static_cast<std::size_t>(eh::Framebuffer::W) / 2;

    GuardedFramebuffer open;
    eh::render_sprites(state, open.framebuffer);
    const std::vector<bool> open_columns = drawn_columns(open);

    // Non-vacuity: the billboard has to genuinely straddle the split, or hiding one side
    // proves nothing at all.
    const auto left_drawn = std::count(
        open_columns.begin(), open_columns.begin() + static_cast<std::ptrdiff_t>(SPLIT), true);
    const auto right_drawn = std::count(open_columns.begin() + static_cast<std::ptrdiff_t>(SPLIT),
                                        open_columns.end(), true);
    REQUIRE(left_drawn > 2);
    REQUIRE(right_drawn > 2);

    // A wall nearer than the egg, but only across the left half of the screen.
    GuardedFramebuffer split;
    for (std::size_t x = 0; x < SPLIT; ++x) {
        split.depth[x] = 0.5f;
    }
    eh::render_sprites(state, split.framebuffer);
    const std::vector<bool> split_columns = drawn_columns(split);

    for (std::size_t x = 0; x < SPLIT; ++x) {
        INFO("column " << x << " sits behind the near wall and must be hidden");
        REQUIRE_FALSE(split_columns[x]);
    }
    for (std::size_t x = SPLIT; x < static_cast<std::size_t>(eh::Framebuffer::W); ++x) {
        INFO("column " << x << " has open sight and must be untouched by the wall");
        REQUIRE(split_columns[x] == open_columns[x]);
    }
    REQUIRE(split.guards_intact());
}

// Sprites never write to the depth buffer - it only ever holds wall distances - so which of
// two overlapping eggs you see is decided purely by paint order. Reversing the far-to-near
// sort passed 96/96, which would put a distant egg in front of a close one.
TEST_CASE("sprites: the nearer of two overlapping eggs is the one you see") {
    constexpr std::size_t X = static_cast<std::size_t>(eh::Framebuffer::W) / 2;

    eh::GameState near_only = state_facing_east();
    near_only.entities.push_back(entity_at(1, eh::EntityType::Egg, 2.0f, 0.0f));
    GuardedFramebuffer near_buffer;
    eh::render_sprites(near_only, near_buffer.framebuffer);
    const std::vector<uint32_t> near_pixels = near_buffer.pixels();

    eh::GameState far_only = state_facing_east();
    far_only.entities.push_back(entity_at(2, eh::EntityType::Egg, 9.0f, 0.0f));
    GuardedFramebuffer far_buffer;
    eh::render_sprites(far_only, far_buffer.framebuffer);
    const std::vector<uint32_t> far_pixels = far_buffer.pixels();

    // Find a pixel both eggs paint, and paint differently. Measured rather than assumed:
    // without such a pixel the comparison below would hold no matter what order they drew in.
    std::size_t probe = 0;
    bool found = false;
    for (std::size_t y = 0; y < static_cast<std::size_t>(eh::Framebuffer::H); ++y) {
        const std::size_t index = y * static_cast<std::size_t>(eh::Framebuffer::W) + X;
        if (near_pixels[index] != BACKGROUND && far_pixels[index] != BACKGROUND &&
            near_pixels[index] != far_pixels[index]) {
            probe = index;
            found = true;
            break;
        }
    }
    REQUIRE(found);

    // Insertion order must not matter; only depth may decide. Both orders are checked so a
    // renderer that simply drew entities as listed could not pass by luck.
    for (const bool near_first : {true, false}) {
        eh::GameState both = state_facing_east();
        if (near_first) {
            both.entities.push_back(entity_at(1, eh::EntityType::Egg, 2.0f, 0.0f));
            both.entities.push_back(entity_at(2, eh::EntityType::Egg, 9.0f, 0.0f));
        } else {
            both.entities.push_back(entity_at(2, eh::EntityType::Egg, 9.0f, 0.0f));
            both.entities.push_back(entity_at(1, eh::EntityType::Egg, 2.0f, 0.0f));
        }

        GuardedFramebuffer buffer;
        eh::render_sprites(both, buffer.framebuffer);

        INFO("near egg pushed first: " << near_first);
        REQUIRE(buffer.pixels()[probe] == near_pixels[probe]);
    }
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

// Every other sprite test faces east, so the billboard camera basis could be frozen at east and
// the whole suite stayed green: walls turned with the player and the eggs did not. These two
// cases are the same contract as the tests above, asked at headings other than zero. Handedness
// is pinned separately against the wall renderer in test_render.cpp, because a dead-ahead egg is
// left/right symmetric and cannot see a mirrored camera plane.
TEST_CASE("sprites: billboards follow the camera around, not just east") {
    constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;
    constexpr double DISTANCE = 3.0;
    const std::array<double, 6> headings{0.0, 37.0, 90.0, 180.0, 214.0, 300.0};

    for (double heading : headings) {
        CAPTURE(heading);
        const double radians = heading * DEGREES_TO_RADIANS;
        const auto ahead_x = static_cast<float>(std::cos(radians) * DISTANCE);
        const auto ahead_y = static_cast<float>(std::sin(radians) * DISTANCE);

        eh::GameState ahead = state_facing_east();
        ahead.player.angle = eh::angle_from_deg(heading);
        ahead.entities.push_back(entity_at(1, eh::EntityType::Egg, ahead_x, ahead_y));

        GuardedFramebuffer ahead_buffer;
        eh::render_sprites(ahead, ahead_buffer.framebuffer);
        const PixelBounds bounds = modified_bounds(ahead_buffer);
        REQUIRE(ahead_buffer.guards_intact());

        // An egg dead ahead is centre-screen. That is what "ahead" means, at any heading.
        REQUIRE(bounds.valid());
        const int centre = (bounds.left + bounds.right) / 2;
        CAPTURE(centre, bounds.left, bounds.right);
        REQUIRE(std::abs(centre - eh::Framebuffer::W / 2) <= 2);

        // The same egg directly behind is culled, at the same heading.
        eh::GameState behind = state_facing_east();
        behind.player.angle = eh::angle_from_deg(heading);
        behind.entities.push_back(entity_at(1, eh::EntityType::Egg, -ahead_x, -ahead_y));

        GuardedFramebuffer behind_buffer;
        eh::render_sprites(behind, behind_buffer.framebuffer);
        REQUIRE(behind_buffer.pixels_unchanged());
        REQUIRE(behind_buffer.guards_intact());
    }
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

// Both sides of the bob seam were pinned -- player_tick advances the normalized phase, and the
// two tests above pin the horizontal sweep -- but nothing named the vertical channel. Deleting
// the bounce outright, or halving its frequency, left the whole suite green. The weapon slid
// side to side without ever stepping.
TEST_CASE("sprites: the weapon dips four times per left-right sweep") {
    constexpr int STEPS = 16;

    eh::GameState state = state_facing_east();
    state.player.bob = 0;
    GuardedFramebuffer rest_buffer;
    eh::render_weapon(state, rest_buffer.framebuffer);
    const PixelBounds rest_bounds = modified_bounds(rest_buffer);
    REQUIRE(rest_bounds.valid());

    std::array<int, STEPS + 1> drop{};
    std::array<int, STEPS + 1> sway{};
    for (int step = 0; step <= STEPS; ++step) {
        state.player.bob = static_cast<eh::fx>((static_cast<int64_t>(eh::FX_ONE) * step) / STEPS);
        GuardedFramebuffer buffer;
        eh::render_weapon(state, buffer.framebuffer);
        const PixelBounds bounds = modified_bounds(buffer);
        REQUIRE(bounds.valid());
        REQUIRE(buffer.guards_intact());
        drop[static_cast<std::size_t>(step)] = bounds.top - rest_bounds.top;
        sway[static_cast<std::size_t>(step)] = bounds.left - rest_bounds.left;
    }

    // The weapon only ever dips below where it rests; it never floats above it.
    for (int step = 0; step <= STEPS; ++step) {
        CAPTURE(step);
        REQUIRE(drop[static_cast<std::size_t>(step)] >= 0);
    }

    // Four footfalls: back at rest on every quarter of the cycle, at its lowest halfway between.
    // A single-frequency bounce would put a peak on the quarters instead of a rest point.
    for (int quarter = 0; quarter <= 4; ++quarter) {
        CAPTURE(quarter);
        REQUIRE(drop[static_cast<std::size_t>(quarter * 4)] == 0);
    }
    for (int footfall = 0; footfall < 4; ++footfall) {
        CAPTURE(footfall);
        REQUIRE(drop[static_cast<std::size_t>(footfall * 4 + 2)] >= 4);
    }

    // Exactly one horizontal sweep spans the same window, which is what makes the dips read as
    // four per sweep rather than merely four dips.
    REQUIRE(sway[0] == 0);
    REQUIRE(sway[8] == 0);
    REQUIRE(sway[16] == 0);
    REQUIRE(sway[4] > 0);
    REQUIRE(sway[12] < 0);
}

// Every sprite test above asserts a relational or safety property: stays in
// bounds, occludes correctly, is visible in the open, orders stably. All of them
// hold when a billboard renders at a constant size regardless of distance.
// Replacing the perspective divide with a fixed constant -- so a distant egg is
// exactly as large as one in your face -- was verified to pass all 79 tests.
// Perspective on enemies is the difference between a 3D game and a sticker.
TEST_CASE("sprites: billboards scale inversely with distance") {
    auto rendered_height = [](float distance) {
        eh::GameState state = state_facing_east();
        state.entities.push_back(entity_at(1, eh::EntityType::Egg, distance, 0.0f));
        GuardedFramebuffer buffer;
        eh::render_sprites(state, buffer.framebuffer);
        REQUIRE(buffer.guards_intact());
        const PixelBounds bounds = modified_bounds(buffer);
        REQUIRE(bounds.valid());
        return bounds;
    };

    const PixelBounds near_bounds = rendered_height(2.0f);
    const PixelBounds far_bounds = rendered_height(4.0f);

    const float near_height = static_cast<float>(near_bounds.bottom - near_bounds.top + 1);
    const float far_height = static_cast<float>(far_bounds.bottom - far_bounds.top + 1);
    const float near_width = static_cast<float>(near_bounds.right - near_bounds.left + 1);
    const float far_width = static_cast<float>(far_bounds.right - far_bounds.left + 1);

    // Direction first, so the magnitude check below cannot pass vacuously.
    REQUIRE(near_height > far_height);
    REQUIRE(near_width > far_width);

    // Halving the distance must double the projected size. The tolerance covers
    // pixel quantization on a roughly 100 pixel sprite, not a scaling error.
    CAPTURE(near_height);
    CAPTURE(far_height);
    REQUIRE(near_height / far_height == Catch::Approx(2.0f).epsilon(0.05));
    REQUIRE(near_width / far_width == Catch::Approx(2.0f).epsilon(0.05));
}

// The sprite half of the shared fog contract. Pointing sprites.cpp at a 48 tile falloff
// while walls kept 16 passed all 101 tests, which would leave an egg glowing against a wall
// that darkens around it. This proves render_sprites consults the shared curve.
TEST_CASE("sprites: a distant egg is shaded darker than a near one") {
    auto mean_egg_luma = [](float distance) {
        eh::GameState state = state_facing_east();
        state.entities.clear();
        state.entities.push_back(entity_at(1, eh::EntityType::Egg, distance, 0.0f));

        GuardedFramebuffer buffer;
        eh::render_sprites(state, buffer.framebuffer);

        double total = 0.0;
        int samples = 0;
        for (const uint32_t pixel : buffer.pixels()) {
            if (pixel == BACKGROUND) {
                continue;
            }
            total += static_cast<double>((pixel >> 16) & 0xffu) +
                     static_cast<double>((pixel >> 8) & 0xffu) + static_cast<double>(pixel & 0xffu);
            ++samples;
        }
        REQUIRE(samples > 0);
        return total / static_cast<double>(samples);
    };

    const double near_luma = mean_egg_luma(2.0f);
    const double far_luma = mean_egg_luma(14.0f);

    // 0.875 at two tiles against the clamped 0.25 floor at fourteen: a true ratio of 0.29.
    // A 48 tile falloff would raise it to 0.75. Compared as a ratio because the far egg
    // covers far fewer pixels and so samples the billboard differently.
    REQUIRE(near_luma > 0.0);
    REQUIRE(far_luma < near_luma);
    REQUIRE(far_luma / near_luma < 0.5);
}

TEST_CASE("sprites: a freshly hit egg renders visibly tinted") {
    // hit_flash is set by entities_tick, set again by fire(), decremented every tick, and
    // serialized into the replay digest - three workstreams assert the number is 9. None
    // of them draw it. Deleting the tint entirely changed no test result.
    auto render = [](uint16_t hit_flash) {
        eh::GameState state = state_facing_east();
        eh::Entity egg = entity_at(1, eh::EntityType::Egg, 3.0f, 0.0f);
        egg.hit_flash = hit_flash;
        state.entities.push_back(egg);
        auto buffer = std::make_unique<GuardedFramebuffer>();
        eh::render_sprites(state, buffer->framebuffer);
        REQUIRE(buffer->guards_intact());
        return buffer->pixels();
    };

    const std::vector<uint32_t> calm = render(0);
    const std::vector<uint32_t> struck = render(9);
    REQUIRE(calm.size() == struck.size());

    std::size_t egg_pixels = 0;
    std::size_t changed = 0;
    double calm_red = 0.0;
    double calm_green = 0.0;
    double struck_red = 0.0;
    double struck_green = 0.0;
    for (std::size_t i = 0; i < calm.size(); ++i) {
        if (calm[i] == BACKGROUND) {
            continue;
        }
        ++egg_pixels;
        if (calm[i] == struck[i]) {
            continue;
        }
        ++changed;

        // hit_tint raises red and divides green and blue by three. Distance shading is
        // applied afterwards, so assert the relationship rather than absolute levels.
        const uint32_t red = struck[i] & 0xffu;
        const uint32_t was_red = calm[i] & 0xffu;
        const uint32_t green = (struck[i] >> 8) & 0xffu;
        const uint32_t was_green = (calm[i] >> 8) & 0xffu;
        REQUIRE(red >= was_red);
        REQUIRE(green <= was_green);

        calm_red += was_red;
        calm_green += was_green;
        struck_red += red;
        struck_green += green;
    }

    CAPTURE(egg_pixels, changed, calm_red, struck_red, calm_green, struck_green);
    REQUIRE(egg_pixels > 100);
    // Nearly the whole egg flashes; a handful of already-red texels may be unchanged.
    REQUIRE(changed * 2 > egg_pixels);

    // The flash reads as red, not just as some other colour: red rises while green is
    // cut to well under half, so the hue shifts rather than the brightness alone.
    REQUIRE(struck_red > calm_red);
    REQUIRE(struck_green * 2.0 < calm_green);
}

TEST_CASE("sprites: an unhit egg is not tinted") {
    eh::GameState state = state_facing_east();
    state.entities.push_back(entity_at(1, eh::EntityType::Egg, 3.0f, 0.0f));
    auto first = std::make_unique<GuardedFramebuffer>();
    eh::render_sprites(state, first->framebuffer);

    state.entities[0].hit_flash = 0;
    auto second = std::make_unique<GuardedFramebuffer>();
    eh::render_sprites(state, second->framebuffer);

    REQUIRE(first->pixels() == second->pixels());
}

TEST_CASE("sprites: the muzzle flash peaks when fired and fades over its lifetime") {
    // player.cpp sets muzzle_flash and sprites.cpp divides by the same duration to derive
    // flash and recoil strength. These were independent literals; pointing the renderer's
    // copy at a different value passed all 87 tests while the weapon silently stopped
    // kicking its full distance. The constant is now shared, and this pins the ramp.
    struct Shot {
        std::size_t lit;
        PixelBounds bounds;
    };

    auto fire = [](uint16_t muzzle_flash) {
        eh::GameState state = state_facing_east();
        state.muzzle_flash = muzzle_flash;
        auto buffer = std::make_unique<GuardedFramebuffer>();
        eh::render_weapon(state, buffer->framebuffer);
        REQUIRE(buffer->guards_intact());

        Shot shot{0, modified_bounds(*buffer)};
        for (uint32_t pixel : buffer->pixels()) {
            if (pixel != BACKGROUND && is_flash_pixel(pixel)) {
                ++shot.lit;
            }
        }
        return shot;
    };

    const Shot idle = fire(0);
    const Shot fresh = fire(eh::MUZZLE_FLASH_TICKS);
    const Shot stale = fire(1);
    CAPTURE(idle.lit, fresh.lit, idle.bounds.top, fresh.bounds.top, stale.bounds.top);

    // The flash itself only exists while the timer is running.
    REQUIRE(idle.lit == 0);
    REQUIRE(fresh.lit > 0);
    REQUIRE(idle.bounds.valid());

    // Recoil kicks the weapon down and to the right in proportion to the remaining
    // ticks, so firing reads as an impulse that settles rather than a steady offset.
    // Only the two firing frames are compared: the flash starburst extends the drawn
    // sprite upward, so an idle frame's bounds are not measuring the same thing.
    REQUIRE(fresh.bounds.valid());
    REQUIRE(stale.bounds.valid());
    REQUIRE(fresh.bounds.top > stale.bounds.top);
    REQUIRE(fresh.bounds.left > stale.bounds.left);
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

TEST_CASE("sprites: the muzzle flash erupts past the end of the barrel") {
    // The test above counts flash pixels, and a count carries no position: the
    // flash can be drawn anywhere on the weapon and still raise the tally. Moving
    // it to the far side of the barrel (the sign of the 22-unit offset along the
    // weapon axis) was verified to pass the entire suite, 106/106.
    //
    // What a muzzle flash actually has to do is stick out past the muzzle. The
    // barrel points up and to the left, so the up-left extreme of everything drawn
    // is the observable: firing must push that extreme further out than the idle
    // weapon reaches. Recoil works against this assertion rather than for it,
    // because it shoves the whole weapon down-right by about 14 pixels, so the
    // flash has to overcome that before the check can pass.
    auto upper_left_extreme = [](uint16_t muzzle_flash) {
        eh::GameState gs = state_facing_east();
        gs.player.bob = 0;
        gs.muzzle_flash = muzzle_flash;
        GuardedFramebuffer buffer;
        eh::render_weapon(gs, buffer.framebuffer);
        const std::vector<uint32_t> pixels = buffer.pixels();

        int extreme = std::numeric_limits<int>::max();
        long long drawn = 0;
        for (int y = 0; y < eh::Framebuffer::H; ++y) {
            for (int x = 0; x < eh::Framebuffer::W; ++x) {
                if (pixels[static_cast<std::size_t>(y) * eh::Framebuffer::W +
                           static_cast<std::size_t>(x)] == BACKGROUND) {
                    continue;
                }
                ++drawn;
                extreme = std::min(extreme, x + y);
            }
        }
        REQUIRE(drawn > 0);
        REQUIRE(buffer.guards_intact());
        return extreme;
    };

    const int idle_reach = upper_left_extreme(0);
    const int firing_reach = upper_left_extreme(4);

    // Measured: 473 idle, 422 firing. With the flash on the wrong side of the
    // barrel the silhouette is just the recoiled weapon and this rises to 483,
    // so the comparison alone is decisive; the margin keeps a flash that barely
    // peeks out from satisfying it.
    REQUIRE(firing_reach < idle_reach);
    REQUIRE(idle_reach - firing_reach > 25);
}

TEST_CASE("sprites: the flash's translucent glow dims as the shot ages") {
    // The test above named for fading ("peaks when fired and fades over its
    // lifetime") never compares stale.lit to fresh.lit -- the stale shot feeds
    // only the recoil bounds, which measure where the weapon sits, not how
    // bright it burns. And is_flash_pixel selects red>245, green>195: that is
    // the star-ray colour, drawn opaque and identical at every tick. So every
    // flash assertion in the suite observes the one component that cannot fade.
    //
    // Replacing the whole strength ramp with a constant (flash_strength = 1.0f,
    // so the glow never dims) passed all 115 tests while recoil kept the bounds
    // checks green.
    //
    // The glow is the only thing the weapon draws that is not fully opaque, so
    // a pixel belongs to it exactly when its value depends on what was
    // underneath. Rendering one state over two backgrounds isolates it without
    // naming a coordinate: opaque art lands identically and cancels. Source-over
    // then gives the strength directly -- two backgrounds differing by D in a
    // channel leave a difference of D*(1-alpha), so with D = 255 the surviving
    // red-channel gap is 255 - alpha. That is compositing algebra, not the
    // weapon's geometry, and it assumes nothing about the ramp under test.
    constexpr uint32_t OVER_LIGHT = 0xffffffffu;
    constexpr uint32_t OVER_DARK = 0xff000000u;

    auto render_over = [](uint32_t background, uint16_t muzzle_flash) {
        GuardedFramebuffer buffer;
        std::fill_n(buffer.framebuffer.pixels, GuardedFramebuffer::PIXEL_COUNT, background);

        eh::GameState gs = state_facing_east();
        gs.player.bob = 0;
        gs.muzzle_flash = muzzle_flash;
        eh::render_weapon(gs, buffer.framebuffer);

        REQUIRE(buffer.guards_intact());
        return buffer.pixels();
    };

    struct Glow {
        std::size_t translucent = 0;
        int peak_strength = -1;
    };

    auto glow_of = [&](uint16_t muzzle_flash) {
        const std::vector<uint32_t> light = render_over(OVER_LIGHT, muzzle_flash);
        const std::vector<uint32_t> dark = render_over(OVER_DARK, muzzle_flash);

        Glow glow;
        for (std::size_t i = 0; i < GuardedFramebuffer::PIXEL_COUNT; ++i) {
            // A pixel the weapon never touched still holds the two different
            // backgrounds, so "differs between renders" is not by itself
            // evidence of translucency. Only pixels the weapon wrote count.
            if (light[i] == OVER_LIGHT && dark[i] == OVER_DARK) {
                continue;
            }
            if (light[i] == dark[i]) {
                continue;
            }
            ++glow.translucent;
            const int light_red = static_cast<int>(light[i] & 0xffu);
            const int dark_red = static_cast<int>(dark[i] & 0xffu);
            glow.peak_strength = std::max(glow.peak_strength, 255 - (light_red - dark_red));
        }
        return glow;
    };

    // Control. Everything else the weapon draws is opaque, so an unfired weapon
    // must render identically over both backgrounds. Without this, a stray
    // translucent element elsewhere on the gun could carry the assertions below
    // while the flash itself was gone.
    const Glow idle = glow_of(0);
    CAPTURE(idle.translucent, idle.peak_strength);
    REQUIRE(idle.translucent == 0);

    // The glow must weaken on every tick of the countdown. Strict monotonicity is
    // the contract "it fades" -- but ordering pins a sequence, not a scale, so it
    // is paired below with two magnitude bounds. Measured against this test as it
    // originally stood, three separate ways of ruining the ramp still fade and so
    // still passed it: compressing 0.35f -> 0.10f (37 38 40 41), dimming the glow
    // alpha 105 -> 60 (23 26 28 31), and lowering the ramp's base while keeping
    // its slope (21 26 31 35).
    std::vector<int> strengths;
    for (uint16_t ticks = 1; ticks <= eh::MUZZLE_FLASH_TICKS; ++ticks) {
        const Glow glow = glow_of(ticks);
        INFO("muzzle_flash := " << ticks << " translucent := " << glow.translucent);
        CHECK(glow.translucent > 0);
        CHECK(glow.peak_strength > 0);
        strengths.push_back(glow.peak_strength);
    }

    // Measured on a correct build: 40, 45, 50, 55. Holding the strength constant
    // pins all four at 55.
    for (std::size_t i = 1; i < strengths.size(); ++i) {
        INFO("strength at " << i << " ticks := " << strengths[i - 1] << ", at " << (i + 1)
                            << " ticks := " << strengths[i]);
        CHECK(strengths[i] > strengths[i - 1]);
    }

    // Magnitude, because a fade that is merely ordered is not a fade anyone sees.
    // Both are bounds with measured headroom rather than pinned values, so the
    // ramp can still be retuned; what they refuse is a ramp retuned into a token.
    //
    // A fresh shot has to actually burn. This is what a dimmed glow fails, whether
    // the alpha is scaled down (31) or the ramp's whole band is lowered (35),
    // both of which keep fading perfectly and pass every ordering check.
    INFO("peak strength at a fresh shot := " << strengths.back());
    CHECK(strengths.back() >= 48); // measured 55

    // ...and it has to travel a real distance while dying. This is what a
    // compressed ramp fails: flattened toward its own peak it reads 51 52 53 55,
    // which is still strictly increasing and still starts at full brightness.
    INFO("travel from stale to fresh := " << (strengths.back() - strengths.front()));
    CHECK(strengths.back() - strengths.front() > 10); // measured 15

    // player.cpp only ever sets this timer to MUZZLE_FLASH_TICKS and decrements
    // it, so the renderer's clamp is defensive rather than reachable today. It
    // is still worth pinning: it is what keeps a future change to how the timer
    // is armed from producing a glow brighter than a fresh shot.
    const Glow fresh = glow_of(eh::MUZZLE_FLASH_TICKS);
    const Glow overlong = glow_of(static_cast<uint16_t>(eh::MUZZLE_FLASH_TICKS * 2));
    CAPTURE(fresh.peak_strength, overlong.peak_strength);
    CHECK(overlong.peak_strength == fresh.peak_strength);
}

// dimensions_for() in sprites.cpp is a second switch over EntityType, independent of the one that
// picks the artwork, and nothing ties them: exchanging any two of its four entries passed 121/121.
// An egg would be drawn at the basket's proportions, a pickup at another pickup's, and no test
// would notice. Sizes compared only against each other cannot see an exchange, because the set of
// sizes is unchanged and only their owners move - so these assertions say which silhouette belongs
// to which entity, rather than that the four differ.
TEST_CASE("sprites: each entity type keeps its own silhouette") {
    struct Rendered {
        int width;
        int height;
    };

    const auto measure = [](eh::EntityType type) {
        GuardedFramebuffer buffer;
        eh::GameState state = state_facing_east();
        state.entities.push_back(entity_at(1, type, 3.0f, 0.0f));
        eh::render_sprites(state, buffer.framebuffer);

        const PixelBounds bounds = modified_bounds(buffer);
        REQUIRE(buffer.guards_intact());
        REQUIRE(bounds.valid());
        return Rendered{bounds.right - bounds.left + 1, bounds.bottom - bounds.top + 1};
    };

    const Rendered egg = measure(eh::EntityType::Egg);
    const Rendered jellybean = measure(eh::EntityType::Jellybean);
    const Rendered carrot = measure(eh::EntityType::Carrot);
    const Rendered basket = measure(eh::EntityType::Basket);
    CAPTURE(egg.width, egg.height, jellybean.width, jellybean.height, carrot.width, carrot.height,
            basket.width, basket.height);

    SECTION("the basket is the only sprite wider than it is tall") {
        CHECK(basket.width > basket.height);
        CHECK(egg.height > egg.width);
        CHECK(carrot.height > carrot.width);
        CHECK(jellybean.height > jellybean.width);
    }

    SECTION("seen from one distance the four keep their relative heights") {
        // Measured 155 / 132 / 108 / 55 pixels at three tiles ahead. Ordering alone is too coarse
        // to state this: exchanging the jellybean's and the carrot's entries leaves them 81 and 75
        // pixels tall, still in order, the margin collapsed from 53 pixels to 6. The magnitude is
        // the contract - a carrot reads as roughly twice a jellybean, and the exchange makes that
        // 1.08x. Ratios rather than pixel counts, so a deliberate change to the sprite projection
        // scale (which test_render.cpp bounds) leaves this green, while one entity's own
        // proportions moving does not.
        //
        // Width cannot carry any of it: the jellybean and the carrot both draw 46 pixels wide,
        // because the bounding box measures the artwork rather than the quad it is sampled into.
        const double egg_over_basket = static_cast<double>(egg.height) / basket.height;
        const double basket_over_carrot = static_cast<double>(basket.height) / carrot.height;
        const double carrot_over_jellybean = static_cast<double>(carrot.height) / jellybean.height;
        CAPTURE(egg_over_basket, basket_over_carrot, carrot_over_jellybean);

        CHECK(egg_over_basket == Catch::Approx(1.174).margin(0.15));
        CHECK(basket_over_carrot == Catch::Approx(1.222).margin(0.15));
        CHECK(carrot_over_jellybean == Catch::Approx(1.964).margin(0.15));
    }
}

// The basket is the second half of the objective: mvp.md line 13 is "crack all five eggs, then
// reach the Basket to win". `render_sprites` gates a golden halo and a size throb on
// `gs.eggs_remaining == 0`, animated by `gs.tick` -- which makes them the game's only cue that the
// goal has changed from hunting to going home. Both were unpinned:
//
//   basket never activates even with every egg dead   -> 124/124 passed
//   pulse frozen (lit, but no throb and no animation) -> 124/124 passed
//
// The sprite suite asserts occlusion, ordering, bounds, inverse-distance size and per-type
// silhouette. Every one of those holds on a basket that never lights up, because they are all
// properties of a single static render.
//
// Measured on the shipped build, basket alone 3 tiles dead ahead, sweeping ticks 0..39 (one pulse
// period is ~35 ticks): with an egg still loose, all 39 later frames are bit-identical to tick 0
// and the halo is absent. With every egg cracked, all 39 differ, drawn height swings 134..157 and
// the halo's peak alpha swings 18..51. The assertions below use margins well inside those.
//
// The halo is separated from the opaque wickerwork by rendering over two backgrounds and keeping
// only pixels the sprite wrote whose value depends on what was underneath (finding #45). No sprite
// geometry or pulse arithmetic is reproduced, so retuning the period or the halo colour leaves this
// green while switching either channel off does not.
TEST_CASE("sprites: the basket stays inert until the last egg cracks, then pulses") {
    const auto render_basket = [](uint16_t eggs_remaining, uint32_t tick, uint32_t background) {
        std::vector<uint32_t> pixels(
            static_cast<std::size_t>(eh::Framebuffer::W) * eh::Framebuffer::H, background);
        std::array<float, eh::Framebuffer::W> depth{};
        depth.fill(1000.0f);
        eh::Framebuffer framebuffer{pixels.data(), depth.data()};

        eh::GameState state = state_facing_east();
        state.eggs_remaining = eggs_remaining;
        state.tick = tick;
        state.entities.push_back(entity_at(1, eh::EntityType::Basket, 3.0f, 0.0f));
        eh::render_sprites(state, framebuffer);
        return pixels;
    };

    const auto drawn_height = [](const std::vector<uint32_t> &pixels, uint32_t background) {
        int top = eh::Framebuffer::H;
        int bottom = -1;
        for (int y = 0; y < eh::Framebuffer::H; ++y) {
            for (int x = 0; x < eh::Framebuffer::W; ++x) {
                if (pixels[static_cast<std::size_t>(y) * eh::Framebuffer::W +
                           static_cast<std::size_t>(x)] != background) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                    break;
                }
            }
        }
        return bottom >= top ? bottom - top + 1 : 0;
    };

    // Two backgrounds 255 apart in red only. A pixel the sprite wrote that still differs between
    // the two renders was blended rather than painted. Over the black render such a pixel's red
    // channel is src_red * alpha / 255 with no contribution from the background, so the strongest
    // survivor is the halo's alpha and nothing else.
    //
    // Counting translucent pixels instead would NOT work: measured, that count tracks the quad's
    // projected area, so freezing `basket_scale` alone drives it to zero swing while the alpha is
    // still animating. It reads like a brightness metric and is really a second size metric.
    const auto halo_peak = [&](uint16_t eggs_remaining, uint32_t tick) {
        const uint32_t dark = 0xff000000u;
        const uint32_t lit = 0xff0000ffu;
        const std::vector<uint32_t> over_dark = render_basket(eggs_remaining, tick, dark);
        const std::vector<uint32_t> over_lit = render_basket(eggs_remaining, tick, lit);
        int peak = 0;
        for (std::size_t i = 0; i < over_dark.size(); ++i) {
            if (over_dark[i] == dark || over_lit[i] == lit) {
                continue; // never written; still holds two different backgrounds
            }
            if ((over_dark[i] & 0xffu) == (over_lit[i] & 0xffu)) {
                continue; // fully opaque art
            }
            peak = std::max(peak, static_cast<int>(over_dark[i] & 0xffu));
        }
        return peak;
    };

    const int SWEEP_TICKS = 40;

    SECTION("while an egg is still loose the basket does not move at all") {
        const std::vector<uint32_t> inert = render_basket(1, 0, BACKGROUND);
        // Non-vacuity: a basket that was never drawn would satisfy "every frame is identical".
        REQUIRE(drawn_height(inert, BACKGROUND) > 100);

        int moved = 0;
        for (uint32_t tick = 1; tick < static_cast<uint32_t>(SWEEP_TICKS); ++tick) {
            if (render_basket(1, tick, BACKGROUND) != inert) {
                ++moved;
            }
        }
        CAPTURE(moved);
        CHECK(moved == 0);
    }

    SECTION("cracking the last egg lights the basket's halo") {
        for (uint32_t tick : {0u, 9u, 17u, 26u, 35u}) {
            CAPTURE(tick);
            const std::vector<uint32_t> quiet = render_basket(1, tick, BACKGROUND);
            const std::vector<uint32_t> live = render_basket(0, tick, BACKGROUND);
            int differing = 0;
            for (std::size_t i = 0; i < quiet.size(); ++i) {
                if (quiet[i] != live[i]) {
                    ++differing;
                }
            }
            const int quiet_halo = halo_peak(1, tick);
            const int live_halo = halo_peak(0, tick);
            CAPTURE(differing, quiet_halo, live_halo);
            CHECK(differing > 5000);
            CHECK(live_halo - quiet_halo >= 8);
        }
    }

    SECTION("the live basket throbs in size and its halo brightens and dims") {
        int shortest = eh::Framebuffer::H;
        int tallest = 0;
        int dimmest = std::numeric_limits<int>::max();
        int brightest = 0;
        for (uint32_t tick = 0; tick < static_cast<uint32_t>(SWEEP_TICKS); ++tick) {
            const int height = drawn_height(render_basket(0, tick, BACKGROUND), BACKGROUND);
            const int halo = halo_peak(0, tick);
            shortest = std::min(shortest, height);
            tallest = std::max(tallest, height);
            dimmest = std::min(dimmest, halo);
            brightest = std::max(brightest, halo);
        }
        CAPTURE(shortest, tallest, dimmest, brightest);
        // Two independent channels: `basket_scale` sizes the quad, `basket_pulse` sets the halo's
        // alpha. Verified by mutation that freezing either one alone fails exactly one of these.
        CHECK(tallest - shortest >= 10);
        CHECK(brightest - dimmest >= 12);
    }
}

// Finding #57. `render_sprites` skips any entity whose `alive` flag is clear (sprites.cpp:470).
// That one line is the game's primary visual kill feedback -- finding #31 explicitly rests on it,
// arguing that a fatal hit's retained flash is invisible "because render_sprites culls !alive and
// the egg blinks out the frame it dies". Absence *is* the death animation, and absence is also how
// a collected carrot leaves the floor.
//
// Disabling the cull leaves every cracked egg standing as a corpse forever and every collected
// pickup lying where it was taken. It was caught -- by exactly one test, for the wrong reason:
// `render: the animation clock advances with simulation time and reaches the screen` died at
// `REQUIRE(basket_pixels > 5000)` reading 0, because five corpses stood between that fixture's
// camera and the basket. That is a non-vacuity guard in a test about the clock, and finding #24's
// caution applies: a mutant dying is not evidence the contract is covered. Repositioning that
// unrelated fixture would silence this regression completely.
//
// The two sections are separated by entity type on purpose, so a cull narrowed to one kind of
// entity fails only its own section rather than both firing off one cause.
TEST_CASE("sprites: a cracked egg and a collected pickup leave the screen entirely") {
    // Control: with nothing alive to draw, the renderer must leave the frame untouched. Without
    // it, "the dead entity drew nothing" is satisfied by a renderer that draws nothing at all.
    GuardedFramebuffer empty;
    eh::GameState bare = state_facing_east();
    eh::render_sprites(bare, empty.framebuffer);
    REQUIRE(empty.pixels_unchanged());

    eh::GameState gs = state_facing_east();
    eh::EntityType type = eh::EntityType::Egg;

    SECTION("a cracked egg") { type = eh::EntityType::Egg; }
    SECTION("a collected carrot") { type = eh::EntityType::Carrot; }

    gs.entities.push_back(entity_at(1, type, 3.0f, 0.0f));

    GuardedFramebuffer live;
    eh::render_sprites(gs, live.framebuffer);
    const std::vector<uint32_t> live_pixels = live.pixels();
    int drawn = 0;
    for (uint32_t pixel : live_pixels) {
        if (pixel != BACKGROUND) {
            ++drawn;
        }
    }

    // Non-vacuity: the entity must genuinely be on screen while alive, or its disappearance is
    // not evidence of anything.
    CAPTURE(drawn);
    REQUIRE(drawn > 500);

    gs.entities[0].alive = false;

    GuardedFramebuffer dead;
    eh::render_sprites(gs, dead.framebuffer);
    CHECK(dead.pixels_unchanged());
    CHECK(dead.guards_intact());
}
TEST_CASE("sprites: the weapon is anchored to the bottom edge and inset from both sides") {
    // Every other weapon placement assertion in this suite is RELATIVE: the bob tests
    // compare bounds.left against a rest render, the dip test compares bounds.top against
    // a rest render, and the muzzle-flash reach test compares a firing silhouette against
    // an idle one. Translating the whole weapon by a constant offset moves the subject and
    // its own baseline together, so it cancels out of every one of those deltas. Measured:
    // drawing the weapon hard against the left edge passed 134/134, and drawing it at the
    // top of the screen tripped only the HUD-ordering test, which is about layer order and
    // died there for an unrelated reason. The gun's absolute screen anchor was pinned by
    // nothing.
    //
    // The bounds below are measured across the whole reachable state space -- bob and the
    // muzzle-flash recoil are the only two inputs that move the weapon -- and no origin
    // arithmetic from sprites.cpp is reproduced here.
    struct Pose {
        const char *label;
        float bob;
        uint16_t muzzle_flash;
    };
    const std::array<Pose, 8> poses{Pose{"at rest", 0.0f, 0},
                                    Pose{"bob quarter", 0.25f, 0},
                                    Pose{"bob half", 0.5f, 0},
                                    Pose{"bob three quarters", 0.75f, 0},
                                    Pose{"firing", 0.0f, eh::MUZZLE_FLASH_TICKS},
                                    Pose{"firing + bob quarter", 0.25f, eh::MUZZLE_FLASH_TICKS},
                                    Pose{"firing + bob half", 0.5f, eh::MUZZLE_FLASH_TICKS},
                                    Pose{"fading shot", 0.75f, 1}};

    SECTION("anchored to the bottom edge") {
        for (const Pose &pose : poses) {
            eh::GameState state = state_facing_east();
            state.player.bob = eh::fx_from_float(pose.bob);
            state.muzzle_flash = pose.muzzle_flash;

            auto buffer = std::make_unique<GuardedFramebuffer>();
            eh::render_weapon(state, buffer->framebuffer);
            const PixelBounds bounds = modified_bounds(*buffer);

            INFO("pose: " << pose.label);
            REQUIRE(buffer->guards_intact());
            REQUIRE(bounds.valid());
            // Non-vacuity: without it, "no weapon pixel lands up there" is satisfied by a
            // weapon that draws nothing at all. Measured 16038-18269.
            REQUIRE(drawn_pixel_count(*buffer) > 5000);

            // Measured: bottom is H-1 in every reachable pose, and top ranges 187..203.
            CHECK(bounds.bottom >= eh::Framebuffer::H - 2);
            CHECK(bounds.top > eh::Framebuffer::H / 3);
        }
    }

    SECTION("inset from both side edges") {
        for (const Pose &pose : poses) {
            eh::GameState state = state_facing_east();
            state.player.bob = eh::fx_from_float(pose.bob);
            state.muzzle_flash = pose.muzzle_flash;

            auto buffer = std::make_unique<GuardedFramebuffer>();
            eh::render_weapon(state, buffer->framebuffer);
            const PixelBounds bounds = modified_bounds(*buffer);

            INFO("pose: " << pose.label);
            REQUIRE(buffer->guards_intact());
            REQUIRE(bounds.valid());
            REQUIRE(drawn_pixel_count(*buffer) > 5000);

            // Measured: left 215..261, right 488..507 on a 640-wide screen.
            CHECK(bounds.left > eh::Framebuffer::W / 8);
            CHECK(bounds.right < eh::Framebuffer::W - eh::Framebuffer::W / 8);
        }
    }

    SECTION("held on the right-hand side of the view") {
        // Characterization, not endorsement: the weapon sits deliberately right of centre,
        // the classic first-person arrangement. Measured centroid 366..399 against a screen
        // centre of 320. Moving it to the left hand is a legitimate art decision -- this
        // makes it a deliberate one rather than a silent drift, in the shape the RNG
        // invariant established.
        for (const Pose &pose : poses) {
            eh::GameState state = state_facing_east();
            state.player.bob = eh::fx_from_float(pose.bob);
            state.muzzle_flash = pose.muzzle_flash;

            auto buffer = std::make_unique<GuardedFramebuffer>();
            eh::render_weapon(state, buffer->framebuffer);

            INFO("pose: " << pose.label);
            REQUIRE(buffer->guards_intact());
            REQUIRE(drawn_pixel_count(*buffer) > 5000);

            CHECK(drawn_centroid_x(*buffer) > eh::Framebuffer::W / 2);
        }
    }
}
