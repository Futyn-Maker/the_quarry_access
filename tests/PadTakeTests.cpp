// input/PadTake: the buttons the mod takes from the game on one pad, worked out reading by
// reading.
#include "Support.hpp"
#include "input/PadTake.hpp"

using qa::input::PadTake;
using qa::input::Take;

namespace
{
    constexpr uint16_t kA = 0x1000;
    constexpr uint16_t kB = 0x2000;
    constexpr uint16_t kUp = 0x0001;
}

TEST_CASE("A taken button that goes down is the mod's press, once, and the game sees it up until it is let go")
{
    PadTake state;
    uint16_t pressed = 0;
    CHECK(Take(state, kA, kA, pressed) == 0);
    CHECK(pressed == kA);
    CHECK(Take(state, kA, kA, pressed) == 0);
    CHECK(pressed == 0);
    CHECK(Take(state, 0, kA, pressed) == 0);
    CHECK(pressed == 0);
    CHECK(Take(state, kA, kA, pressed) == 0);
    CHECK(pressed == kA);
}

TEST_CASE("A press stays hidden after the taking ends, until it is let go")
{
    PadTake state;
    uint16_t pressed = 0;
    Take(state, kA, kA, pressed);
    CHECK(Take(state, kA, 0, pressed) == 0);
    CHECK(pressed == 0);
    CHECK(Take(state, 0, 0, pressed) == 0);
    CHECK(Take(state, kA, 0, pressed) == kA);
    CHECK(pressed == 0);
}

TEST_CASE("A button already down when the taking begins stays the game's until it is let go")
{
    PadTake state;
    uint16_t pressed = 0;
    CHECK(Take(state, kA, 0, pressed) == kA);
    CHECK(Take(state, kA, kA, pressed) == kA);
    CHECK(pressed == 0);
    CHECK(Take(state, 0, kA, pressed) == 0);
    CHECK(Take(state, kA, kA, pressed) == 0);
    CHECK(pressed == kA);
}

TEST_CASE("Buttons that are not taken go to the game, and two taken at once are both pressed")
{
    PadTake state;
    uint16_t pressed = 0;
    CHECK(Take(state, kB, kA, pressed) == kB);
    CHECK(pressed == 0);
    CHECK(Take(state, static_cast<uint16_t>(kB | kA | kUp), static_cast<uint16_t>(kA | kUp), pressed) == kB);
    CHECK(pressed == static_cast<uint16_t>(kA | kUp));
}
