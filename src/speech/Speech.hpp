#pragma once
// Speech policies on top of the two voices the mod has (speech/Outputs): the player's screen
// reader, or SAPI.
//
//   Focus(text)    - for focus and navigation. Interrupts what is being said only when
//                    the player has pressed something since it started, so their own
//                    input is answered at once while the mod never cuts itself short.
//   Announce(text) - non-interrupting: the reader says it after what it is already
//                    saying (screen arrivals, prompts, notifications, subtitles).
//   Now(text)      - hard interrupt, for what the player asked for directly. No dedupe.
//   Stop()         - silence everything.
//   Repeat()       - speak the last utterance again.
//
// Thread-safe.

#include <string>
#include <string_view>

namespace qa::speech
{
    void Init();

    void Focus(std::wstring_view text);
    void Announce(std::wstring_view text);
    void Now(std::wstring_view text);
    void Stop();
    // Silences everything for a key the player pressed. The log notes it at Verbose only,
    // since the arrows and the D-pad come many times a minute.
    void StopForKey(std::wstring_view key);
    void Repeat();

    // Text of the last thing spoken (for the read-screen readout).
    std::wstring Last();

    // SAPI speaks even while a screen reader runs, when so asked (the ini at the start, the
    // Speech hotkey during play).
    void SetPreferSapi(bool prefer);
    // Moves the speech between the screen reader and SAPI, says which now speaks, and keeps
    // the choice in the ini. Without a screen reader SAPI speaks either way, and that is said.
    void ToggleOutput();
    // What speaks now: the screen reader's name, or "SAPI".
    std::wstring OutputName();
}
