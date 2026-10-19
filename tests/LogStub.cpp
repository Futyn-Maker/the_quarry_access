// Stands in for core/Log, which writes through UE4SS: the modules under test log through this
// one, which keeps every line at every level for the tests to read.
#include "Support.hpp"
#include "core/Log.hpp"

#include <atomic>
#include <mutex>
#include <vector>

namespace qa::log
{
    namespace
    {
        std::atomic<int> g_level{static_cast<int>(Level::Trace)};
        std::mutex g_mutex;
        std::vector<std::wstring> g_lines;
    }

    void Init(const std::wstring&, bool, Level level)
    {
        g_level = static_cast<int>(level);
    }

    void Shutdown() {}

    Level GetLevel()
    {
        return static_cast<Level>(g_level.load());
    }

    void SetLevel(Level level)
    {
        g_level = static_cast<int>(level);
    }

    Level CycleLevel()
    {
        switch (GetLevel())
        {
        case Level::Info: SetLevel(Level::Verbose); break;
        case Level::Verbose: SetLevel(Level::Trace); break;
        default: SetLevel(Level::Info); break;
        }
        return GetLevel();
    }

    std::wstring LevelName(Level level)
    {
        switch (level)
        {
        case Level::Error: return L"Error";
        case Level::Info: return L"Info";
        case Level::Verbose: return L"Verbose";
        case Level::Trace: return L"Trace";
        }
        return L"Info";
    }

    bool ParseLevel(std::wstring_view name, Level& out)
    {
        for (const Level level : {Level::Error, Level::Info, Level::Verbose, Level::Trace})
        {
            if (str::EqualsNoCase(name, LevelName(level)))
            {
                out = level;
                return true;
            }
        }
        return false;
    }

    void Write(Level, std::wstring_view text)
    {
        std::lock_guard lock(g_mutex);
        g_lines.emplace_back(text);
    }

    void Say(std::wstring_view policy, std::wstring_view text)
    {
        Write(Level::Info, L"SAY " + std::wstring(policy) + L" \"" + std::wstring(text) + L"\"");
    }
}

namespace tests
{
    std::vector<std::wstring> LogLines()
    {
        std::lock_guard lock(qa::log::g_mutex);
        return qa::log::g_lines;
    }

    void ClearLog()
    {
        std::lock_guard lock(qa::log::g_mutex);
        qa::log::g_lines.clear();
    }

    size_t LogLinesWith(std::wstring_view text)
    {
        size_t count = 0;
        for (const auto& line : LogLines())
        {
            if (line.find(text) != std::wstring::npos) ++count;
        }
        return count;
    }
}
