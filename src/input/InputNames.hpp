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

    // Where the keys of an action come from and what is spoken, for the log.
    std::wstring DescribeAction(std::wstring_view actionName);
    // Spoken name of an engine key name ("SpaceBar" -> "Space", "Gamepad_FaceButton_Bottom" -> "A").
    std::wstring KeyDisplayName(std::wstring_view keyName);

    // Localized name of the pointing device for the scheme ("mouse" / "right stick").
    std::wstring PointerName();

    void InvalidateCache();

    // Watches the keyboard, mouse and gamepad so speech can tell what the player asked for
    // from what the game said on its own: only the former is allowed to interrupt.
    void InstallActivityTracker();

    // Milliseconds since the player last pressed, held or moved anything. A large number
    // when the game does not have the focus or nothing has been touched yet.
    long long MsSinceInput();

    // True while a key, button or stick is held down.
    bool InputHeld();

    // True while the player is holding the character's own movement keys or pushing the
    // left stick, whichever the game has them bound to. Keys the mod itself is holding do
    // not count.
    bool MovementHeld();

    // Holds down the game's own movement keys for a heading given in the camera's frame,
    // both from -1 to 1, exactly as a player pressing them would. False when the keys cannot
    // be held, which is the case whenever the game is not the window in front.
    bool HoldWalkKeys(double forward, double right);
    // Lets go of whatever HoldWalkKeys is holding.
    void ReleaseWalkKeys();
    // The four keys the character walks with, for the log.
    std::wstring DescribeWalkKeys();
}
