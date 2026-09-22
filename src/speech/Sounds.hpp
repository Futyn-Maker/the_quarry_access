#pragma once
// Short cues played through the sound card rather than the screen reader, so they
// reach the player at once, even while a held key keeps the reader silent or while the
// reader is still busy with a subtitle.
//
// PlaySound plays one of them at a time, and a new one cuts off the one playing. A cue
// tells of something that happened once, while the blips below come again a moment later,
// so the blips wait while a cue sounds.

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

    // Plays a beacon: `pan` places it from the left ear (-1) to the right (1), `level` raises
    // its pitch as the target comes closer, and `muffled` drops it an octave for a target
    // behind the player.
    void Beacon(double pan, double level, bool muffled);

    // Plays an aim blip: `pan` places it on the side of the target, `level` (0 low to 1 high)
    // is its pitch, above or below the crosshair; `locked` plays the quick double ping of a
    // shot that would land instead, and `muffled` drops the blip an octave for a target
    // behind the player.
    void Aim(double pan, double level, bool locked, bool muffled);
}
