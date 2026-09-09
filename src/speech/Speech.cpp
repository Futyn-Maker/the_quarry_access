#include "speech/Speech.hpp"

#include "core/Config.hpp"
#include "core/Log.hpp"
#include "input/InputNames.hpp"
#include "speech/TolkBridge.hpp"

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
        Clock::time_point g_lastSpokenAt{};
        Clock::time_point g_lastOutputAt{}; // when the reader was last given something to say
        std::deque<std::wstring> g_queued;  // bounded subtitle queue
        std::deque<std::wstring> g_history; // for Repeat()
        // Time-based pacing fallback when the reader does not report IsSpeaking.
        Clock::time_point g_estimatedIdleAt{};

        long long MsSince(Clock::time_point t)
        {
            if (t.time_since_epoch().count() == 0) return 1'000'000;
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
        }

        bool ReaderBusy()
        {
            if (tolk::IsSpeaking()) return true;
            return Clock::now() < g_estimatedIdleAt;
        }

        void EstimateDuration(std::wstring_view text)
        {
            // ~15 characters per second is a conservative speech-rate estimate.
            const auto ms = 250 + static_cast<long long>(text.size()) * 65;
            g_estimatedIdleAt = Clock::now() + std::chrono::milliseconds(std::min<long long>(ms, 12000));
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
            std::wstring copy(text);
            tolk::Output(copy.c_str(), interrupt);
            g_lastOutputAt = Clock::now();
            EstimateDuration(text);
            Remember(text);
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
        g_queued.clear();
    }

    void Focus(std::wstring_view text)
    {
        if (text.empty() || !tolk::IsLoaded()) return;
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
        g_queued.clear();
        OutputNow(text, true, L"focus");
    }

    void Announce(std::wstring_view text)
    {
        if (text.empty() || !tolk::IsLoaded()) return;
        std::lock_guard lock(g_mutex);
        OutputNow(text, false, L"announce");
    }

    void Queue(std::wstring_view text)
    {
        if (text.empty() || !tolk::IsLoaded()) return;
        std::lock_guard lock(g_mutex);
        if (!ReaderBusy() && g_queued.empty())
        {
            OutputNow(text, false, L"queue");
            return;
        }
        g_queued.emplace_back(text);
        const auto maxQueued = static_cast<size_t>(std::max(1, cfg::Get().maxQueuedSubtitles));
        while (g_queued.size() > maxQueued)
        {
            log::Verbose(L"speech: dropping queued \"{}\"", g_queued.front());
            g_queued.pop_front();
        }
    }

    void Now(std::wstring_view text)
    {
        if (text.empty() || !tolk::IsLoaded()) return;
        std::lock_guard lock(g_mutex);
        g_queued.clear();
        OutputNow(text, true, L"now");
    }

    void Stop()
    {
        std::lock_guard lock(g_mutex);
        g_queued.clear();
        g_estimatedIdleAt = {};
        tolk::Silence();
        log::Info(L"speech: stopped");
    }

    void Repeat()
    {
        std::lock_guard lock(g_mutex);
        if (g_history.empty()) return;
        const std::wstring last = g_history.back();
        log::Say(L"repeat", last);
        tolk::Output(last.c_str(), true);
        g_lastOutputAt = Clock::now();
        EstimateDuration(last);
    }

    std::wstring Last()
    {
        std::lock_guard lock(g_mutex);
        return g_lastSpoken;
    }

    void Tick()
    {
        std::lock_guard lock(g_mutex);
        if (!tolk::IsLoaded()) return;
        if (!g_queued.empty() && !ReaderBusy())
        {
            const std::wstring text = g_queued.front();
            g_queued.pop_front();
            OutputNow(text, false, L"queue");
        }
    }
}
