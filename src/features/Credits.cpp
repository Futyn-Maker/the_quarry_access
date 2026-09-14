#include "features/Credits.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/GameText.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        // A roll at the end of the game. MainCredits_C adds a line widget to its CreditsCanvas for
        // each row of its credits data as the row reaches the bottom edge, counts the rows added so
        // far in CurrentIndex, and removes a line once it has left the top, so the canvas holds the
        // lines on screen from top to bottom with the newest last.
        struct Roll
        {
            UObject* widget = nullptr;
            int64_t added = 0;  // rows the roll had added at the last poll
            bool begun = false; // the roll has been announced
        };
        Roll g_roll;

        // The opening credits: IntroCreditsWidget_C shows one IntroCredit_C at a time in its left,
        // centre or right box, and each removes itself once it has faded.
        UObject* g_intro = nullptr;
        std::vector<std::wstring> g_introSaid; // the names said for that widget

        // Lines joined into sentences: a line that does not end in punctuation of its own closes one.
        std::wstring Sentences(const std::vector<std::wstring>& lines)
        {
            std::wstring out;
            for (const auto& raw : lines)
            {
                const auto line = str::CollapseWhitespace(raw);
                if (line.empty()) continue;
                if (!out.empty())
                {
                    if (std::wstring_view(L".,;:!?").find(out.back()) == std::wstring_view::npos) out += L'.';
                    out += L' ';
                }
                out += line;
            }
            return out;
        }

        // A block the credits write over several lines, such as a licensed song.
        std::wstring Block(std::wstring_view text)
        {
            return Sentences(str::Split(text, L'\n'));
        }

        std::wstring FirstLine(std::wstring_view text)
        {
            return str::CollapseWhitespace(text.substr(0, text.find(L'\n')));
        }

        // The words of a row as the credits data holds them; the line widgets draw names and
        // section titles in capitals.
        void ReadCredit(UObject* line, std::wstring& title, std::wstring& name)
        {
            auto* credit = obj::FindProperty(line, L"Credit");
            void* value = credit ? obj::ValuePtr(line, credit) : nullptr;
            if (!value) return;
            if (auto* member = obj::StructMember(credit, L"CreditTitle")) obj::ReadStringAt(value, member, title);
            if (auto* member = obj::StructMember(credit, L"CreditName")) obj::ReadStringAt(value, member, name);
        }

        // The name inside the keyword of an image row ("[IMAGE:WWISE]"), which picks the logo drawn.
        std::wstring LogoName(std::wstring_view keyword)
        {
            const size_t colon = keyword.find(L':');
            const size_t close = keyword.rfind(L']');
            if (colon == std::wstring_view::npos || close == std::wstring_view::npos || close <= colon) return str::CollapseWhitespace(keyword);
            return str::CollapseWhitespace(keyword.substr(colon + 1, close - colon - 1));
        }

        // What one line of a roll shows: a role and a name side by side, a name, a section title or
        // a block, a logo. A spacer shows nothing.
        std::wstring LineText(UObject* line)
        {
            std::wstring title;
            std::wstring name;
            ReadCredit(line, title, name);
            if (obj::IsA(line, L"MainCreditSpanTitle_C") || obj::IsA(line, L"MainCreditBodyText_C")) return Block(title);
            if (obj::IsA(line, L"MainCreditImageBase_C")) return title.empty() ? std::wstring() : locale::Mod(L"credits.logo", LogoName(title));
            if (!obj::IsA(line, L"MainCredit_C")) return {};
            title = str::CollapseWhitespace(title);
            name = str::CollapseWhitespace(name);
            if (title.empty() || name.empty()) return title.empty() ? name : title;
            return locale::Mod(L"credits.pair", title, name);
        }

        std::vector<UObject*> RollLines(UObject* roll)
        {
            UObject* canvas = nullptr;
            if (!obj::ReadObject(roll, L"CreditsCanvas", canvas) || !obj::IsLive(canvas)) return {};
            return obj::PanelChildren(canvas);
        }

        // The opening credits on screen, in the order of their places: left, centre, right.
        std::vector<UObject*> IntroCredits(UObject* widget)
        {
            std::vector<UObject*> credits;
            for (const wchar_t* place : {L"BoxLeft", L"BoxCenter", L"BoxRight"})
            {
                UObject* box = nullptr;
                if (!obj::ReadObject(widget, place, box) || !obj::IsLive(box)) continue;
                for (UObject* child : obj::PanelChildren(box))
                {
                    if (obj::IsA(child, L"IntroCredit_C") && obj::IsWidgetShown(child, true)) credits.push_back(child);
                }
            }
            return credits;
        }

        // An opening credit as it is shown: the name, and on the last one the game's word for "and"
        // above it.
        std::wstring IntroText(UObject* credit)
        {
            std::wstring name;
            obj::ReadString(credit, L"Text", name);
            name = str::CollapseWhitespace(name);
            if (name.empty()) return {};
            bool last = false;
            if (obj::ReadBool(credit, L"bIsLastCredit", last) && last)
            {
                const auto word = str::CollapseWhitespace(gametext::ReadLocalized(credit, L"AndString"));
                if (!word.empty()) return word + L" " + name;
            }
            return name;
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (!ev.appeared || !obj::IsLive(ev.instance)) return;
            if (obj::IsA(ev.instance, L"CreditsWidgetSMG026") && ev.instance != g_roll.widget)
            {
                g_roll = Roll{ev.instance};
                log::Info(L"credits: {} {} shown", obj::ClassName(ev.instance), obj::ObjectName(ev.instance));
            }
            else if (obj::IsA(ev.instance, L"IntroCreditsWidgetSMG026") && ev.instance != g_intro)
            {
                g_intro = ev.instance;
                g_introSaid.clear();
                log::Info(L"credits: {} {} shown", obj::ClassName(ev.instance), obj::ObjectName(ev.instance));
            }
        }

        // The roll says that it has begun and then each section title as it comes up from the bottom.
        // A roll moves faster than speech, so the lines between the titles are left to the readout;
        // of a block over several lines the first line is said, which names it.
        void PollRoll()
        {
            if (!g_roll.widget) return;
            if (!obj::IsLive(g_roll.widget))
            {
                g_roll = Roll{};
                return;
            }
            int64_t added = 0;
            if (!obj::ReadInt(g_roll.widget, L"CurrentIndex", added)) return;
            if (added < g_roll.added)
            {
                // The same widget started its roll again.
                g_roll.added = 0;
                g_roll.begun = false;
            }
            const int64_t fresh = added - g_roll.added;
            g_roll.added = added;
            // Rows that came up while the roll was not on screen were seen by nobody.
            if (fresh <= 0 || !obj::IsWidgetShown(g_roll.widget, true)) return;
            if (!g_roll.begun)
            {
                g_roll.begun = true;
                UObject* data = nullptr;
                obj::ReadObject(g_roll.widget, L"CreditsData", data);
                log::Info(L"credits: the roll of {} begins", obj::IsLive(data) ? obj::ObjectName(data) : std::wstring(L"the default credits"));
                speech::Announce(locale::Mod(L"credits.start"));
            }
            const auto lines = RollLines(g_roll.widget);
            const size_t count = std::min(lines.size(), static_cast<size_t>(fresh));
            for (size_t i = lines.size() - count; i < lines.size(); ++i)
            {
                if (!obj::IsA(lines[i], L"MainCreditSpanTitle_C")) continue;
                std::wstring title;
                std::wstring name;
                ReadCredit(lines[i], title, name);
                const auto heading = FirstLine(title);
                if (heading.empty()) continue;
                log::Info(L"credits: section \"{}\"", heading);
                speech::Announce(heading);
            }
        }

        // Each opening credit is said once, the moment it is on screen.
        void PollIntro()
        {
            if (!g_intro) return;
            if (!obj::IsLive(g_intro))
            {
                g_intro = nullptr;
                g_introSaid.clear();
                return;
            }
            for (UObject* credit : IntroCredits(g_intro))
            {
                const auto text = IntroText(credit);
                if (text.empty() || std::find(g_introSaid.begin(), g_introSaid.end(), text) != g_introSaid.end()) continue;
                g_introSaid.push_back(text);
                log::Info(L"credits: opening credit \"{}\"", text);
                speech::Announce(text);
            }
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 3 != 0) return;
            PollRoll();
            PollIntro();
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"credits.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void CreditsFeature::Install()
    {
        watch::AddHudListener(&OnHud);
        gamethread::AddPoller(L"credits", &Poll);
    }

    void CreditsFeature::Describe(std::vector<std::wstring>& out)
    {
        if (obj::IsLive(g_intro))
        {
            std::vector<std::wstring> names;
            for (UObject* credit : IntroCredits(g_intro))
                names.push_back(IntroText(credit));
            const auto text = Sentences(names);
            if (!text.empty()) out.push_back(text);
        }
        if (obj::IsLive(g_roll.widget) && obj::IsWidgetShown(g_roll.widget, true))
        {
            std::vector<std::wstring> lines;
            for (UObject* line : RollLines(g_roll.widget))
                lines.push_back(LineText(line));
            const auto text = Sentences(lines);
            if (!text.empty()) out.push_back(text);
        }
    }
}
