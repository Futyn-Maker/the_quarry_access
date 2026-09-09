#pragma once
// Descriptions of the game's menu widgets: which user widget is the interactable
// element, its label, type, state, position among its siblings, description
// line, and the prompt bar (label + key) of a screen.

#include <Unreal/UObject.hpp>

#include <string>
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
        std::wstring state; // "unavailable", "locked", "new"
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

    // True for the user-widget classes that act as focusable controls.
    bool IsInteractable(UObject* widget);

    // The interactable user widget for a focused widget (walks up from inner buttons).
    UObject* Interactable(UObject* focused);

    Description Describe(UObject* interactable);

    // Spoken form of a description according to the configured verbosity.
    std::wstring Speak(const Description& description);

    // Screen title (menu title widget text without the trailing colon), or a mod string for known screens.
    std::wstring ScreenTitle(UObject* screen);

    // Visible prompt widgets of a screen (bottom bar and additional prompts).
    std::vector<Prompt> Prompts(UObject* screen);
    std::wstring SpeakPrompts(const std::vector<Prompt>& prompts);

    // The bottom-left description line of a screen, when present.
    std::wstring ContextLine(UObject* screen);

    // Text of a prompt widget with "$(prompt)" replaced by its key name.
    std::wstring PromptText(UObject* promptWidget);
}
