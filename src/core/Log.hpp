#pragma once
// Logging: every line goes to UE4SS's console/UE4SS.log (prefixed with
// [QuarryAccess]) and, when enabled, to Mods\QuarryAccess\QuarryAccess.log.
// Utterances are logged as `SAY <policy> "<text>"` so that the tester and the
// automated checks can read the speech transcript.

#include <format>
#include <string>
#include <string_view>

namespace qa::log
{
    enum class Level : int
    {
        Error = 0,
        Info = 1,
        Verbose = 2,
        Trace = 3,
    };

    void Init(const std::wstring& logFilePath, bool fileEnabled, Level level);
    void Shutdown();

    Level GetLevel();
    void SetLevel(Level level);
    Level CycleLevel(); // Info -> Verbose -> Trace -> Info
    std::wstring LevelName(Level level);
    bool ParseLevel(std::wstring_view name, Level& out);

    void Write(Level level, std::wstring_view text);

    template <class... Args>
    void Error(std::wformat_string<Args...> fmt, Args&&... args)
    {
        Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    template <class... Args>
    void Info(std::wformat_string<Args...> fmt, Args&&... args)
    {
        if (GetLevel() < Level::Info) return;
        Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template <class... Args>
    void Verbose(std::wformat_string<Args...> fmt, Args&&... args)
    {
        if (GetLevel() < Level::Verbose) return;
        Write(Level::Verbose, std::format(fmt, std::forward<Args>(args)...));
    }

    template <class... Args>
    void Trace(std::wformat_string<Args...> fmt, Args&&... args)
    {
        if (GetLevel() < Level::Trace) return;
        Write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
    }

    inline bool IsVerbose()
    {
        return GetLevel() >= Level::Verbose;
    }
    inline bool IsTrace()
    {
        return GetLevel() >= Level::Trace;
    }

    // Speech transcript line.
    void Say(std::wstring_view policy, std::wstring_view text);
}
