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
        UObject* g_tab = nullptr; // the selected tab, while the menu is up
        bool g_up = false;

        std::wstring PausedText()
        {
            return gametext::Resolve(L"SMG_UI_PAUSED_000001");
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 4 != 0) return;
            // The state is read at every poll: reading it only now and then once missed whole
            // openings, and a menu opened and closed again in between was taken for one.
            const auto menu = ui::PauseMenuState();
            const auto tabs = menu.up ? ui::PauseTabsOf(menu.system) : ui::PauseTabs{};
            if (!menu.up)
            {
                if (g_up) log::Info(L"pause: closed");
                g_up = false;
                g_tab = nullptr;
                return;
            }
            if (!g_up)
            {
                log::Info(L"pause: open on {} (game says {}), tab {} ({} of {})", obj::ClassName(menu.content), menu.answered ? L"yes" : L"nothing", tabs.label,
                          tabs.index, tabs.count);
                speech::Focus(PausedText());
                ArriveWith({}, ui::PauseAnchor(menu.content));
            }
            else if (tabs.tab && tabs.tab != g_tab)
            {
                log::Info(L"pause: tab {} ({} of {}) on {}", tabs.label, tabs.index, tabs.count, obj::ClassName(menu.content));
                ArriveWith({}, ui::PauseAnchor(menu.content));
            }
            g_up = true;
            g_tab = tabs.tab;
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
        const auto line = ui::PauseTabLine();
        if (!line.empty()) out.push_back(line);
    }

    void PauseFeature::Help(std::vector<std::wstring>& out)
    {
        // The tab keys are named with the screen's prompts; the key that resumes the game
        // shows no prompt.
        const auto tabs = ui::ActivePauseTabs();
        if (!tabs.system) return;
        const auto resume = input::KeyForAction(ui::ActionName(tabs.system, L"UnpauseActionMapping"));
        if (resume.empty()) return;
        out.push_back(locale::Mod(L"help.pause", resume));
    }
}
