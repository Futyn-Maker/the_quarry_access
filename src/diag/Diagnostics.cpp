#include "diag/Diagnostics.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "features/Exploration.hpp"
#include "features/Feature.hpp"
#include "features/Subtitles.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
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

    // Every property of one object, by its full path or the name of its class (the first
    // live instance then), so that a default value the game keeps in its code can be read.
    void DumpProperties(const std::wstring& what)
    {
        UObject* object = obj::FindObject(what);
        if (!object) object = obj::FindFirstLive(what);
        if (!object)
        {
            log::Error(L"props: nothing is called {}", what);
            return;
        }
        log::Info(L"props: {}", obj::FullName(object));
        auto* cls = object->GetClassPrivate();
        if (!cls) return;
        for (auto* prop : cls->ForEachPropertyInChain())
        {
            if (!prop) continue;
            log::Info(L"props:   {} ({}) = {}", prop->GetName(), obj::PropertyTypeName(prop), obj::ValueToString(prop, obj::ValuePtr(object, prop)));
        }
    }

    namespace
    {
        void RunCommandImpl(const std::wstring& line)
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
            else if (lower == L"subtitles on" || lower == L"subtitles off")
                features::SetSubtitles(lower == L"subtitles on");
            else if (lower == L"explore")
                features::DumpExploration();
            else if (lower == L"walkkeys")
                log::Info(L"input: {}", input::DescribeWalkKeys());
            else if (str::StartsWith(lower, L"key "))
                log::Info(L"{}", input::DescribeAction(cmd.substr(4)));
            else if (str::StartsWith(lower, L"props "))
                DumpProperties(str::Trim(cmd.substr(6)));
            else
                log::Error(L"unknown command: {}", cmd);
        }

        void RunCommandGuarded(void* context)
        {
            RunCommandImpl(*static_cast<const std::wstring*>(context));
        }

        // A command must never take the game down: a fault abandons it and is logged.
        void RunCommand(const std::wstring& line)
        {
            if (!obj::SafeInvoke(&RunCommandGuarded, const_cast<std::wstring*>(&line)))
                log::Error(L"command ran into a memory fault and was abandoned: {}", line);
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

    // How a widget hangs in the drawn hierarchy: through a slot, as the root of its owner's
    // tree, in the viewport, or not at all; and a render transform that moves or scales it.
    std::wstring Attachment(UObject* w)
    {
        std::wstring out;
        UObject* slot = nullptr;
        bool attached = obj::ReadObject(w, L"Slot", slot) && slot;
        if (!attached)
        {
            UObject* outer = w->GetOuterPrivate();
            UObject* root = nullptr;
            if (outer && obj::IsA(outer, L"WidgetTree") && obj::ReadObject(outer, L"RootWidget", root) && root == w) attached = true;
        }
        if (!attached)
        {
            if (obj::IsA(w, L"UserWidget"))
                out += obj::CallForBool(w, L"IsInViewport") ? L" viewport=yes" : L" viewport=no";
            else
                out += L" detached";
        }
        if (const auto* t = static_cast<const float*>(obj::StructPtr(w, L"RenderTransform")))
        {
            if (t[0] != 0.0f || t[1] != 0.0f) out += std::format(L" offset={:.0f},{:.0f}", t[0], t[1]);
            if (t[2] != 1.0f || t[3] != 1.0f) out += std::format(L" scale={:.2f},{:.2f}", t[2], t[3]);
        }
        return out;
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
            lines.push_back(std::format(L"[widget] {} {} focused={} {}{}", obj::ClassName(widget), obj::ObjectName(widget), obj::ClassName(focused),
                                        obj::ObjectName(focused), Attachment(widget)));
            obj::WalkWidgetTree(
                widget, 14,
                [&](UObject* w, int depth)
                {
                    if (!obj::IsWidgetVisible(w)) return true;
                    std::wstring text = str::StripMarkup(obj::TextOf(w));
                    std::wstring pad(static_cast<size_t>(depth) * 2, L' ');
                    if (!text.empty() || obj::IsA(w, L"UserWidget"))
                    {
                        const double opacity = obj::WidgetOpacity(w);
                        lines.push_back(std::format(L"{}- {} {}{}{}", pad, obj::ClassName(w), obj::ObjectName(w), text.empty() ? L"" : L" = \"" + text + L"\"",
                                                    (opacity < 0.999 ? std::format(L" opacity={:.2f}", opacity) : std::wstring()) + Attachment(w)));
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
        // The keys of the device in the player's hands: the gamepad's chords after a gamepad
        // press, the keyboard's keys otherwise.
        if (input::CurrentScheme() == input::Scheme::Gamepad && !str::Trim(s.chordHold).empty())
            parts.push_back(locale::Mod(L"help.chords", std::vector<std::wstring>{input::KeyDisplayName(s.chordHold), input::KeyDisplayName(s.chordRepeat),
                                                                                  input::KeyDisplayName(s.chordReadScreen), input::KeyDisplayName(s.chordStop),
                                                                                  input::KeyDisplayName(s.chordHelp)}));
        else
            parts.push_back(locale::Mod(L"help.hotkeys", std::vector<std::wstring>{s.keyRepeat, s.keyReadScreen, s.keyStop, s.keyHelp}));
        features::HelpAll(parts);
        return str::JoinSentences(parts);
    }
}
