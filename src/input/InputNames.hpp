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

    // The gamepad as the game reads it: the buttons, triggers and sticks of the first pad
    // that answers, taken through the same XInput the game reads, so it is read in the menus
    // as well as in play. (The player controller stops answering for keys while a menu has
    // the input, which is why the mod's buttons are not asked of it.)
    struct PadReading
    {
        bool valid = false;
        unsigned buttons = 0;
        int leftTrigger = 0;
        int rightTrigger = 0;
        int leftX = 0;
        int leftY = 0;
        int rightX = 0;
        int rightY = 0;
    };
    PadReading ReadPad();

    // Whether the button, trigger or stick direction an engine key name stands for
    // ("Gamepad_FaceButton_Bottom", "Gamepad_RightTrigger", "Gamepad_RightStick_Up") is down
    // in a reading.
    bool PadKeyDown(const PadReading& pad, std::wstring_view keyName);

    // The button the mod's chords are held with. While it is down the game is shown a pad
    // with nothing pressed, and whatever was pressed under it stays hidden until it is let go
    // of, so a chord is never also a press for the game.
    void SetChordHold(std::wstring_view keyName);

    // The device the player's last mod hotkey came from is the device the answer is worded
    // for: for a moment after the press the scheme reads as that device.
    void NoteHotkeyDevice(Scheme scheme);

    // Points the game's own reading of the gamepad through the mod, so that a walk can push
    // the stick the game already reads instead of pressing keys, which would make the game
    // treat the keyboard as the device in hand and redraw every prompt for it.
    void ShareGamepadReading();

    // Holds down the game's own movement keys for a heading given in the camera's frame,
    // both from -1 to 1, exactly as a player pressing them would. False when the keys cannot
    // be held, which is the case whenever the game is not the window in front.
    bool HoldWalkKeys(double forward, double right);
    // Lets go of whatever HoldWalkKeys is holding.
    void ReleaseWalkKeys();
    // Lets go and gives the game's own gamepad reading back to it, for shutdown.
    void ForgetWalkKeys();
    // The four keys the character walks with, for the log.
    std::wstring DescribeWalkKeys();
}
