#pragma once
// The game-free part of the mod's own list for a dropdown (features/Menus): the option the
// list opens on and the option each of its keys takes it to.

#include <cstdint>

namespace qa::features::dropdown
{
    enum class Step
    {
        Previous,
        Next,
        First,
        Last,
    };

    // The option the list opens on, out of `count`: the one the dropdown shows, or the first
    // when it shows none. A dropdown with no options has no list, and the answer is -1.
    int64_t Opened(int64_t shown, int64_t count);
    // The option a step takes the list to from `index`, out of `count`. The first and the last
    // hold, so a step past either stays on it. -1 for a dropdown with no options.
    int64_t Moved(int64_t index, Step step, int64_t count);
}
