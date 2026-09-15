#include "speech/Sounds.hpp"

#include "core/Log.hpp"

#include <windows.h>

#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

namespace qa::sounds
{
    namespace
    {
        constexpr int kRate = 44100;
        constexpr int kTickSteps = 12; // blips from the lowest to the highest pitch

        using Wav = std::vector<uint8_t>; // a WAV file in memory; PlaySound reads it while playing

        std::array<Wav, 6> g_cues;
        std::array<Wav, kTickSteps> g_ticks;
        std::array<Wav, 4> g_beacons; // made on demand; a few are kept so a playing one is not overwritten
        Wav g_locked;                 // the double ping of a shot that would land
        size_t g_beaconIndex = 0;
        double g_amplitude = 0.0;
        bool g_enabled = false;

        void Append(Wav& out, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            out.insert(out.end(), bytes, bytes + size);
        }

        void Append32(Wav& out, uint32_t value)
        {
            Append(out, &value, 4);
        }

        void Append16(Wav& out, uint16_t value)
        {
            Append(out, &value, 2);
        }

        struct Note
        {
            double frequency;
            double seconds;
        };

        // Sine tones with a soft attack and release, so that they neither click nor startle;
        // the amplitude follows the configured volume. `pan` places the sound: -1 left, 0
        // centre, 1 right.
        Wav Wave(const std::vector<Note>& notes, double amplitude, double pan = 0.0)
        {
            const bool stereo = pan != 0.0;
            const double left = stereo ? std::clamp(1.0 - pan, 0.0, 1.0) : 1.0;
            const double right = stereo ? std::clamp(1.0 + pan, 0.0, 1.0) : 1.0;
            std::vector<int16_t> samples;
            for (const auto& note : notes)
            {
                const int count = static_cast<int>(note.seconds * kRate);
                const int attack = kRate * 4 / 1000;
                const int release = std::min(count / 2, kRate * 30 / 1000);
                for (int i = 0; i < count; ++i)
                {
                    double envelope = 1.0;
                    if (i < attack) envelope = static_cast<double>(i) / attack;
                    if (count - i < release) envelope = std::min(envelope, static_cast<double>(count - i) / release);
                    const double value = std::sin(2.0 * std::numbers::pi * note.frequency * i / kRate) * amplitude * envelope;
                    if (stereo)
                    {
                        samples.push_back(static_cast<int16_t>(value * left * 32767.0));
                        samples.push_back(static_cast<int16_t>(value * right * 32767.0));
                    }
                    else
                    {
                        samples.push_back(static_cast<int16_t>(value * 32767.0));
                    }
                }
            }
            const uint16_t channels = stereo ? 2 : 1;
            Wav wav;
            const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
            Append(wav, "RIFF", 4);
            Append32(wav, 36 + dataBytes);
            Append(wav, "WAVE", 4);
            Append(wav, "fmt ", 4);
            Append32(wav, 16);
            Append16(wav, 1); // PCM
            Append16(wav, channels);
            Append32(wav, kRate);
            Append32(wav, kRate * 2 * channels);
            Append16(wav, static_cast<uint16_t>(2 * channels));
            Append16(wav, 16);
            Append(wav, "data", 4);
            Append32(wav, dataBytes);
            Append(wav, samples.data(), dataBytes);
            return wav;
        }

        void PlayWav(const Wav& wav)
        {
            if (!g_enabled || wav.empty()) return;
            PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
    }

    void Init(int volumePercent)
    {
        const int volume = std::clamp(volumePercent, 0, 100);
        g_enabled = volume > 0;
        if (!g_enabled)
        {
            log::Info(L"sounds: off");
            return;
        }
        // Full volume is half scale: loud enough over the game, never a jolt.
        const double amplitude = 0.5 * volume / 100.0;
        g_amplitude = amplitude;
        // Two short rising notes: the usual shape of a confirmation; two falling ones for a loss.
        g_cues[static_cast<size_t>(Cue::Confirm)] = Wave({{660.0, 0.055}, {990.0, 0.075}}, amplitude);
        g_cues[static_cast<size_t>(Cue::Fail)] = Wave({{660.0, 0.06}, {440.0, 0.11}}, amplitude);
        // Directions: three quick notes climbing for up and falling for down, one note in
        // the left or the right ear for the sides.
        g_cues[static_cast<size_t>(Cue::Up)] = Wave({{440.0, 0.04}, {554.0, 0.04}, {659.0, 0.06}}, amplitude);
        g_cues[static_cast<size_t>(Cue::Down)] = Wave({{659.0, 0.04}, {554.0, 0.04}, {440.0, 0.06}}, amplitude);
        g_cues[static_cast<size_t>(Cue::Left)] = Wave({{523.0, 0.05}, {523.0, 0.08}}, amplitude, -1.0);
        g_cues[static_cast<size_t>(Cue::Right)] = Wave({{523.0, 0.05}, {523.0, 0.08}}, amplitude, 1.0);
        // Blips two octaves apart from the lowest to the highest, quieter than the cues.
        for (int i = 0; i < kTickSteps; ++i)
        {
            const double frequency = 330.0 * std::pow(4.0, static_cast<double>(i) / (kTickSteps - 1));
            g_ticks[static_cast<size_t>(i)] = Wave({{frequency, 0.03}}, amplitude * 0.6);
        }
        // Two high notes close together, unlike anything else the mod plays.
        g_locked = Wave({{1047.0, 0.035}, {1568.0, 0.05}}, amplitude * 0.7, 1e-6);
        log::Info(L"sounds: volume {}", volume);
    }

    void Play(Cue cue)
    {
        const auto index = static_cast<size_t>(cue);
        if (index < g_cues.size()) PlayWav(g_cues[index]);
    }

    void Tick(double level)
    {
        const int step = static_cast<int>(std::lround(std::clamp(level, 0.0, 1.0) * (kTickSteps - 1)));
        PlayWav(g_ticks[static_cast<size_t>(step)]);
    }

    void Beacon(double pan, double level, bool muffled)
    {
        if (!g_enabled) return;
        // Two octaves from far to near; an octave lower when the target is behind.
        double frequency = 330.0 * std::pow(4.0, std::clamp(level, 0.0, 1.0));
        if (muffled) frequency *= 0.5;
        // A pan of exactly 0 would make a mono file; the beacon is always stereo.
        const double placed = std::clamp(pan, -1.0, 1.0);
        Wav& wav = g_beacons[g_beaconIndex++ % g_beacons.size()];
        wav = Wave({{frequency, muffled ? 0.04 : 0.06}}, g_amplitude * 0.7, placed == 0.0 ? 1e-6 : placed);
        PlayWav(wav);
    }

    void Aim(double pan, double level, bool locked, bool muffled)
    {
        if (!g_enabled) return;
        if (locked)
        {
            PlayWav(g_locked);
            return;
        }
        // Two octaves from below the crosshair to above it; an octave lower when the target is
        // behind.
        double frequency = 330.0 * std::pow(4.0, std::clamp(level, 0.0, 1.0));
        if (muffled) frequency *= 0.5;
        const double placed = std::clamp(pan, -1.0, 1.0);
        Wav& wav = g_beacons[g_beaconIndex++ % g_beacons.size()];
        wav = Wave({{frequency, 0.05}}, g_amplitude * 0.7, placed == 0.0 ? 1e-6 : placed);
        PlayWav(wav);
    }
}
