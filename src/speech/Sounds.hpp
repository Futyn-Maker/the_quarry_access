#pragma once
// Short cues played through the sound card rather than the screen reader, so they
// reach the player at once, even while a held key keeps the reader silent or while the
// reader is still busy with a subtitle.

namespace qa::sounds
{
    enum class Cue
    {
        Confirm, // a choice committed or a challenge won
        Fail,    // a challenge lost
        Up,      // the direction a quick-time event asks for
        Down,
        Left,
        Right,
    };

    // Prepares the cues at the given volume (0 to 100; 0 keeps the mod silent).
    void Init(int volumePercent);

    // Plays a cue without waiting for it. Safe from any thread.
    void Play(Cue cue);

    // Plays a short blip whose pitch follows `level` (0 low to 1 high): the audible form of a
    // ring or a bar that fills or empties.
    void Tick(double level);
}
