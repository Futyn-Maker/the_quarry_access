#pragma once
// Mod hotkeys: keyboard (F5 repeat, F6 read screen, F7 stop, F8 help, F9 subtitle
// reading on/off, F4 last subtitle line, N/P next and previous exploration target, T
// beacon, dev keys) and gamepad chords (hold Back and press a face button, the right
// bumper, the right trigger or the right stick).
// Keyboard keys are polled on the game thread with GetAsyncKeyState while the
// game window is in the foreground; the gamepad is read through the same XInput the
// game reads, so the chords answer in the menus as well as in play.

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
        NextTarget,
        PreviousTarget,
        Where,
        Walk,
        Beacon,
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

    // Runs a command (game thread).
    void Run(Command command, Source source = Source::Other);

    std::wstring CommandName(Command command);
}
