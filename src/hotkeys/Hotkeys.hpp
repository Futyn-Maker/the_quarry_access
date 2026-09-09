#pragma once
// Mod hotkeys: keyboard (F5 repeat, F6 read screen, F7 stop, F8 help, F9 subtitle
// reading on/off, F4 last subtitle line, dev keys) and a gamepad chord (hold
// Back/Select + D-pad or a stick click).
// Keyboard keys are polled on the game thread with GetAsyncKeyState while the
// game window is in the foreground; the gamepad chord is polled through the
// player controller.

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
        DevDumpTree,
        DevTrace,
        DevLogLevel,
    };

    // Parses the "Ctrl+F9" style bindings from the ini and registers the pollers.
    void Install();

    // Runs a command (game thread).
    void Run(Command command);

    std::wstring CommandName(Command command);
}
