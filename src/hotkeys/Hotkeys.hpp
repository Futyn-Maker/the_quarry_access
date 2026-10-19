#pragma once
// Mod hotkeys: the keys of hotkeys/HotkeyTable on the keyboard (F5 repeat, F6 read screen,
// F7 stop, F8 help, F9 subtitle reading on/off, F4 last subtitle line, F3 speech through
// the screen reader or SAPI, F2 verbosity, F1 the mod off and on, N/P next and previous
// exploration target, T beacon, K the tarot card places, dev keys) and as gamepad chords
// (hold Back and press a face button, a bumper, a trigger, Start or a stick).
// Keyboard keys are taken by the mod's own low-level keyboard hook ahead of the screen
// reader and the game (KeyHook.hpp), and polled with GetAsyncKeyState only when the hook
// could not be set; the gamepad is read through the same XInput the game reads, so the
// chords answer in the menus as well as in play. While the mod is switched off (core/Gate)
// only its switch answers.

#include "hotkeys/HotkeyTable.hpp"

#include <string>

namespace qa::hotkeys
{
    // Where a command came from, which is the device its answer is worded for.
    enum class Source
    {
        Keyboard,
        Gamepad,
        Other,
    };

    // Reads the bindings of the table from the ini and registers the pollers.
    void Install();
    // Takes the keyboard hook down, for shutdown.
    void Uninstall();

    // Runs a command (game thread).
    void Run(Command command, Source source = Source::Other);

    // The keys a command is on, as the settings spell them: the keyboard key ("F5",
    // "Ctrl+F9"), the button pressed under the chord hold and the pad button answered
    // without the chord in the command's own context (engine key names); empty for none.
    std::wstring KeyOf(Command command);
    std::wstring ChordOf(Command command);
    std::wstring PadOf(Command command);
    // The button held for the chords; empty when the chords are off.
    std::wstring ChordHold();
    // True when the help is worded for the gamepad: the player's last device, with the chords on.
    bool ChordsInHand();
    // The spoken name of a command's key for the device in the player's hands: the pad
    // button's name after a gamepad press where the command has one, else the keyboard key as
    // spelled.
    std::wstring SpokenKey(Command command);
}
