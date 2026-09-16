#pragma once
// Speech policies on top of Tolk:
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
    void Repeat();

    // Text of the last thing spoken (for the read-screen readout).
    std::wstring Last();

    // Moves the speech between the screen reader and SAPI, says which now speaks, and keeps
    // the choice in the ini. Without a screen reader SAPI speaks either way, and that is said.
    void ToggleOutput();
}
