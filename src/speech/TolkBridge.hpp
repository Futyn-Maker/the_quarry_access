#pragma once
// Dynamic binding to Tolk.dll (screen-reader abstraction: NVDA, JAWS...). Tolk.dll and
// its driver DLLs must sit next to the game executable. SAPI is not Tolk's here: the mod
// speaks through SAPI itself (Sapi.hpp).

#include <string>

namespace qa::tolk
{
    bool Load();
    void Unload();
    bool IsLoaded();

    // Speech + braille through the active screen reader (Tolk_Output).
    bool Output(const wchar_t* text, bool interrupt);
    bool Silence();
    bool IsSpeaking();
    bool HasSpeech();
    bool HasBraille();
    std::wstring DetectScreenReader(); // empty when no screen reader is running
    // Braille alone, for what SAPI is speaking while a screen reader with a display runs.
    bool Braille(const wchar_t* text);
}
