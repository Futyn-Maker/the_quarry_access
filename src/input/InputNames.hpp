#pragma once
// Turns input action names ("UINavigationCancel") into spoken key names for the
// control scheme the player is using (keyboard/mouse or gamepad).

#include <string>
#include <string_view>

namespace qa::input
{
    enum class Scheme
    {
        Unknown,
        Gamepad,
        MouseKeyboard,
        Touch,
    };

    Scheme CurrentScheme();
    std::wstring SchemeName(Scheme scheme);

    // Spoken name of the key bound to an action for the current scheme, e.g. "Backspace" / "B".
    // Empty when the action is unknown.
    std::wstring KeyForAction(std::wstring_view actionName);

    // Spoken name of an engine key name ("SpaceBar" -> "Space", "Gamepad_FaceButton_Bottom" -> "A").
    std::wstring KeyDisplayName(std::wstring_view keyName);

    // Localized name of the pointing device for the scheme ("mouse" / "right stick").
    std::wstring PointerName();

    void InvalidateCache();
}
