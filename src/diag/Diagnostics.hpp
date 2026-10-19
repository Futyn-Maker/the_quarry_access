#pragma once
// Development diagnostics and the user-facing "read screen"/"help" readouts.

#include "core/Log.hpp"

#include <string>

namespace qa::diag
{
    // Writes every visible user widget with its texts and all HUD instances to
    // Mods\QuarryAccess\dumps\screen-<timestamp>.txt and to the log.
    void DumpScreen();

    void ToggleTrace();
    // Sets the log level and keeps it in the ini for the next session.
    void SetLogLevel(log::Level level);
    void CycleLogLevel();

    // Composes what F6 says: screen, focused element, prompts, mechanic state, last subtitle.
    std::wstring ReadScreen();
    // The same without the fallbacks for an empty screen: what the features describe and
    // the HUD shows, or nothing.
    std::wstring Readout();

    // Composes what F8 says.
    std::wstring Help();

    void SetModDir(const std::wstring& modDir);

    // Developer command file: Mods\QuarryAccess\command.txt is polled twice a second;
    // each line is executed and the file is deleted. Commands: "trace on|off",
    // "dump", "loglevel Error|Info|Verbose|Trace", "say <text>", "read", "help",
    // "onoff" (the mod's switch, as F1), "props <object path or class name>".
    void InstallCommandFile();

    // Writes every property of an object to the log (by full path, or the first live
    // instance of a class).
    void DumpProperties(const std::wstring& what);
}
