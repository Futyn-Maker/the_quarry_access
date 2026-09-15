#include "features/Tarot.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        // A film player of the game and what it held at the last look. The visions play through
        // the crystal ball's player (/Game/Movies/Tarot/Tarot_BinkMediaPlayer) in the fortune
        // teller's scene and through the game's general player (/Game/Movies/BinkMediaPlayer)
        // when the tarot tab of the pause menu replays one; every player of the class is
        // watched, so a third would be found as well.
        struct Player
        {
            UObject* object = nullptr;
            std::wstring url;
            bool playing = false;
        };
        std::vector<Player> g_players;
        unsigned long long g_scannedAt = 0;
        std::wstring g_shown; // the key of the vision playing, empty when none

        // The vision a film holds: "Tarot/Sun/Sun_Dylan.bk2" names the Sun card's dawn for
        // Dylan, "Tarot/Judgement/Judgement_3.bk2" the Judgement card's third variant. Any
        // film not under Tarot is some other film of the game.
        std::wstring KeyOf(std::wstring_view url)
        {
            const auto lower = str::ToLower(url);
            if (lower.find(L"tarot/") == std::wstring::npos && lower.find(L"tarot\\") == std::wstring::npos) return {};
            const size_t slash = lower.find_last_of(L"/\\");
            std::wstring name = slash == std::wstring::npos ? lower : lower.substr(slash + 1);
            const size_t dot = name.rfind(L'.');
            if (dot != std::wstring::npos) name.erase(dot);
            name = str::ReplaceAll(name, L"_clean", L"");
            std::wstring key;
            for (const wchar_t c : name)
                if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) key += c;
            return key;
        }

        void Rescan()
        {
            std::vector<Player> players;
            for (UObject* object : obj::FindAllLive(L"BinkMediaPlayer"))
            {
                const auto known = std::find_if(g_players.begin(), g_players.end(), [&](const Player& p) { return p.object == object; });
                if (known != g_players.end())
                    players.push_back(*known);
                else
                    players.push_back(Player{object});
            }
            if (players.size() != g_players.size())
            {
                std::vector<std::wstring> names;
                for (const auto& p : players)
                    names.push_back(obj::ObjectName(p.object));
                log::Info(L"tarot: {} film player(s): {}", players.size(), str::Join(names, L", "));
            }
            g_players = std::move(players);
        }

        void PollImpl()
        {
            const auto frame = gamethread::FrameCount();
            if (frame % 15 != 0) return;
            if (g_players.empty() || frame - g_scannedAt >= 60)
            {
                g_scannedAt = frame;
                Rescan();
            }
            std::wstring shown;
            for (Player& p : g_players)
            {
                if (!obj::IsLive(p.object)) continue;
                std::wstring url;
                obj::ReadString(p.object, L"URL", url);
                const bool playing = obj::CallForBool(p.object, L"IsPlaying");
                if (url != p.url) log::Info(L"tarot: {} holds \"{}\"", obj::ObjectName(p.object), url);
                const auto key = KeyOf(url);
                if (playing && !key.empty())
                {
                    shown = key;
                    if (!p.playing || url != p.url)
                    {
                        const std::wstring text = L"tarot." + key;
                        log::Info(L"tarot: vision \"{}\" plays on {} ({})", url, obj::ObjectName(p.object),
                                  locale::Has(text) ? L"described" : L"no description");
                        if (locale::Has(text)) speech::Announce(locale::Mod(text));
                    }
                }
                p.url = url;
                p.playing = playing;
            }
            g_shown = shown;
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"tarot.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void TarotFeature::Install()
    {
        gamethread::AddPoller(L"tarot", &Poll);
    }

    void TarotFeature::Describe(std::vector<std::wstring>& out)
    {
        if (g_shown.empty()) return;
        const std::wstring text = L"tarot." + g_shown;
        if (locale::Has(text)) out.push_back(locale::Mod(text));
    }
}
