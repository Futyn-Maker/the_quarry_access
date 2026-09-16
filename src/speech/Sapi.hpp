#pragma once
// Speech through the Microsoft Speech API, for players without a screen reader and for
// those who prefer it over theirs. The voice lives on a thread of its own with its own COM
// apartment and message loop, so what the game thread does never touches it, and the voice
// follows the mod's language when the system's default voice speaks another.

#include <string>
#include <string_view>

namespace qa::sapi
{
    // Starts the speech thread and creates the voice. False when SAPI is not available.
    bool Start();
    void Stop();
    bool IsAvailable();

    // Picks a voice for a language code such as "ru_RU" when the default voice speaks another
    // language; the default voice stays when it speaks the language or when none does.
    void SelectVoiceFor(std::wstring_view localeCode);
    std::wstring VoiceName();

    // Says the text after what is already being said, or instead of it.
    bool Speak(std::wstring_view text, bool interrupt);
    void Silence();
}
