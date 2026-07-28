#pragma once

namespace eh {

struct Framebuffer;
struct GameState;

void render_walls(const GameState &, Framebuffer &);

} // namespace eh
