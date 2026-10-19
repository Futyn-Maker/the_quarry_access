#pragma once
// The mod's switch. Off, the mod says nothing and plays no sound, the keys it takes only in
// play go back to the game and a walk ends; its watchers run on, so that the game's state is
// current when it is switched on again and the screen can be read at once. The speech and
// sound modules ask it, so no feature has to. It is never kept across sessions: the mod
// starts switched on.

namespace qa::gate
{
    bool IsOn();
    void Set(bool on);
}
