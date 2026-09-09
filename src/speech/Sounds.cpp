#include "speech/Sounds.hpp"

#include "core/Log.hpp"

#include <windows.h>

#include <mmsystem.h>

#include <algorithm>
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

        std::vector<uint8_t> g_confirm; // a WAV file in memory; PlaySound reads it while playing
        bool g_enabled = false;

        void Append(std::vector<uint8_t>& out, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            out.insert(out.end(), bytes, bytes + size);
        }

        void Append32(std::vector<uint8_t>& out, uint32_t value)
        {
            Append(out, &value, 4);
        }

        void Append16(std::vector<uint8_t>& out, uint16_t value)
        {
            Append(out, &value, 2);
        }

        struct Note
        {
            double frequency;
            double seconds;
        };

        // A sine tone per note with a soft attack and release, so that it neither clicks nor
        // startles; the amplitude follows the configured volume.
        std::vector<uint8_t> Wave(const std::vector<Note>& notes, double amplitude)
        {
            std::vector<int16_t> samples;
            for (const auto& note : notes)
            {
                const int count = static_cast<int>(note.seconds * kRate);
                const int attack = kRate * 4 / 1000;
                const int release = kRate * 30 / 1000;
                for (int i = 0; i < count; ++i)
                {
                    double envelope = 1.0;
                    if (i < attack) envelope = static_cast<double>(i) / attack;
                    if (count - i < release) envelope = std::min(envelope, static_cast<double>(count - i) / release);
                    const double value = std::sin(2.0 * std::numbers::pi * note.frequency * i / kRate) * amplitude * envelope;
                    samples.push_back(static_cast<int16_t>(value * 32767.0));
                }
            }
            std::vector<uint8_t> wav;
            const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
            Append(wav, "RIFF", 4);
            Append32(wav, 36 + dataBytes);
            Append(wav, "WAVE", 4);
            Append(wav, "fmt ", 4);
            Append32(wav, 16);
            Append16(wav, 1); // PCM
            Append16(wav, 1); // mono
            Append32(wav, kRate);
            Append32(wav, kRate * 2);
            Append16(wav, 2);
            Append16(wav, 16);
            Append(wav, "data", 4);
            Append32(wav, dataBytes);
            Append(wav, samples.data(), dataBytes);
            return wav;
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
        // Two short rising notes: the usual shape of a confirmation.
        g_confirm = Wave({{660.0, 0.055}, {990.0, 0.075}}, amplitude);
        log::Info(L"sounds: volume {}", volume);
    }

    void Play(Cue cue)
    {
        if (!g_enabled) return;
        const std::vector<uint8_t>* wav = nullptr;
        switch (cue)
        {
        case Cue::Confirm: wav = &g_confirm; break;
        }
        if (!wav || wav->empty()) return;
        PlaySoundW(reinterpret_cast<LPCWSTR>(wav->data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
}
