#include "speech/Speech.hpp"

#include "core/Config.hpp"
#include "core/Log.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Outputs.hpp"

#include <chrono>
#include <deque>
#include <mutex>

namespace qa::speech
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // Input this recent is treated as the reason for the utterance even if it landed
        // just after the previous one started.
        constexpr long long kFreshInputMs = 120;

        std::mutex g_mutex;
        std::wstring g_lastSpoken;
        std::wstring g_lastMessage; // the last thing said that was not itself a readout
        Clock::time_point g_lastSpokenAt{};
        Clock::time_point g_lastOutputAt{}; // when the reader was last given something to say
        std::deque<std::wstring> g_history; // for Repeat()
        bool g_preferSapi = false;

        // SAPI speaks when no screen reader runs, and when the player prefers it to theirs.
        bool SapiSpeaks()
        {
            if (!outputs::SapiAvailable()) return false;
            return g_preferSapi || outputs::ReaderName().empty();
        }

        // One line to whichever speaks. A screen reader with a display keeps getting the
        // braille while SAPI does the speaking, which the outputs see to.
        void Emit(const std::wstring& text, bool interrupt)
        {
            outputs::Say(text, interrupt, SapiSpeaks());
        }

        long long MsSince(Clock::time_point t)
        {
            if (t.time_since_epoch().count() == 0) return 1'000'000;
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
        }

        void Remember(std::wstring_view text)
        {
            g_lastSpoken = std::wstring(text);
            g_lastSpokenAt = Clock::now();
            g_history.emplace_back(text);
            while (g_history.size() > 10)
                g_history.pop_front();
        }

        void OutputNow(std::wstring_view text, bool interrupt, const wchar_t* policy)
        {
            log::Say(policy, text);
            Emit(std::wstring(text), interrupt);
            g_lastOutputAt = Clock::now();
            Remember(text);
            if (std::wstring_view(policy) != L"now") g_lastMessage = std::wstring(text);
        }

        // Cutting a sentence short is only right when the player is waiting for the answer
        // to something they just did: they pressed, held or moved something after the
        // current utterance began. Everything the game says by itself waits its turn.
        bool PlayerIsWaiting()
        {
            const long long sinceInput = input::MsSinceInput();
            return sinceInput < kFreshInputMs || sinceInput <= MsSince(g_lastOutputAt);
        }
    }

    void Init()
    {
        std::lock_guard lock(g_mutex);
        g_lastSpoken.clear();
        g_lastMessage.clear();
    }

    void Focus(std::wstring_view text)
    {
        if (text.empty() || !outputs::CanSpeak()) return;
        std::lock_guard lock(g_mutex);
        if (text == g_lastSpoken && MsSince(g_lastSpokenAt) < cfg::Get().focusDedupeMs)
        {
            log::Trace(L"speech: focus dedupe \"{}\"", text);
            return;
        }
        if (!PlayerIsWaiting())
        {
            OutputNow(text, false, L"focus-after");
            return;
        }
        OutputNow(text, true, L"focus");
    }

    void Announce(std::wstring_view text)
    {
        if (text.empty() || !outputs::CanSpeak()) return;
        std::lock_guard lock(g_mutex);
        OutputNow(text, false, L"announce");
    }

    void Now(std::wstring_view text)
    {
        if (text.empty() || !outputs::CanSpeak()) return;
        std::lock_guard lock(g_mutex);
        OutputNow(text, true, L"now");
    }

    void Stop()
    {
        std::lock_guard lock(g_mutex);
        outputs::Silence();
        log::Info(L"speech: stopped");
    }

    void StopForKey(std::wstring_view key)
    {
        std::lock_guard lock(g_mutex);
        outputs::Silence();
        log::Verbose(L"speech: stopped by {}", key);
    }

    void Repeat()
    {
        std::lock_guard lock(g_mutex);
        if (g_history.empty()) return;
        const std::wstring last = g_history.back();
        log::Say(L"repeat", last);
        Emit(last, true);
        g_lastOutputAt = Clock::now();
    }

    std::wstring Last()
    {
        std::lock_guard lock(g_mutex);
        return g_lastMessage;
    }

    void ToggleOutput()
    {
        std::lock_guard lock(g_mutex);
        const std::wstring reader = outputs::ReaderName();
        // Without a screen reader the choice makes no difference: SAPI speaks either way.
        if (reader.empty() || !outputs::SapiAvailable())
        {
            OutputNow(locale::Mod(L"speech.noreader"), true, L"now");
            return;
        }
        g_preferSapi = !g_preferSapi;
        outputs::Silence();
        log::Info(L"speech: output {} (SAPI preferred: {}; SAPI voice {})", g_preferSapi ? L"SAPI" : reader, g_preferSapi, outputs::SapiVoiceName());
        if (!cfg::Persist(L"Speech", L"PreferSapi", g_preferSapi ? L"1" : L"0"))
            log::Error(L"speech: the output setting could not be saved to QuarryAccess.ini");
        OutputNow(locale::Mod(L"speech.output", g_preferSapi ? L"SAPI" : reader), true, L"now");
    }

    void SetPreferSapi(bool prefer)
    {
        std::lock_guard lock(g_mutex);
        g_preferSapi = prefer;
    }

    std::wstring OutputName()
    {
        std::lock_guard lock(g_mutex);
        if (SapiSpeaks()) return L"SAPI";
        return outputs::ReaderName();
    }
}
