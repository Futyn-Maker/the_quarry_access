#include "core/Log.hpp"
#include "core/Strings.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace qa::log
{
    namespace
    {
        std::atomic<int> g_level{static_cast<int>(Level::Info)};
        std::mutex g_mutex;
        FILE* g_file = nullptr;
        std::wstring g_path;
        size_t g_bytesWritten = 0;
        constexpr size_t kRotateBytes = 5u * 1024u * 1024u;

        std::wstring Timestamp()
        {
            SYSTEMTIME t{};
            GetLocalTime(&t);
            return std::format(L"{:02}:{:02}:{:02}.{:03}", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        }

        const wchar_t* Tag(Level level)
        {
            switch (level)
            {
            case Level::Error: return L"ERROR";
            case Level::Info: return L"info ";
            case Level::Verbose: return L"verb ";
            case Level::Trace: return L"trace";
            }
            return L"?";
        }

        void OpenFile()
        {
            if (g_path.empty()) return;
            g_file = _wfopen(g_path.c_str(), L"ab");
            g_bytesWritten = 0;
            if (g_file)
            {
                const std::string header = "\xEF\xBB\xBF---- QuarryAccess log opened ----\n";
                fwrite(header.data(), 1, header.size(), g_file);
                fflush(g_file);
            }
        }
    }

    void Init(const std::wstring& logFilePath, bool fileEnabled, Level level)
    {
        std::lock_guard lock(g_mutex);
        g_level = static_cast<int>(level);
        if (fileEnabled)
        {
            g_path = logFilePath;
            std::error_code ec;
            const auto existing = std::filesystem::file_size(std::filesystem::path(g_path), ec);
            if (!ec && existing > kRotateBytes)
            {
                std::filesystem::rename(g_path, g_path + L".old", ec);
            }
            OpenFile();
        }
    }

    void Shutdown()
    {
        std::lock_guard lock(g_mutex);
        if (g_file)
        {
            fclose(g_file);
            g_file = nullptr;
        }
    }

    Level GetLevel()
    {
        return static_cast<Level>(g_level.load(std::memory_order_relaxed));
    }

    void SetLevel(Level level)
    {
        g_level = static_cast<int>(level);
    }

    Level CycleLevel()
    {
        Level next = Level::Info;
        switch (GetLevel())
        {
        case Level::Info: next = Level::Verbose; break;
        case Level::Verbose: next = Level::Trace; break;
        default: next = Level::Info; break;
        }
        SetLevel(next);
        return next;
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
        const auto n = str::ToLower(str::Trim(name));
        if (n == L"error")
        {
            out = Level::Error;
            return true;
        }
        if (n == L"info")
        {
            out = Level::Info;
            return true;
        }
        if (n == L"verbose")
        {
            out = Level::Verbose;
            return true;
        }
        if (n == L"trace")
        {
            out = Level::Trace;
            return true;
        }
        return false;
    }

    void Write(Level level, std::wstring_view text)
    {
        const std::wstring line = std::format(L"[{}] [{}] {}", Timestamp(), Tag(level), text);

        if (level == Level::Error)
        {
            RC::Output::send<RC::LogLevel::Error>(STR("[QuarryAccess] {}\n"), std::wstring(text));
        }
        else
        {
            RC::Output::send<RC::LogLevel::Default>(STR("[QuarryAccess] {}\n"), std::wstring(text));
        }

        std::lock_guard lock(g_mutex);
        if (!g_file) return;
        const std::string utf8 = str::WideToUtf8(line) + "\n";
        fwrite(utf8.data(), 1, utf8.size(), g_file);
        fflush(g_file);
        g_bytesWritten += utf8.size();
        if (g_bytesWritten > kRotateBytes)
        {
            fclose(g_file);
            g_file = nullptr;
            std::error_code ec;
            std::filesystem::rename(g_path, g_path + L".old", ec);
            OpenFile();
        }
    }

    void Say(std::wstring_view policy, std::wstring_view text)
    {
        Write(Level::Info, std::format(L"SAY {} \"{}\"", policy, text));
    }
}
