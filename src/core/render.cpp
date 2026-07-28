#include "core/framebuffer.h"

#include "core/hud.h"
#include "core/raycast.h"
#include "core/sprites.h"

namespace eh {

// Foundation-owned orchestration: downstream renderers fill these four stages only.
void render_frame(const GameState &gs, Framebuffer &fb) {
    render_walls(gs, fb);
    render_sprites(gs, fb);
    render_weapon(gs, fb);
    render_hud(gs, fb);
}

} // namespace eh
