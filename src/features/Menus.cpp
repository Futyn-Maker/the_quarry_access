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
        double g_screenGoneAt = -1.0; // when the screen was first found no longer drawn
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

        // The group of the list the selection sits in (the clues are grouped by where they
        // belong), so that moving into another group names it once, before the item.
        UObject* g_group = nullptr;

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
        // declares its own function, so each of these is routed separately. The pause tabs
        // are not among them: their content is shown before the menu around it is up, and the
        // pause feature reads the menu as a whole once it is.
        const wchar_t* const kShowingScreens[] = {L"MenuBaseWidget_C", L"PopupScreenBaseWidget_C", L"CouchCo-opHandover_C", L"RewindPause_C",
                                                  L"RewindUnlocked_C"};

        // Pause tabs are reported as the heading of their screen, not as its selection.
        bool IsTab(UObject* interactable)
        {
            return interactable && ui::KindOf(interactable) == ui::Kind::Tab;
        }

        // The name of the group the selection has just moved into, once; a list that has no
        // groups, and a move within one, say nothing.
        std::wstring GroupHeading(UObject* interactable)
        {
            UObject* group = interactable ? obj::NearestAncestorOfClass(interactable, L"CollectablesGroup_C") : nullptr;
            // A selection that belongs to no group is beside the list, not in it (a line of
            // the panel next to a clue): the group stands, so coming back to the clue does
            // not name its section over again.
            if (!group || group == g_group) return {};
            g_group = group;
            auto heading = str::CollapseWhitespace(ui::PropertyText(group, L"Title"));
            while (!heading.empty() && (heading.back() == L':' || heading.back() == L'：'))
                heading.pop_back();
            return str::Trim(heading);
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
            g_group = nullptr;
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

        // A screen becomes the current one without being read: the reading is arranged by
        // whoever knew the screen changed.
        void AdoptScreen(UObject* screen)
        {
            g_screen = screen;
            g_screenGoneAt = -1.0;
            WatchScreen(screen);
        }

        void BeginArrival(UObject* screen, const wchar_t* source)
        {
            if (!screen) return;
            // The pause menu is one screen built of several widgets: the tab bar, the content
            // of the selected tab and the character carousel. The focus travels between them
            // as the player walks the tabs, so they all answer to the content on display, and
            // a bar or a carousel with none of it drawn is nothing to read.
            if (ui::IsPauseWidget(screen))
            {
                screen = ui::PauseAnchor(screen);
                if (!screen) return;
            }
            if (screen == g_screen) return;
            AdoptScreen(screen);
            StartArrival();
            log::Verbose(L"menus: screen via {} -> {}", source, obj::ClassName(screen));
        }

        // A screen no longer on display. The pause menu is made of widgets that come and go in
        // their own order and are left drawn behind it, so there the menu being up is what
        // counts, and nothing else is asked of them.
        bool ScreenGone(UObject* screen)
        {
            if (!obj::IsLive(screen)) return true;
            if (ui::IsPauseWidget(screen))
            {
                const auto menu = ui::PauseMenuState();
                return !menu.up || (obj::IsLive(menu.content) && menu.content != screen);
            }
            return !obj::IsWidgetShown(screen);
        }

        // A screen that is gone stops being the current one, so that what it still holds is
        // not read out and opening it again is read again. Screens go out of sight for a
        // moment while they animate, so it takes a moment of absence.
        void PollScreenImpl()
        {
            if (gamethread::FrameCount() % 8 != 0 || !obj::IsLive(g_screen) || g_arrivalPending) return;
            if (!ScreenGone(g_screen))
            {
                g_screenGoneAt = -1.0;
                return;
            }
            const double now = gamethread::NowSeconds();
            if (g_screenGoneAt < 0.0)
            {
                g_screenGoneAt = now;
                return;
            }
            if (now - g_screenGoneAt < 0.25) return;
            log::Verbose(L"menus: {} closed", obj::ClassName(g_screen));
            g_screen = nullptr;
            g_screenGoneAt = -1.0;
            g_focused = nullptr;
            g_titleScreen = nullptr;
            g_lastTitle.clear();
            g_lastBody.clear();
            g_lastFocusText.clear();
            g_lastPromptText.clear();
            g_promptCandidate.clear();
            g_group = nullptr;
            g_pages.clear();
            for (int id : g_screenTextWatches)
                watch::UnwatchText(id);
            g_screenTextWatches.clear();
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
            const auto heading = GroupHeading(interactable);
            const auto text = DescribeFocused(interactable);
            if (text.empty() || text == g_lastFocusText) return;
            g_lastFocusText = text;
            speech::Focus(str::JoinSentences({heading, text}));
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
                if (UObject* pause = ui::PauseAnchor(g_screen)) g_screen = pause;
                if (!obj::IsLive(g_screen))
                {
                    if (elapsed < 1.2) return;
                    g_arrivalPending = false;
                    SpeakSequence(g_heading);
                    g_heading.clear();
                    return;
                }
            }
            // The pause menu shows the content of a tab a moment after its bar says which tab
            // that is, and a tab with nothing to select (the character tab) never moves the
            // focus into it: the reading follows the menu to whatever it ends up showing,
            // instead of being held to the tab that was on display when it was asked for.
            if (UObject* content = ui::PauseAnchor(g_screen); content && content != g_screen) AdoptScreen(content);
            // A screen closed before its reading was due is not read at all: the player has
            // already left it.
            if (ScreenGone(g_screen))
            {
                if (elapsed < 1.2) return;
                g_arrivalPending = false;
                g_heading.clear();
                log::Info(L"menus: {} was gone before it could be read", obj::ClassName(g_screen));
                return;
            }
            const bool pause = ui::PauseAnchor(g_screen) == g_screen;
            UObject* interactable = ui::Interactable(watch::CurrentFocused());
            if (interactable == g_focusBeforeArrival) interactable = nullptr;
            const auto focusText = DescribeFocused(interactable);
            // Give a screen a moment to put focus somewhere; a screen with nothing to select
            // (the character tab of the pause menu) never does, and is read at once.
            if (focusText.empty() && elapsed < 1.2 && ui::HasControls(g_screen)) return;

            g_arrivalPending = false;
            g_focusChangedAt = now;
            if (!focusText.empty())
            {
                g_focused = interactable;
                g_lastFocusText = focusText;
            }
            std::vector<std::wstring> parts = g_heading;
            // The pause menu is headed by its tab, whatever made it be read.
            if (parts.empty() && pause) parts.push_back(ui::PauseTabLine());
            const bool heading = !parts.empty() && !parts.front().empty();
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
            if (body.find(focusText) == std::wstring::npos) parts.push_back(str::JoinSentences({GroupHeading(interactable), focusText}));
            parts.push_back(prompts);
            SpeakSequence(parts);
        }

        // Prompts are re-read whenever the set on screen changes, which is how opening a
        // section inside a screen gets its own prompts announced. They close the readout,
        // so they wait until the screen and the selection have stopped moving.
        void PollPromptsImpl()
        {
            // A screen on its way out loses its prompts one by one; none of that is news.
            if (g_arrivalPending || ScreenGone(g_screen)) return;
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

        void PollScreen(float)
        {
            obj::SafeInvokeLogged(L"menus.PollScreenImpl", [](void*) { PollScreenImpl(); }, nullptr);
        }

        void PollValue(float)
        {
            obj::SafeInvokeLogged(L"menus.PollValueImpl", [](void*) { PollValueImpl(); }, nullptr);
        }
    }

    void ArriveWith(std::vector<std::wstring> heading, UObject* screen)
    {
        g_heading = std::move(heading);
        if (obj::IsLive(screen) && screen != g_screen) AdoptScreen(screen);
        StartArrival();
    }

    bool ArrivalPending()
    {
        return g_arrivalPending;
    }

    void ResetMenus()
    {
        g_screen = nullptr;
        g_screenGoneAt = -1.0;
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
        g_group = nullptr;
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
        gamethread::AddPoller(L"menus.screen", &PollScreen);
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
