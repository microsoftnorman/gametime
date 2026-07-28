#pragma once

#include <cstdint>

namespace eh {

struct InputFrame {
    int8_t move_x = 0;    // -1 left, +1 right (strafe)
    int8_t move_y = 0;    // -1 back, +1 forward
    int8_t turn = 0;      // keyboard turn, -1/0/+1
    int16_t mouse_dx = 0; // integer mouse counts this tick
    uint8_t buttons = 0;  // bitmask
    enum Button : uint8_t { Fire = 1, Sprint = 2, Restart = 4, Start = 8 };
    bool held(Button b) const { return (buttons & b) != 0; }
};

} // namespace eh
