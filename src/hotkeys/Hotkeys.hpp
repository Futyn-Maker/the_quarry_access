#pragma once
// Mod hotkeys: keyboard (F5 repeat, F6 read screen, F7 stop, F8 help, F9 subtitle
// reading on/off, F4 last subtitle line, F3 speech through the screen reader or SAPI, N/P
// next and previous exploration target, T beacon, K the tarot card places, dev keys) and
// gamepad chords (hold Back and press a face button, the right bumper, the right trigger
// or the right stick).
// Keyboard keys are taken by the mod's own low-level keyboard hook ahead of the screen
// reader and the game (KeyHook.hpp), and polled with GetAsyncKeyState only when the hook
// could not be set; the gamepad is read through the same XInput the game reads, so the
// chords answer in the menus as well as in play.

#include <string>

namespace qa::hotkeys
{
    enum class Command
    {
        Repeat,
        ReadScreen,
        Stop,
        Help,
        Subtitles,
        LastSubtitle,
        Speech,
        Verbosity,
        NextTarget,
        PreviousTarget,
        Where,
        Walk,
        Beacon,
        TarotCards,
        DevDumpTree,
        DevTrace,
        DevLogLevel,
    };

    // Where a command came from, which is the device its answer is worded for.
    enum class Source
    {
        Keyboard,
        Gamepad,
        Other,
    };

    // Parses the "Ctrl+F9" style bindings from the ini and registers the pollers.
    void Install();
    // Takes the keyboard hook down, for shutdown.
    void Uninstall();

    // Runs a command (game thread).
    void Run(Command command, Source source = Source::Other);

    std::wstring CommandName(Command command);
}
