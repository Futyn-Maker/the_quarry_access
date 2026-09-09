#pragma once
// Descriptions of the game's menu widgets: which user widget is the interactable
// element, its label, type, state, position among its siblings, description
// line, and the prompt bar (label + key) of a screen.

#include <Unreal/UObject.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace qa::ui
{
    using RC::Unreal::UObject;

    enum class Kind
    {
        Button,
        Selector,
        Slider,
        Checkbox,
        Tab,
        Edit,
        Other,
    };

    struct Description
    {
        UObject* widget = nullptr; // the interactable user widget
        Kind kind = Kind::Other;
        std::wstring label;
        std::wstring value; // selectors, sliders, edit fields
        std::wstring state; // "unavailable", "locked"
        int index = 0;      // 1-based among visible interactable siblings, 0 when unknown
        int count = 0;
        std::wstring tip; // description line
    };

    struct Prompt
    {
        std::wstring label;
        std::wstring key;
        std::wstring action;
    };

    // The pause menu's tab bar: the selected tab and its place among the visible tabs.
    struct PauseTabs
    {
        UObject* system = nullptr; // the tab bar widget, null when the pause menu is not up
        UObject* tab = nullptr;    // the selected tab widget
        std::wstring label;
        int index = 0;
        int count = 0;
    };

    // Teaches the reflection layer which widgets keep their text in a single property.
    void Install();

    // True for the user-widget classes that act as focusable controls.
    bool IsInteractable(UObject* widget);

    // The interactable user widget for a focused widget (walks up from inner buttons).
    UObject* Interactable(UObject* focused);

    Kind KindOf(UObject* interactable);
    Description Describe(UObject* interactable);

    // Spoken form of a description according to the configured verbosity.
    std::wstring Speak(const Description& description);

    // The widgets that together make up what the player sees as one screen. Usually the
    // screen itself; in the pause menu the tab bar, the content of the selected tab and the
    // character carousel are separate widgets and are read together.
    std::vector<UObject*> ScreenRoots(UObject* screen);

    // Screen title (menu title widget text without the trailing colon).
    std::wstring ScreenTitle(UObject* screen);

    // Static text a screen shows besides its title and controls: the message of a popup,
    // the objective line of a pause tab.
    std::wstring ScreenBody(UObject* screen);

    // Everything readable on a screen, for reading it out on request.
    std::wstring ScreenText(UObject* screen);

    // Visible prompt widgets of a screen (bottom bar and additional prompts).
    std::vector<Prompt> Prompts(UObject* screen);
    std::wstring SpeakPrompts(const std::vector<Prompt>& prompts);

    // The bottom-left description line of a screen, when present.
    std::wstring ContextLine(UObject* screen);

    // Text of a prompt widget with "$(prompt)" replaced by its key name.
    std::wstring PromptText(UObject* promptWidget);

    // Name of the input action stored in an action-mapping property ("UINavigationCancel").
    std::wstring ActionName(UObject* widget, std::wstring_view property);

    // The tab bar of the pause menu, when it is on display.
    PauseTabs ActivePauseTabs();
    // The same in two steps: the bar (a scan of all objects) and its state (cheap).
    UObject* PauseTabSystem();
    PauseTabs PauseTabsOf(UObject* system);

    // What a HUD element says: a caption, a notification with its hot-link prompt, a title.
    std::wstring HudText(UObject* instance);

    // The character shown by a character carousel: name, then what its card says.
    std::wstring CarouselText(UObject* carousel);
}
