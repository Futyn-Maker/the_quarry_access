#pragma once
// Speech policies on top of Tolk:
//
//   Focus(text)    - for focus and navigation. Interrupts what is being said when the
//                    player has pressed something since it started, so their own input
//                    is answered at once while the mod never cuts itself short.
//   Announce(text) - ordered, non-interrupting (screen arrivals, prompts, notifications).
//   Queue(text)    - like Announce but bounded (subtitles): when too many are pending
//                    the oldest is dropped, never the newest.
//   Now(text)      - hard interrupt, for what the player asked for directly and for
//                    anything with a deadline. No dedupe.
//   Stop()         - silence everything.
//   Repeat()       - speak the last utterance again.
//
// Thread-safe. Tick() must run regularly (engine tick pump).

#include <string>
#include <string_view>

namespace qa::speech
{
    void Init();
    void Tick();

    void Focus(std::wstring_view text);
    void Announce(std::wstring_view text);
    void Queue(std::wstring_view text);
    void Now(std::wstring_view text);
    void Stop();
    void Repeat();

    // Text of the last thing spoken (for the read-screen readout).
    std::wstring Last();
}
