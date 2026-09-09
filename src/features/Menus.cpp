#include "features/Menus.hpp"

#include "core/Config.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

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

        // A screen that has just opened is announced as one ordered sequence — title, then
        // focused item, then prompts — once it has settled, so the parts do not cut each other.
        bool g_arrivalPending = false;
        double g_arrivalAt = -1.0;
        UObject* g_focusBeforeArrival = nullptr;

        // The prompt line always comes last, so it is held back until neither the selection
        // nor the prompts themselves have changed for this long.
        constexpr double kSettleSeconds = 0.5;
        std::wstring g_promptCandidate;
        double g_promptCandidateAt = -1.0;

        // The value of the focused control, to speak changes made with left/right.
        UObject* g_valueWidget = nullptr;
        std::wstring g_lastValue;

        std::wstring DescribeFocused(UObject* interactable)
        {
            if (!interactable || !obj::IsWidgetVisible(interactable)) return {};
            auto description = ui::Describe(interactable);
            if (description.label.empty()) return {};
            if (description.tip.empty() && cfg::Get().verbosity == cfg::Verbosity::Full) description.tip = ui::ContextLine(g_screen);
            return ui::Speak(description);
        }

        std::wstring CurrentPromptText()
        {
            if (cfg::Get().verbosity == cfg::Verbosity::Minimal) return {};
            return ui::SpeakPrompts(ui::Prompts(g_screen));
        }

        void BeginArrival(UObject* screen, const wchar_t* source)
        {
            if (!screen || screen == g_screen) return;
            g_screen = screen;
            g_arrivalPending = true;
            g_arrivalAt = gamethread::NowSeconds();
            g_focusChangedAt = g_arrivalAt;
            // Whatever was focused a moment ago belongs to the screen being left and must
            // not be read out as the selection of this one.
            g_focusBeforeArrival = g_focused;
            g_lastPromptText.clear();
            g_promptCandidate.clear();
            log::Verbose(L"menus: screen via {} -> {}", source, obj::ClassName(screen));
        }

        void OnFocusChanged(UObject* screen, UObject* focused)
        {
            if (screen) BeginArrival(screen, L"focus");
            UObject* interactable = ui::Interactable(focused);
            if (!interactable || interactable == g_focused) return;
            g_focused = interactable;
            g_focusChangedAt = gamethread::NowSeconds();
            // While a screen is opening the item is spoken by the arrival sequence, in order.
            if (g_arrivalPending) return;
            const auto text = DescribeFocused(interactable);
            if (text.empty() || text == g_lastFocusText) return;
            g_lastFocusText = text;
            speech::Focus(text);
        }

        void PollArrivalImpl()
        {
            if (!g_arrivalPending) return;
            if (!obj::IsLive(g_screen))
            {
                g_arrivalPending = false;
                return;
            }
            const double now = gamethread::NowSeconds();
            const double elapsed = now - g_arrivalAt;
            if (elapsed < 0.3) return;
            UObject* interactable = ui::Interactable(watch::CurrentFocused());
            if (interactable == g_focusBeforeArrival) interactable = nullptr;
            const auto focusText = DescribeFocused(interactable);
            // Give a screen a moment to put focus somewhere; some screens never do.
            if (focusText.empty() && elapsed < 1.2) return;

            g_arrivalPending = false;
            g_focusChangedAt = now;
            if (interactable)
            {
                g_focused = interactable;
                g_lastFocusText = focusText;
            }
            const auto title = ui::ScreenTitle(g_screen);
            const auto prompts = CurrentPromptText();
            g_lastPromptText = prompts;
            g_promptCandidate = prompts;
            if (!title.empty()) speech::Announce(title);
            if (!focusText.empty()) speech::Announce(focusText);
            if (!prompts.empty()) speech::Announce(prompts);
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

        // Changing a setting keeps the focus on the same control, so the new value and the
        // description of that value are spoken.
        void PollValueImpl()
        {
            if (gamethread::FrameCount() % 4 != 0) return;
            UObject* interactable = ui::Interactable(watch::CurrentFocused());
            if (!interactable)
            {
                g_valueWidget = nullptr;
                g_lastValue.clear();
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

        void PollArrival(float)
        {
            obj::SafeInvoke([](void*) { PollArrivalImpl(); }, nullptr);
        }

        void PollPrompts(float)
        {
            obj::SafeInvoke([](void*) { PollPromptsImpl(); }, nullptr);
        }

        void PollValue(float)
        {
            obj::SafeInvoke([](void*) { PollValueImpl(); }, nullptr);
        }
    }

    void MenusFeature::Install()
    {
        watch::AddFocusListener([](UObject* screen, UObject* focused) { OnFocusChanged(screen, focused); });
        hooks::OnScript(L"MenuBaseWidget_C", L"Show", [](UObject* self, FFrame&) { BeginArrival(self, L"Show"); });
        hooks::OnScript(L"PopupScreenBaseWidget_C", L"Show", [](UObject* self, FFrame&) { BeginArrival(self, L"Show"); });
        gamethread::AddPoller(L"menus.arrival", &PollArrival);
        gamethread::AddPoller(L"menus.prompts", &PollPrompts);
        gamethread::AddPoller(L"menus.value", &PollValue);
    }

    void MenusFeature::Describe(std::vector<std::wstring>& out)
    {
        UObject* screen = watch::CurrentScreen();
        if (!screen) return;
        const auto title = ui::ScreenTitle(screen);
        if (!title.empty()) out.push_back(title);
        if (UObject* interactable = ui::Interactable(watch::CurrentFocused()))
        {
            auto description = ui::Describe(interactable);
            if (description.tip.empty()) description.tip = ui::ContextLine(screen);
            out.push_back(ui::Speak(description));
        }
        const auto prompts = ui::SpeakPrompts(ui::Prompts(screen));
        if (!prompts.empty()) out.push_back(prompts);
    }

    void MenusFeature::Help(std::vector<std::wstring>& out)
    {
        UObject* screen = watch::CurrentScreen();
        if (!screen) return;
        out.push_back(locale::Mod(L"help.menu", input::KeyForAction(L"UINavigationConfirm"), input::KeyForAction(L"UINavigationCancel")));
        const auto prompts = ui::SpeakPrompts(ui::Prompts(screen));
        if (!prompts.empty()) out.push_back(prompts);
    }
}
