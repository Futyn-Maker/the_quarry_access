#pragma once
// Speech policies on top of Tolk (modelled on the Palworld accessibility mod):
//
//   Focus(text)    - interrupting; for focus/navigation. Cascade-aware: right after
//                    a screen arrives (NotifyScreenArrival) the focus events of the
//                    opening cascade collapse into a single pending slot that Tick()
//                    flushes once the cascade settles, so only the final focus is heard.
//   Announce(text) - ordered, non-interrupting (notifications, popups, choices).
//   Queue(text)    - like Announce but bounded (subtitles): when too many are pending
//                    the oldest is dropped, never the newest.
//   Now(text)      - hard interrupt (QTE direction, interrupts). No dedupe.
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
    void NotifyScreenArrival();

    // Text of the last thing spoken (for the read-screen readout).
    std::wstring Last();
}
