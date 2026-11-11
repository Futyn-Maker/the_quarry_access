#include "input/PadTake.hpp"

namespace qa::input
{
    uint16_t Take(PadTake& state, uint16_t buttons, uint16_t taken, uint16_t& pressed)
    {
        pressed = static_cast<uint16_t>(buttons & ~state.last & taken);
        state.held = static_cast<uint16_t>((state.held | pressed) & buttons);
        state.last = buttons;
        return static_cast<uint16_t>(buttons & ~state.held);
    }
}
