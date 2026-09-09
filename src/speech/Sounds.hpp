#pragma once
// Short cues played through the sound card rather than the screen reader, so they
// reach the player even while a held key keeps the reader silent.

namespace qa::sounds
{
    enum class Cue
    {
        Confirm, // a choice committed: the key can be released
    };

    // Prepares the cues at the given volume (0 to 100; 0 keeps the mod silent).
    void Init(int volumePercent);

    // Plays a cue without waiting for it. Safe from any thread.
    void Play(Cue cue);
}
