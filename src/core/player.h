#pragma once

namespace eh {

struct GameState;
struct InputFrame;

void player_tick(GameState &, const InputFrame &);
void fire(GameState &);

} // namespace eh
