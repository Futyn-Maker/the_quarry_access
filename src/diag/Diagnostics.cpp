#include "diag/Diagnostics.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "features/Feature.hpp"
#include "hooks/HookDispatcher.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "watch/Watchers.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>

namespace qa::diag
{
    using RC::Unreal::UObject;

    namespace
    {
        std::wstring g_modDir;

        std::wstring TimestampForFile()
        {
            SYSTEMTIME t{};
            GetLocalTime(&t);
            return std::format(L"{:04}{:02}{:02}-{:02}{:02}{:02}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        }
    }

    void SetModDir(const std::wstring& modDir)
    {
        g_modDir = modDir;
    }

    namespace
    {
        void RunCommand(const std::wstring& line)
        {
            const std::wstring cmd = str::Trim(line);
            if (cmd.empty()) return;
            log::Info(L"command: {}", cmd);
            const auto lower = str::ToLower(cmd);
            if (lower == L"trace on")
                hooks::SetTrace(true);
            else if (lower == L"trace off")
                hooks::SetTrace(false);
            else if (lower == L"dump")
                DumpScreen();
            else if (lower == L"read")
                speech::Now(ReadScreen());
            else if (lower == L"help")
                speech::Now(Help());
            else if (str::StartsWith(lower, L"loglevel "))
            {
                log::Level level;
                if (log::ParseLevel(cmd.substr(9), level)) log::SetLevel(level);
            }
            else if (str::StartsWith(lower, L"say "))
                speech::Announce(cmd.substr(4));
            else
                log::Error(L"unknown command: {}", cmd);
        }

        void PollCommandFile(float)
        {
            static double lastCheck = -1.0;
            const double now = gamethread::NowSeconds();
            if (now - lastCheck < 0.5) return;
            lastCheck = now;
            if (g_modDir.empty()) return;
            const std::filesystem::path path = std::filesystem::path(g_modDir) / L"command.txt";
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) return;
            std::string bytes;
            {
                std::ifstream raw(path, std::ios::binary);
                bytes.assign((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
            }
            std::filesystem::remove(path, ec);
            for (const auto& line : str::Split(str::Utf8ToWide(bytes), L'\n'))
            {
                RunCommand(line);
            }
        }
    }

    void InstallCommandFile()
    {
        gamethread::AddPoller(L"commandfile", &PollCommandFile);
    }

    void DumpScreen()
    {
        std::vector<std::wstring> lines;
        lines.push_back(L"== QuarryAccess screen dump ==");
        lines.push_back(L"last hook: " + hooks::LastHook());

        const auto widgets = obj::FindAllLive(L"UserWidget");
        size_t shown = 0;
        for (auto* widget : widgets)
        {
            if (!obj::IsWidgetShown(widget)) continue;
            // Only report top-level user widgets (not nested inside another user widget's tree).
            UObject* owner = obj::FindOuterUserWidget(widget);
            if (owner) continue;
            ++shown;
            UObject* focused = nullptr;
            obj::ReadObject(widget, L"LastFocusedWidget", focused);
            lines.push_back(std::format(L"[widget] {} {} focused={} {}", obj::ClassName(widget), obj::ObjectName(widget), obj::ClassName(focused),
                                        obj::ObjectName(focused)));
            obj::WalkWidgetTree(
                widget, 14,
                [&](UObject* w, int depth)
                {
                    if (!obj::IsWidgetVisible(w)) return true;
                    std::wstring text = str::StripMarkup(obj::TextOf(w));
                    std::wstring pad(static_cast<size_t>(depth) * 2, L' ');
                    if (!text.empty() || obj::IsA(w, L"UserWidget"))
                    {
                        lines.push_back(std::format(L"{}- {} {}{}", pad, obj::ClassName(w), obj::ObjectName(w), text.empty() ? L"" : L" = \"" + text + L"\""));
                    }
                    return true;
                });
        }
        lines.push_back(std::format(L"shown top-level user widgets: {} (of {} live)", shown, widgets.size()));
        for (const auto& [name, instance] : watch::LiveHudInstances())
        {
            lines.push_back(std::format(L"[hud] {} -> {} {} texts: {}", name, obj::ClassName(instance), obj::ObjectName(instance),
                                        str::Join(obj::DescendantTexts(instance), L" | ")));
        }
        lines.push_back(std::format(L"current screen: {}  focused: {}", obj::ClassName(watch::CurrentScreen()), obj::ClassName(watch::CurrentFocused())));

        std::wstring path;
        if (!g_modDir.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(g_modDir) / L"dumps", ec);
            path = g_modDir + L"\\dumps\\screen-" + TimestampForFile() + L".txt";
            std::ofstream out(path, std::ios::binary);
            if (out)
            {
                out << "\xEF\xBB\xBF";
                for (const auto& line : lines)
                    out << str::WideToUtf8(line) << "\n";
            }
        }
        for (const auto& line : lines)
            log::Info(L"dump: {}", line);
        log::Info(L"screen dump written to {}", path);
    }

    void ToggleTrace()
    {
        const bool on = !hooks::TraceEnabled();
        hooks::SetTrace(on);
        speech::Announce(locale::Mod(on ? L"diag.trace.on" : L"diag.trace.off"));
    }

    void CycleLogLevel()
    {
        const auto level = log::CycleLevel();
        speech::Announce(locale::Mod(L"diag.loglevel", log::LevelName(level)));
    }

    std::wstring ReadScreen()
    {
        // Features describe the current screen (menus, HUD, ...). Only when none of them
        // has anything to say do we fall back to the raw focused-widget text.
        std::vector<std::wstring> parts;
        features::DescribeAll(parts);
        if (parts.empty())
        {
            if (UObject* focused = watch::CurrentFocused())
            {
                const auto texts = obj::DescendantTexts(focused, 4);
                std::wstring text = str::Join(texts, L", ");
                if (text.empty()) text = obj::ObjectName(focused);
                parts.push_back(locale::Mod(L"read.focused", text));
            }
        }
        for (const auto& [name, instance] : watch::LiveHudInstances())
        {
            const auto texts = obj::DescendantTexts(instance, 6);
            if (!texts.empty()) parts.push_back(str::Join(texts, L", "));
        }
        if (parts.empty())
        {
            const auto last = speech::Last();
            if (!last.empty())
                parts.push_back(locale::Mod(L"read.last", last));
            else
                parts.push_back(locale::Mod(L"read.nothing"));
        }
        return str::JoinSentences(parts);
    }

    std::wstring Help()
    {
        const auto& s = cfg::Get();
        std::vector<std::wstring> parts;
        parts.push_back(locale::Mod(L"help.intro"));
        parts.push_back(locale::Mod(L"help.hotkeys", std::vector<std::wstring>{s.keyRepeat, s.keyReadScreen, s.keyStop, s.keyHelp}));
        parts.push_back(locale::Mod(L"help.pad"));
        features::HelpAll(parts);
        return str::JoinSentences(parts);
    }
}
