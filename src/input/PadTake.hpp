#pragma once
// The game-free part of the buttons the mod takes from the game on one pad (input/InputNames):
// a button that goes down while it is taken is the mod's press, and the game is shown it up
// from that reading until it is let go, even when the taking has ended in between, so the game
// never sees half a press. A button already down when the taking begins stays the game's until
// it is let go.

#include <cstdint>

namespace qa::input
{
    struct PadTake
    {
        uint16_t last = 0; // what the pad showed at the last reading
        uint16_t held = 0; // the buttons the mod took and that are not let go yet
    };

    // One reading of the pad: `buttons` is what it shows (XInput's button bits), `taken` the
    // buttons the mod takes now. Returns the buttons the game is shown, and sets `pressed` to
    // the taken buttons that went down on this reading.
    uint16_t Take(PadTake& state, uint16_t buttons, uint16_t taken, uint16_t& pressed);
}
