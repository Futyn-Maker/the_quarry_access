#pragma once
// The two voices the mod speaks with, and the one thread that owns them.
//
// Both are Prism backends: the screen reader the player runs (NVDA, JAWS, Narrator through UI
// Automation, and the others Prism knows) and SAPI, which speaks when no screen reader runs and
// whenever the player asks for it. Prism picks nothing by itself here: SAPI is named outright,
// so a machine without a screen reader speaks with the voice Windows is set up with rather
// than with whatever other engine happens to be installed.
//
// A Prism backend is not thread-safe, and the COM proxies behind the Windows backends answer
// only in the apartment that made them, so every call into the library is made on this module's
// thread. The rest of the mod hands it lines and reads the names it keeps, and never waits.

#include <string>
#include <string_view>

namespace qa::outputs
{
    // Starts the speech thread, which loads prism.dll from the given path, finds the screen
    // reader and creates the SAPI voice. False when neither voice could be had.
    bool Start(const std::wstring& prismDllPath);
    void Stop();

    // Something can be spoken: at least one of the two voices is there.
    bool CanSpeak();
    // The screen reader that is running, as Prism names it; empty when none is.
    std::wstring ReaderName();
    // That screen reader also drives a braille display.
    bool ReaderHasBraille();
    bool SapiAvailable();
    std::wstring SapiVoiceName();

    // Picks the SAPI voice for a language code such as "ru_RU": the voice Windows is set to
    // when it speaks that language, else the installed voice that fits it best. Nothing else
    // about the voice is touched, so its rate and its volume stay the player's own settings.
    void SelectVoiceFor(std::wstring_view localeCode);

    // One line out, through SAPI or through the screen reader. A screen reader with a braille
    // display gets the line even while SAPI speaks, so a player who reads braille loses nothing
    // by moving the speech to SAPI.
    void Say(std::wstring_view text, bool interrupt, bool throughSapi);
    // Stops both voices and drops what is waiting.
    void Silence();
}
