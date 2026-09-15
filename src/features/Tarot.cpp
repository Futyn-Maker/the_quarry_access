#include "features/Tarot.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        // The two video players the game plays visions through: the one in the crystal ball,
        // and the one the tarot tab of the pause menu replays them with.
        const wchar_t* const kPlayers[] = {L"/Game/Movies/Tarot/Tarot_BinkMediaPlayer.Tarot_BinkMediaPlayer",
                                           L"/Game/Movies/BinkMediaPlayer/BinkMediaPlayer.BinkMediaPlayer"};

        struct Player
        {
            std::wstring url;     // the video it holds, at the last look
            bool playing = false; // and whether it was playing
        };
        Player g_players[2];
        std::wstring g_shown; // the key of the vision playing, empty when none

        // The vision a video file holds: "Tarot/Sun/Sun_Dylan.bk2" names the Sun card's dawn
        // for Dylan, "Tarot/Judgement/Judgement_3.bk2" the Judgement card's third variant.
        // Anything not under Tarot is some other film of the game.
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

        void PollImpl()
        {
            if (gamethread::FrameCount() % 15 != 0) return;
            std::wstring shown;
            for (size_t i = 0; i < std::size(kPlayers); ++i)
            {
                Player& p = g_players[i];
                UObject* player = obj::FindObject(kPlayers[i]);
                if (!player || !obj::IsLive(player))
                {
                    p = Player{};
                    continue;
                }
                std::wstring url;
                obj::ReadString(player, L"URL", url);
                const bool playing = obj::CallForBool(player, L"IsPlaying");
                if (url != p.url) log::Info(L"tarot: player {} holds \"{}\"", i, url);
                const auto key = KeyOf(url);
                if (playing && !key.empty())
                {
                    shown = key;
                    if (!p.playing || url != p.url)
                    {
                        const std::wstring text = L"tarot." + key;
                        log::Info(L"tarot: vision \"{}\" plays on player {} ({})", url, i, locale::Has(text) ? L"described" : L"no description");
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
