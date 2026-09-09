#include "features/Subtitles.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace qa::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        UObject* g_widget = nullptr;             // the subtitle widget while the game shows it
        std::vector<std::wstring> g_shown;       // the lines on screen at the last poll
        std::vector<std::wstring> g_shownBefore; // and at the poll before that
        std::wstring g_lastLine;                 // the last line that appeared
        UObject* g_lineWidget = nullptr;         // the subtitle widget it appeared in
        std::wstring g_setting;                  // the subtitle setting last written to the log
        bool g_read = true;                      // whether lines are spoken as they appear

        const wchar_t* SettingName(int64_t setting)
        {
            switch (setting)
            {
            case 0: return L"Off";
            case 1: return L"On";
            case 2: return L"ClosedCaptions";
            case 3: return L"Minimal";
            default: return L"unknown";
            }
        }

        // The subtitle setting as the widget reports it, for the log: whatever the setting
        // keeps off the screen never reaches the lines that are read.
        std::wstring SubtitleSetting(UObject* widget)
        {
            auto* fn = obj::FindFunction(widget, L"GetSubtitleSetting");
            if (!fn) return L"unknown";
            int64_t value = -1;
            obj::Call(widget, fn, nullptr,
                      [&](void* params)
                      {
                          for (auto* prop : fn->ForEachProperty())
                          {
                              if (prop && prop->GetName() == L"ReturnValue") obj::ReadIntAt(params, prop, value);
                          }
                      });
            return SettingName(value);
        }

        // The text of one subtitle line exactly as displayed.
        std::wstring LineText(UObject* line)
        {
            UObject* block = nullptr;
            if (!obj::ReadObject(line, L"LineText", block) || !obj::IsLive(block) || !obj::IsWidgetVisible(block)) return {};
            return str::Trim(str::StripMarkup(obj::TextOf(block)));
        }

        // The line widgets of the subtitle widget: its line array, else the children of its
        // line container.
        std::vector<UObject*> Lines(UObject* widget)
        {
            std::vector<UObject*> lines;
            obj::ReadObjectArray(widget, L"ForegroundLines", lines);
            if (lines.empty())
            {
                UObject* container = nullptr;
                if (obj::ReadObject(widget, L"ForegroundSubtitleLineContainer", container) && obj::IsLive(container)) lines = obj::PanelChildren(container);
            }
            return lines;
        }

        void Forget()
        {
            g_shown.clear();
            g_shownBefore.clear();
            g_setting.clear();
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (ev.appeared)
            {
                if (!obj::IsA(ev.instance, L"GFSubtitleWidget")) return;
                g_widget = ev.instance;
                Forget();
                // A new widget means a new player controller: the game was left or started,
                // and a line from before that is not worth repeating.
                if (ev.instance != g_lineWidget) g_lastLine.clear();
                log::Info(L"subtitles: {} shown", obj::ClassName(ev.instance));
            }
            else if (ev.instance == g_widget)
            {
                g_widget = nullptr;
                Forget();
            }
        }

        bool Contains(const std::vector<std::wstring>& lines, const std::wstring& text)
        {
            return std::find(lines.begin(), lines.end(), text) != lines.end();
        }

        // The main menu means the game was left; a line from before is not worth repeating.
        void ForgetAtMainMenu(UObject* screen)
        {
            if (g_lastLine.empty() || !screen || !obj::IsA(screen, L"MainMenuWidget_C")) return;
            log::Info(L"subtitles: last line forgotten at the main menu");
            ResetSubtitles();
        }

        // Every frame, so that a line is read the moment it appears: the lines on screen are
        // compared with the lines of the two polls before and only new text is spoken. Two
        // new lines in one frame are one subtitle wrapped over two lines, or two speakers
        // at once; they are read together.
        void PollImpl()
        {
            if (gamethread::FrameCount() % 30 == 0)
            {
                UObject* focused = watch::CurrentFocused();
                ForgetAtMainMenu(focused ? obj::NearestAncestorOfClass(focused, L"MainMenuWidget_C") : nullptr);
            }
            if (!obj::IsLive(g_widget)) return;
            if (gamethread::FrameCount() % 60 == 0)
            {
                const auto setting = SubtitleSetting(g_widget);
                if (setting != g_setting)
                {
                    g_setting = setting;
                    log::Info(L"subtitles: setting {}", setting);
                }
            }
            std::vector<std::wstring> shown;
            std::vector<std::wstring> fresh;
            for (auto* line : Lines(g_widget))
            {
                if (!obj::IsLive(line) || !obj::IsWidgetShown(line)) continue;
                const auto text = LineText(line);
                if (text.empty()) continue;
                if (!Contains(g_shown, text) && !Contains(g_shownBefore, text) && !Contains(fresh, text)) fresh.push_back(text);
                shown.push_back(text);
            }
            g_shownBefore = std::move(g_shown);
            g_shown = std::move(shown);
            if (fresh.empty()) return;
            g_lastLine = str::Join(fresh, L" ");
            g_lineWidget = g_widget;
            log::Info(L"subtitles: line \"{}\"", g_lastLine);
            if (g_read) speech::Announce(g_lastLine);
        }

        void Poll(float)
        {
            obj::SafeInvoke([](void*) { PollImpl(); }, nullptr);
        }
    }

    void SubtitlesFeature::Install()
    {
        g_read = cfg::Get().readSubtitles;
        log::Info(L"subtitles: reading {} at start", g_read ? L"on" : L"off");
        watch::AddHudListener(&OnHud);
        hooks::OnScript(L"MenuBaseWidget_C", L"Show", [](UObject* self, FFrame&) { ForgetAtMainMenu(self); });
        gamethread::AddPoller(L"subtitles", &Poll);
    }

    void SubtitlesFeature::Describe(std::vector<std::wstring>& out)
    {
        if (!obj::IsLive(g_widget) || g_shown.empty()) return;
        out.push_back(str::Join(g_shown, L" "));
    }

    void SubtitlesFeature::Help(std::vector<std::wstring>& out)
    {
        const auto& s = cfg::Get();
        out.push_back(locale::Mod(L"help.subtitles", std::vector<std::wstring>{s.keySubtitles, s.keyLastSubtitle, input::KeyDisplayName(s.padSubtitles),
                                                                               input::KeyDisplayName(s.padLastSubtitle)}));
    }

    void ToggleSubtitles()
    {
        SetSubtitles(!g_read);
        speech::Now(locale::Mod(g_read ? L"subtitles.on" : L"subtitles.off"));
    }

    void SetSubtitles(bool read)
    {
        g_read = read;
        log::Info(L"subtitles: reading {}", g_read ? L"on" : L"off");
        // Kept in the ini, so that the next start begins the way this one ended.
        if (!cfg::Persist(L"Subtitles", L"Read", g_read ? L"1" : L"0")) log::Error(L"subtitles: the setting could not be saved to QuarryAccess.ini");
    }

    void SpeakLastSubtitle()
    {
        speech::Now(g_lastLine.empty() ? locale::Mod(L"subtitles.none") : g_lastLine);
    }

    void ResetSubtitles()
    {
        g_lastLine.clear();
        g_lineWidget = nullptr;
    }
}
