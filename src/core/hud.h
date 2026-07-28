#pragma once

namespace eh {

struct Framebuffer;
struct GameState;

void render_hud(const GameState &, Framebuffer &);

} // namespace eh
