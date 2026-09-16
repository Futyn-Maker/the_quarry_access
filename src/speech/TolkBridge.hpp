#pragma once
// Dynamic binding to Tolk.dll (screen-reader abstraction: NVDA, JAWS, SAPI...).
// Tolk.dll and its driver DLLs must sit next to the game executable.

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
    std::wstring DetectScreenReader(); // empty when none (SAPI fallback may still be used)

    // Whether SAPI is to speak even while a screen reader is running. Set before Load for
    // the start; changing it afterwards moves the speech at once and names the driver now
    // speaking. Without a screen reader SAPI speaks either way.
    void SetPreferSapi(bool prefer);
    bool PrefersSapi();
    std::wstring PreferSapi(bool prefer);
    // Loads Tolk afresh with the current preference, for a driver that would not let go.
    std::wstring Reload();
}
