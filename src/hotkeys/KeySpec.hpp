#pragma once
// The spelling of a keyboard key in QuarryAccess.ini: "F5", "Ctrl+F9", "N", "numpad5",
// "space", with the modifiers Ctrl, Alt and Shift in any order before the key, and in any
// case.

#include <string_view>

namespace qa::hotkeys
{
    struct KeySpec
    {
        int vk = 0; // the virtual-key code
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
    };

    // False for an empty spelling, for "none" and for a key it does not know; the modifiers
    // alone name no key.
    bool ParseKeySpec(std::wstring_view spec, KeySpec& out);
}
