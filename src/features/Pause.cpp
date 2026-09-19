#include "features/Pause.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "features/Menus.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        UObject* g_tab = nullptr; // the selected tab, while the menu is up
        bool g_up = false;
        bool g_leaving = false;   // the player answered the quit popup by leaving
        bool g_dismissed = false; // they answered it by staying, and the menu is theirs again

        std::wstring PausedText()
        {
            return gametext::Resolve(L"SMG_UI_PAUSED_000001");
        }

        // The game hands the pause screen the answer to its quit popup as the number of the
        // button pressed. The popup is an asset of its own, and its buttons are "Yes", which
        // confirms, and "No", which cancels, in that order.
        void OnQuitAnswered(UObject*, RC::Unreal::FFrame& stack)
        {
            int64_t chosen = -1;
            params::Int(stack, L"PopupIndexSelected", chosen);
            g_leaving = chosen == 0;
            g_dismissed = !g_leaving;
            log::Info(L"pause: the quit popup was answered {}, the player {}", chosen, g_leaving ? L"is leaving" : L"stays");
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 4 != 0) return;
            // The state is read at every poll: reading it only now and then once missed whole
            // openings, and a menu opened and closed again in between was taken for one.
            const auto menu = ui::PauseMenuState();
            const auto tabs = menu.up ? ui::PauseTabsOf(menu.system) : ui::PauseTabs{};
            // The way out ends where the game arrives: at a menu screen of the frontend.
            if (g_leaving && obj::IsA(watch::CurrentScreen(), L"MenuBaseWidget_C"))
            {
                log::Info(L"pause: the game has arrived, the world is spoken about again");
                g_leaving = false;
            }
            // A popup closing over the menu leaves the focus nowhere, so nothing else would
            // read the menu the player has just chosen to stay in.
            if (g_dismissed)
            {
                g_dismissed = false;
                if (menu.up) ArriveWith({}, ui::PauseAnchor(menu.content));
            }
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
        if (!hooks::OnNative(L"/Script/SMG026Runtime.PauseScreenBaseSMG026:OnQuitToFEPopupEnded", &OnQuitAnswered, nullptr))
            log::Info(L"pause: the quit popup answer cannot be followed");
        gamethread::AddPoller(L"pause", &Poll);
    }

    bool LeavingTheGame()
    {
        return g_leaving;
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
