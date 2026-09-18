#include "features/Menus.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>

namespace qa::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        UObject* g_screen = nullptr;
        UObject* g_focused = nullptr;
        std::wstring g_lastFocusText;
        std::wstring g_lastPromptText;
        double g_focusChangedAt = -1.0;

        // A screen that has just opened is announced as one ordered sequence — heading,
        // title, message, focused item, prompts — once it has settled, so the parts do
        // not cut each other. Only the first part may interrupt, and only when the
        // player asked for the screen.
        bool g_arrivalPending = false;
        double g_arrivalAt = -1.0;
        UObject* g_focusBeforeArrival = nullptr;
        std::vector<std::wstring> g_heading;

        // The prompt line always comes last, so it is held back until neither the selection
        // nor the prompts themselves have changed for this long.
        constexpr double kSettleSeconds = 0.5;
        std::wstring g_promptCandidate;
        double g_promptCandidateAt = -1.0;

        // The value of the focused control, to speak changes made with left/right, and the
        // length of a text field, which is all a password field gives away.
        UObject* g_valueWidget = nullptr;
        std::wstring g_lastValue;
        size_t g_lastLength = 0;

        // Text blocks a screen may rewrite while it stays open, watched so the new words
        // are read: a failed sign-in turns the 2K account screen into its error form.
        const wchar_t* const kScreenTexts[] = {L"Title", L"SubTitle", L"Subtitle", L"BodyText", L"Body"};
        std::vector<int> g_screenTextWatches;

        // The switchers of the screen with the page each showed at the last poll: a screen
        // that turns to another page of its own (the Wolf Pack host goes from its saves to
        // the lobby) is read again, like a section opened inside it.
        std::vector<std::pair<UObject*, int64_t>> g_pages;

        // A section opened inside a screen (a settings category, the director's chair) is
        // read like a screen; its title is repeated only when it changed.
        UObject* g_titleScreen = nullptr;
        std::wstring g_lastTitle;
        std::wstring g_lastBody;

        // The character carousel of a screen (movie mode, couch co-op): turning it does
        // not move the focus, so the card it shows is read on its own.
        UObject* g_carousel = nullptr;
        UObject* g_carouselItem = nullptr;
        unsigned long long g_lastCarouselScan = 0;

        // The selectors of the screen that the tab keys turn wherever the focus is (the
        // mode of the Wolf Pack lobby), with what each said at the last poll: turning one
        // does not move the focus either.
        std::vector<std::pair<UObject*, std::wstring>> g_tabSelectors;
        UObject* g_tabSelectorScreen = nullptr;
        unsigned long long g_lastTabSelectorScan = 0;
        std::wstring g_tabSelectorCandidate;

        // Screens announce themselves when they show. A screen class that overrides Show
        // declares its own function, so each of these is routed separately.
        const wchar_t* const kShowingScreens[] = {L"MenuBaseWidget_C",       L"PopupScreenBaseWidget_C", L"CouchCo-opHandover_C", L"PauseTabCollectablesBase_C",
                                                  L"PauseTabRelationship_C", L"RewindPause_C",           L"RewindUnlocked_C"};

        // Pause tabs are reported as the heading of their screen, not as its selection.
        bool IsTab(UObject* interactable)
        {
            return interactable && ui::KindOf(interactable) == ui::Kind::Tab;
        }

        std::wstring DescribeFocused(UObject* interactable)
        {
            if (!interactable || IsTab(interactable) || !obj::IsWidgetShown(interactable)) return {};
            auto description = ui::Describe(interactable);
            // A text field with no hint of its own is still announced, as a text field.
            if (description.label.empty() && description.kind != ui::Kind::Edit) return {};
            if (description.tip.empty() && cfg::Get().verbosity == cfg::Verbosity::Full) description.tip = ui::ContextLine(g_screen);
            return ui::Speak(description);
        }

        std::wstring CurrentPromptText()
        {
            if (cfg::Get().verbosity == cfg::Verbosity::Minimal) return {};
            return ui::SpeakPrompts(ui::Prompts(g_screen));
        }

        void StartArrival()
        {
            g_arrivalPending = true;
            g_arrivalAt = gamethread::NowSeconds();
            g_focusChangedAt = g_arrivalAt;
            // Whatever was focused a moment ago belongs to the screen being left and must
            // not be read out as the selection of this one.
            g_focusBeforeArrival = g_focused;
            g_lastPromptText.clear();
            g_promptCandidate.clear();
        }

        // The screen's own words and pages are watched from the moment it opens, so that a
        // screen which rewrites them in place says them again instead of falling silent.
        void WatchScreen(UObject* screen)
        {
            g_pages.clear();
            obj::WalkWidgetTree(screen, 16,
                                [](UObject* widget, int)
                                {
                                    int64_t page = -1;
                                    if (obj::IsA(widget, L"WidgetSwitcher") && obj::ReadInt(widget, L"ActiveWidgetIndex", page))
                                        g_pages.emplace_back(widget, page);
                                    return true;
                                });
            for (int id : g_screenTextWatches)
                watch::UnwatchText(id);
            g_screenTextWatches.clear();
            for (const wchar_t* property : kScreenTexts)
            {
                UObject* block = nullptr;
                if (!obj::ReadObject(screen, property, block) || !obj::IsLive(block)) continue;
                g_screenTextWatches.push_back(watch::WatchText(block,
                                                               [](UObject* changed, const std::wstring& text)
                                                               {
                                                                   if (g_arrivalPending || !obj::IsWidgetShown(changed)) return;
                                                                   const auto line = str::CollapseWhitespace(str::StripMarkup(text));
                                                                   if (!line.empty()) speech::Announce(line);
                                                               }));
            }
        }

        void BeginArrival(UObject* screen, const wchar_t* source)
        {
            if (!screen || screen == g_screen) return;
            g_screen = screen;
            WatchScreen(screen);
            StartArrival();
            log::Verbose(L"menus: screen via {} -> {}", source, obj::ClassName(screen));
        }

        void OnFocusChanged(UObject* screen, UObject* focused)
        {
            if (screen) BeginArrival(screen, L"focus");
            UObject* interactable = ui::Interactable(focused);
            if (!interactable || IsTab(interactable) || interactable == g_focused) return;
            g_focused = interactable;
            g_focusChangedAt = gamethread::NowSeconds();
            // While a screen is opening the item is spoken by the arrival sequence, in order.
            if (g_arrivalPending) return;
            const auto text = DescribeFocused(interactable);
            if (text.empty() || text == g_lastFocusText) return;
            g_lastFocusText = text;
            speech::Focus(text);
        }

        // The first part answers the player, so it may interrupt; the rest follows in order.
        void SpeakSequence(const std::vector<std::wstring>& parts)
        {
            bool first = true;
            for (const auto& part : parts)
            {
                if (part.empty()) continue;
                if (first)
                    speech::Focus(part);
                else
                    speech::Announce(part);
                first = false;
            }
        }

        // A screen title that only repeats the name of the pause tab is left out.
        bool RepeatsTab(const std::wstring& title)
        {
            const auto tabs = ui::ActivePauseTabs();
            if (!tabs.tab || tabs.label.empty()) return false;
            return str::EqualsNoCase(str::Trim(title.substr(0, title.find(L','))), tabs.label);
        }

        void PollArrivalImpl()
        {
            if (!g_arrivalPending) return;
            const double now = gamethread::NowSeconds();
            const double elapsed = now - g_arrivalAt;
            if (elapsed < 0.3) return;
            if (!obj::IsLive(g_screen))
            {
                // A heading may arrive before the screen it belongs to has been noticed.
                g_screen = watch::CurrentScreen();
                if (!obj::IsLive(g_screen))
                {
                    if (elapsed < 1.2) return;
                    g_arrivalPending = false;
                    SpeakSequence(g_heading);
                    g_heading.clear();
                    return;
                }
            }
            UObject* interactable = ui::Interactable(watch::CurrentFocused());
            if (interactable == g_focusBeforeArrival) interactable = nullptr;
            const auto focusText = DescribeFocused(interactable);
            // Give a screen a moment to put focus somewhere; some screens never do.
            if (focusText.empty() && elapsed < 1.2) return;

            g_arrivalPending = false;
            g_focusChangedAt = now;
            if (!focusText.empty())
            {
                g_focused = interactable;
                g_lastFocusText = focusText;
            }
            std::vector<std::wstring> parts = g_heading;
            const bool heading = !g_heading.empty();
            g_heading.clear();
            const auto title = ui::ScreenTitle(g_screen);
            const auto body = ui::ScreenBody(g_screen);
            const bool sameScreen = g_screen == g_titleScreen;
            const bool sameTitle = sameScreen && title == g_lastTitle;
            const bool sameBody = sameTitle && body == g_lastBody;
            g_titleScreen = g_screen;
            g_lastTitle = title;
            g_lastBody = body;
            const auto prompts = CurrentPromptText();
            g_lastPromptText = prompts;
            g_promptCandidate = prompts;
            // A screen read again with nothing new on it (a page turned and back) says nothing.
            if (sameBody && focusText.empty() && !heading) return;
            if (!title.empty() && !sameTitle && !RepeatsTab(title)) parts.push_back(title);
            if (!sameBody) parts.push_back(body);
            // A control the screen already names in its text (a selector the tab keys turn)
            // is not named again as the selection.
            if (body.find(focusText) == std::wstring::npos) parts.push_back(focusText);
            parts.push_back(prompts);
            SpeakSequence(parts);
        }

        // Prompts are re-read whenever the set on screen changes, which is how opening a
        // section inside a screen gets its own prompts announced. They close the readout,
        // so they wait until the screen and the selection have stopped moving.
        void PollPromptsImpl()
        {
            if (g_arrivalPending || !obj::IsLive(g_screen)) return;
            if (gamethread::FrameCount() % 8 != 0) return;
            const auto text = CurrentPromptText();
            if (text == g_lastPromptText)
            {
                g_promptCandidate = text;
                return;
            }
            const double now = gamethread::NowSeconds();
            if (text != g_promptCandidate)
            {
                g_promptCandidate = text;
                g_promptCandidateAt = now;
                return;
            }
            if (now - g_promptCandidateAt < kSettleSeconds || now - g_focusChangedAt < kSettleSeconds) return;
            g_lastPromptText = text;
            if (!text.empty()) speech::Announce(text);
        }

        void PollPagesImpl()
        {
            if (!obj::IsLive(g_screen) || gamethread::FrameCount() % 8 != 0) return;
            for (auto& [switcher, page] : g_pages)
            {
                int64_t current = -1;
                if (!obj::IsLive(switcher) || !obj::ReadInt(switcher, L"ActiveWidgetIndex", current) || current == page) continue;
                page = current;
                // Pages turned while the screen is being read are part of that reading.
                if (g_arrivalPending || !obj::IsWidgetShown(switcher)) continue;
                log::Verbose(L"menus: {} turned to page {}", obj::ObjectName(switcher), current);
                ArriveWith({});
                return;
            }
        }

        // Changing a setting keeps the focus on the same control, so the new value and the
        // description of that value are spoken.
        void PollValueImpl()
        {
            if (gamethread::FrameCount() % 4 != 0) return;
            UObject* interactable = ui::Interactable(watch::CurrentFocused());
            if (!interactable || IsTab(interactable))
            {
                g_valueWidget = nullptr;
                g_lastValue.clear();
                g_lastLength = 0;
                return;
            }
            // A text field is followed as it is typed into: only what was added is read
            // back. A password field draws a dot per letter, so it ticks instead.
            if (ui::KindOf(interactable) == ui::Kind::Edit)
            {
                const auto edit = ui::EditField(interactable);
                const bool same = interactable == g_valueWidget;
                const auto previous = g_lastValue;
                const size_t previousLength = g_lastLength;
                g_valueWidget = interactable;
                g_lastValue = edit.text;
                g_lastLength = edit.length;
                if (!same || edit.length == previousLength) return;
                g_focusChangedAt = gamethread::NowSeconds();
                if (edit.hidden)
                {
                    sounds::Tick(std::min(1.0, static_cast<double>(edit.length) / 16.0));
                    return;
                }
                const bool appended = edit.length > previousLength && edit.text.compare(0, previous.size(), previous) == 0;
                const auto said = appended ? edit.text.substr(previous.size()) : edit.text;
                if (!said.empty()) speech::Focus(said);
                return;
            }

            const auto description = ui::Describe(interactable);
            if (interactable != g_valueWidget)
            {
                g_valueWidget = interactable;
                g_lastValue = description.value;
                return;
            }
            if (description.value.empty() || description.value == g_lastValue) return;
            g_lastValue = description.value;
            g_focusChangedAt = gamethread::NowSeconds();
            std::vector<std::wstring> parts{description.value};
            if (!description.tip.empty() && description.tip != description.label) parts.push_back(description.tip);
            speech::Focus(str::Join(parts, L", "));
        }

        void PollCarouselImpl()
        {
            const auto frame = gamethread::FrameCount();
            if (frame % 4 != 0) return;
            if (!obj::IsLive(g_carousel) || !obj::IsWidgetShown(g_carousel))
            {
                if (frame - g_lastCarouselScan < 30) return;
                g_lastCarouselScan = frame;
                g_carousel = nullptr;
                for (auto* carousel : obj::FindAllLive(L"CharacterCarousel_C"))
                {
                    if (obj::IsWidgetShown(carousel)) g_carousel = carousel;
                }
                if (!g_carousel)
                {
                    g_carouselItem = nullptr;
                    return;
                }
            }
            UObject* item = nullptr;
            obj::ReadObject(g_carousel, L"CurrentCarouselItem", item);
            if (item == g_carouselItem) return;
            const bool first = g_carouselItem == nullptr;
            g_carouselItem = item;
            // When the screen is being read the card is part of that readout.
            if (first || g_arrivalPending) return;
            g_focusChangedAt = gamethread::NowSeconds();
            const auto text = ui::CarouselText(g_carousel);
            if (!text.empty()) speech::Focus(text);
        }

        void PollTabSelectorsImpl()
        {
            const auto frame = gamethread::FrameCount();
            if (frame % 4 != 0 || !obj::IsLive(g_screen)) return;
            if (g_screen != g_tabSelectorScreen || frame - g_lastTabSelectorScan > 30)
            {
                // What a selector said is kept across scans; one found anew is taken as it
                // is, since the screen that shows it reads it.
                std::vector<std::pair<UObject*, std::wstring>> found;
                for (auto* selector : ui::TabSelectors(g_screen))
                {
                    const auto known = std::find_if(g_tabSelectors.begin(), g_tabSelectors.end(), [&](const auto& s) { return s.first == selector; });
                    const bool keep = g_screen == g_tabSelectorScreen && known != g_tabSelectors.end();
                    found.emplace_back(selector, keep ? known->second : ui::Speak(ui::Describe(selector)));
                }
                g_tabSelectors = std::move(found);
                g_tabSelectorScreen = g_screen;
                g_lastTabSelectorScan = frame;
            }
            UObject* focused = ui::Interactable(watch::CurrentFocused());
            for (auto& [selector, said] : g_tabSelectors)
            {
                if (!obj::IsLive(selector) || selector == focused) continue;
                const auto text = ui::Speak(ui::Describe(selector));
                if (text == said) continue;
                // The description beside the selector is written a moment after its value,
                // so a new reading is taken once it holds for two polls.
                if (text != g_tabSelectorCandidate)
                {
                    g_tabSelectorCandidate = text;
                    continue;
                }
                said = text;
                if (g_arrivalPending) continue;
                g_focusChangedAt = gamethread::NowSeconds();
                speech::Focus(text);
            }
        }

        void PollTabSelectors(float)
        {
            obj::SafeInvokeLogged(L"menus.PollTabSelectorsImpl", [](void*) { PollTabSelectorsImpl(); }, nullptr);
        }

        void PollArrival(float)
        {
            obj::SafeInvokeLogged(L"menus.PollArrivalImpl", [](void*) { PollArrivalImpl(); }, nullptr);
        }

        void PollCarousel(float)
        {
            obj::SafeInvokeLogged(L"menus.PollCarouselImpl", [](void*) { PollCarouselImpl(); }, nullptr);
        }

        void PollPrompts(float)
        {
            obj::SafeInvokeLogged(L"menus.PollPromptsImpl", [](void*) { PollPromptsImpl(); }, nullptr);
        }

        void PollPages(float)
        {
            obj::SafeInvokeLogged(L"menus.PollPagesImpl", [](void*) { PollPagesImpl(); }, nullptr);
        }

        void PollValue(float)
        {
            obj::SafeInvokeLogged(L"menus.PollValueImpl", [](void*) { PollValueImpl(); }, nullptr);
        }
    }

    void ArriveWith(std::vector<std::wstring> heading)
    {
        g_heading = std::move(heading);
        StartArrival();
    }

    bool ArrivalPending()
    {
        return g_arrivalPending;
    }

    void ResetMenus()
    {
        g_screen = nullptr;
        g_focused = nullptr;
        g_focusBeforeArrival = nullptr;
        g_valueWidget = nullptr;
        g_lastFocusText.clear();
        g_lastPromptText.clear();
        g_promptCandidate.clear();
        g_lastValue.clear();
        g_lastLength = 0;
        g_screenTextWatches.clear();
        g_pages.clear();
        g_heading.clear();
        g_arrivalPending = false;
        g_titleScreen = nullptr;
        g_lastTitle.clear();
        g_lastBody.clear();
        g_carousel = nullptr;
        g_carouselItem = nullptr;
        g_tabSelectors.clear();
        g_tabSelectorScreen = nullptr;
        g_tabSelectorCandidate.clear();
    }

    void MenusFeature::Install()
    {
        watch::AddFocusListener([](UObject* screen, UObject* focused) { OnFocusChanged(screen, focused); });
        for (const wchar_t* screenClass : kShowingScreens)
        {
            hooks::OnScript(screenClass, L"Show", [](UObject* self, FFrame&) { BeginArrival(self, L"Show"); });
        }
        for (const wchar_t* function : {L"ShowNestedContent", L"PopNestedContent"})
        {
            hooks::OnScript(L"NestedContentMenu_C", function, [](UObject*, FFrame&) { ArriveWith({}); });
        }
        gamethread::AddPoller(L"menus.arrival", &PollArrival);
        gamethread::AddPoller(L"menus.carousel", &PollCarousel);
        gamethread::AddPoller(L"menus.prompts", &PollPrompts);
        gamethread::AddPoller(L"menus.value", &PollValue);
        gamethread::AddPoller(L"menus.pages", &PollPages);
        gamethread::AddPoller(L"menus.tabselectors", &PollTabSelectors);
    }

    void MenusFeature::Describe(std::vector<std::wstring>& out)
    {
        UObject* screen = watch::CurrentScreen();
        if (!screen) return;
        UObject* interactable = ui::Interactable(watch::CurrentFocused());
        if (interactable && !IsTab(interactable))
        {
            const auto title = ui::ScreenTitle(screen);
            if (!title.empty() && !RepeatsTab(title)) out.push_back(title);
            const auto body = ui::ScreenBody(screen);
            if (!body.empty()) out.push_back(body);
            auto description = ui::Describe(interactable);
            if (description.tip.empty()) description.tip = ui::ContextLine(screen);
            const auto focused = ui::Speak(description);
            if (body.find(focused) == std::wstring::npos) out.push_back(focused);
        }
        else
        {
            // Nothing is selected: everything the screen shows is read.
            const auto text = ui::ScreenText(screen);
            if (!text.empty()) out.push_back(text);
        }
        const auto prompts = ui::SpeakPrompts(ui::Prompts(screen));
        if (!prompts.empty()) out.push_back(prompts);
    }

    void MenusFeature::Help(std::vector<std::wstring>& out)
    {
        UObject* screen = watch::CurrentScreen();
        if (!screen) return;
        // On a screen with a text field the arrow keys move the caret rather than the
        // selection, and the game walks the screen with the tab key instead.
        if (ui::HasTextField(screen))
            out.push_back(locale::Mod(L"help.form", input::KeyDisplayName(L"Tab"), input::KeyForAction(L"UINavigationConfirm")));
        else
            out.push_back(locale::Mod(L"help.menu", input::KeyForAction(L"UINavigationConfirm"), input::KeyForAction(L"UINavigationCancel")));
        const auto prompts = ui::SpeakPrompts(ui::Prompts(screen));
        if (!prompts.empty()) out.push_back(prompts);
    }
}
