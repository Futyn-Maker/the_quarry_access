#include "speech/Sounds.hpp"

#include "core/Log.hpp"

#include <windows.h>

#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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

        std::array<Wav, 9> g_cues;
        std::array<Wav, kTickSteps> g_ticks;
        std::array<Wav, 4> g_beacons; // made on demand; a few are kept so a playing one is not overwritten
        Wav g_locked;                 // the double ping of a shot that would land
        size_t g_beaconIndex = 0;
        double g_amplitude = 0.0;
        bool g_enabled = false;
        std::atomic<int64_t> g_cueUntil{0}; // when the last cue falls silent, in milliseconds of the steady clock

        int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // How long a sound made by Wave lasts, from its header: the bytes of its samples over
        // the bytes it plays a second.
        int64_t LengthMs(const Wav& wav)
        {
            if (wav.size() < 44) return 0;
            uint32_t bytesPerSecond = 0;
            uint32_t dataBytes = 0;
            std::memcpy(&bytesPerSecond, wav.data() + 28, 4);
            std::memcpy(&dataBytes, wav.data() + 40, 4);
            return bytesPerSecond == 0 ? 0 : static_cast<int64_t>(dataBytes) * 1000 / bytesPerSecond;
        }

        // A blip that came now would cut off a cue still sounding.
        bool CueSounding()
        {
            return NowMs() < g_cueUntil.load();
        }

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
            double glideTo = 0.0;  // the pitch the note slides to over its length; 0 holds it
            double attack = 0.004; // seconds it takes to rise
            double release = 0.03; // seconds it takes to fade, at most half the note
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
                const int attack = std::max(1, static_cast<int>(std::lround(note.attack * kRate)));
                const int release = std::min(count / 2, static_cast<int>(std::lround(note.release * kRate)));
                double phase = 0.0;
                for (int i = 0; i < count; ++i)
                {
                    double envelope = 1.0;
                    if (i < attack) envelope = static_cast<double>(i) / attack;
                    if (count - i < release) envelope = std::min(envelope, static_cast<double>(count - i) / release);
                    const double value = std::sin(phase) * amplitude * envelope;
                    // A slide moves by equal steps of pitch, as the ear hears them, rather than
                    // of frequency.
                    const double frequency =
                        note.glideTo > 0.0 ? note.frequency * std::pow(note.glideTo / note.frequency, static_cast<double>(i) / count) : note.frequency;
                    phase += 2.0 * std::numbers::pi * frequency / kRate;
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
        // The game handing the character over and taking it back: one note sliding up a fifth
        // and one sliding down, softer and quieter than the cues of the mechanics, since one of
        // them comes with every cutscene.
        g_cues[static_cast<size_t>(Cue::ControlGained)] =
            Wave({{.frequency = 392.0, .seconds = 0.18, .glideTo = 587.0, .attack = 0.025, .release = 0.06}}, amplitude * 0.6);
        g_cues[static_cast<size_t>(Cue::ControlLost)] =
            Wave({{.frequency = 587.0, .seconds = 0.18, .glideTo = 392.0, .attack = 0.025, .release = 0.06}}, amplitude * 0.6);
        // An interruption: three quick taps of one high note, unlike the two notes of an
        // outcome and the runs of a direction, heard the instant the game offers it and
        // before its words. A note of no frequency is the silence between the taps.
        g_cues[static_cast<size_t>(Cue::Interrupt)] = Wave({{988.0, 0.035}, {0.0, 0.03}, {988.0, 0.035}, {0.0, 0.03}, {988.0, 0.06}}, amplitude);
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
        if (!g_enabled || index >= g_cues.size() || g_cues[index].empty()) return;
        g_cueUntil = NowMs() + LengthMs(g_cues[index]);
        PlayWav(g_cues[index]);
    }

    void Tick(double level)
    {
        if (CueSounding()) return;
        const int step = static_cast<int>(std::lround(std::clamp(level, 0.0, 1.0) * (kTickSteps - 1)));
        PlayWav(g_ticks[static_cast<size_t>(step)]);
    }

    void Beacon(double pan, double level, bool muffled)
    {
        if (!g_enabled || CueSounding()) return;
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
        if (!g_enabled || CueSounding()) return;
        const double placed = std::clamp(pan, -1.0, 1.0);
        if (locked)
        {
            // The double ping keeps the side of the target, so that a hit at the edge of the
            // pattern can be nudged to its middle.
            Wav& wav = g_beacons[g_beaconIndex++ % g_beacons.size()];
            wav = Wave({{1047.0, 0.035}, {1568.0, 0.05}}, g_amplitude * 0.7, placed == 0.0 ? 1e-6 : placed);
            PlayWav(wav);
            return;
        }
        // Two octaves from below the crosshair to above it; an octave lower when the target is
        // behind.
        double frequency = 330.0 * std::pow(4.0, std::clamp(level, 0.0, 1.0));
        if (muffled) frequency *= 0.5;
        Wav& wav = g_beacons[g_beaconIndex++ % g_beacons.size()];
        wav = Wave({{frequency, 0.05}}, g_amplitude * 0.7, placed == 0.0 ? 1e-6 : placed);
        PlayWav(wav);
    }
}
