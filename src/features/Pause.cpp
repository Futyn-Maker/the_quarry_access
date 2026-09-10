#include "features/Pause.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "features/Menus.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        UObject* g_system = nullptr; // the tab bar of the pause menu
        UObject* g_tab = nullptr;    // its selected tab, while the menu is up
        bool g_active = false;
        unsigned long long g_lastScan = 0;

        std::wstring TabLine(const ui::PauseTabs& tabs)
        {
            if (tabs.label.empty()) return {};
            std::vector<std::wstring> parts{tabs.label, locale::Mod(L"ui.type.tab")};
            if (tabs.index > 0 && tabs.count > 1) parts.push_back(locale::Mod(L"ui.pos", std::to_wstring(tabs.index), std::to_wstring(tabs.count)));
            return str::Join(parts, L", ");
        }

        std::wstring PausedText()
        {
            return gametext::Resolve(L"SMG_UI_PAUSED_000001");
        }

        void PollImpl()
        {
            const auto frame = gamethread::FrameCount();
            if (frame % 4 != 0) return;
            // Looking the bar up means scanning every object, so it is done sparingly.
            if (!obj::IsLive(g_system) || (!g_active && frame - g_lastScan > 60))
            {
                if (frame - g_lastScan < 60) return;
                g_lastScan = frame;
                g_system = ui::PauseTabSystem();
                if (!g_system) return;
            }
            const auto tabs = ui::PauseTabsOf(g_system);
            const bool active = tabs.system != nullptr;
            if (active && !g_active)
            {
                log::Verbose(L"pause: opened, tab {} ({} of {})", tabs.label, tabs.index, tabs.count);
                speech::Focus(PausedText());
                ArriveWith({TabLine(tabs)});
            }
            else if (active && tabs.tab && tabs.tab != g_tab)
            {
                log::Verbose(L"pause: tab {} ({} of {})", tabs.label, tabs.index, tabs.count);
                ArriveWith({TabLine(tabs)});
            }
            g_active = active;
            g_tab = active ? tabs.tab : nullptr;
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"pause.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void PauseFeature::Install()
    {
        gamethread::AddPoller(L"pause", &Poll);
    }

    void PauseFeature::Describe(std::vector<std::wstring>& out)
    {
        const auto tabs = ui::ActivePauseTabs();
        if (!tabs.system) return;
        const auto paused = PausedText();
        if (!paused.empty()) out.push_back(paused);
        const auto line = TabLine(tabs);
        if (!line.empty()) out.push_back(line);
    }

    void PauseFeature::Help(std::vector<std::wstring>& out)
    {
        const auto tabs = ui::ActivePauseTabs();
        if (!tabs.system) return;
        const auto left = input::KeyForAction(ui::ActionName(tabs.system, L"TabLeftActionMapping"));
        const auto right = input::KeyForAction(ui::ActionName(tabs.system, L"TabRightActionMapping"));
        const auto resume = input::KeyForAction(ui::ActionName(tabs.system, L"UnpauseActionMapping"));
        if (left.empty() || right.empty() || resume.empty()) return;
        out.push_back(locale::Mod(L"help.pause", std::vector<std::wstring>{left, right, resume}));
    }
}
