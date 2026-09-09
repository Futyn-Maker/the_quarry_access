#include "features/Hud.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <vector>

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        // HUD elements whose text stands on its own. Elements the player acts on (choices,
        // prompts, quick-time events, subtitles) belong to their own features.
        const wchar_t* const kReadableHuds[] = {L"ActionHUDSceneDetails", L"ActionHUDNotification", L"ActionHUDAlertSMG026",     L"ActionHUDActDisplaySMG026",
                                                L"ActionHUDSingleLine",   L"ActionHUDTitleIntro",   L"ActionHUDCharacterIntros", L"ActionHUDBigTitlesDP"};

        struct Tracked
        {
            UObject* instance = nullptr;
            std::wstring seen;   // text at the last poll
            std::wstring spoken; // text last announced for this element
            int stablePolls = 0;
        };
        std::vector<Tracked> g_tracked;
        std::wstring g_lastText;
        double g_lastTextAt = -1.0;

        bool Readable(UObject* hud)
        {
            for (const wchar_t* name : kReadableHuds)
            {
                if (obj::IsA(hud, name)) return true;
            }
            return false;
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (!ev.appeared)
            {
                std::erase_if(g_tracked, [&](const Tracked& t) { return t.instance == ev.instance; });
                return;
            }
            if (obj::IsA(ev.hud, L"ActionHUDLoadingScreen"))
            {
                speech::Announce(locale::Mod(L"hud.loading"));
                return;
            }
            if (obj::IsA(ev.hud, L"ActionHUDSavingIcon"))
            {
                speech::Announce(locale::Mod(L"hud.saving"));
                return;
            }
            if (!Readable(ev.hud)) return;
            if (std::any_of(g_tracked.begin(), g_tracked.end(), [&](const Tracked& t) { return t.instance == ev.instance; })) return;
            g_tracked.push_back(Tracked{ev.instance});
        }

        // An element is read once its text has stopped changing: a caption is filled in a
        // moment after the element appears, and the same element can show several
        // messages in a row.
        void PollImpl()
        {
            if (gamethread::FrameCount() % 6 != 0) return;
            const double now = gamethread::NowSeconds();
            for (auto it = g_tracked.begin(); it != g_tracked.end();)
            {
                if (!obj::IsLive(it->instance))
                {
                    it = g_tracked.erase(it);
                    continue;
                }
                if (obj::IsWidgetVisible(it->instance))
                {
                    const auto text = ui::HudText(it->instance);
                    if (text != it->seen)
                    {
                        it->seen = text;
                        it->stablePolls = 0;
                    }
                    else if (!text.empty() && ++it->stablePolls >= 2 && text != it->spoken)
                    {
                        it->spoken = text;
                        // The same caption can arrive through two HUD slots at once.
                        if (text != g_lastText || now - g_lastTextAt > 5.0)
                        {
                            g_lastText = text;
                            g_lastTextAt = now;
                            speech::Announce(text);
                        }
                    }
                }
                ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvoke([](void*) { PollImpl(); }, nullptr);
        }
    }

    void HudFeature::Install()
    {
        watch::AddHudListener(&OnHud);
        gamethread::AddPoller(L"hud.text", &Poll);
    }
}
