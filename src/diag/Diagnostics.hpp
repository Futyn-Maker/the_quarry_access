#pragma once
// Development diagnostics and the user-facing "read screen"/"help" readouts.

#include <string>

namespace qa::diag
{
    // Writes every visible user widget with its texts and all HUD instances to
    // Mods\QuarryAccess\dumps\screen-<timestamp>.txt and to the log.
    void DumpScreen();

    void ToggleTrace();
    void CycleLogLevel();

    // Composes what F6 says: screen, focused element, prompts, mechanic state, last subtitle.
    std::wstring ReadScreen();

    // Composes what F8 says.
    std::wstring Help();

    void SetModDir(const std::wstring& modDir);
}
