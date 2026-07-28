#pragma once

namespace eh {

struct Framebuffer;
struct GameState;

void render_sprites(const GameState &, Framebuffer &);
void render_weapon(const GameState &, Framebuffer &);

} // namespace eh
