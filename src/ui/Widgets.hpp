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

    // What a text field holds. A password field draws dots instead of letters, so its
    // text is not given out; only how much of it there is.
    struct EditState
    {
        std::wstring text;
        size_t length = 0;
        bool hidden = false;
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

    // What a text field holds right now, read from the widget the game draws.
    EditState EditField(UObject* field);

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

    // True when the screen holds a text field, which the game walks with the tab key.
    bool HasTextField(UObject* screen);

    // Visible prompt widgets of a screen (bottom bar and additional prompts).
    std::vector<Prompt> Prompts(UObject* screen);
    std::wstring SpeakPrompts(const std::vector<Prompt>& prompts);

    // The bottom-left description line of a screen, when present.
    std::wstring ContextLine(UObject* screen);

    // Text of a prompt widget with "$(prompt)" replaced by its key name.
    std::wstring PromptText(UObject* promptWidget);

    // Displayed text of the widget held in a property of `widget` (a text block, a text
    // leaf or a user widget), empty unless it is shown and not faded out.
    std::wstring PropertyText(UObject* widget, std::wstring_view property);

    // Spoken key of a prompt widget: the key its glyph prints, else the key bound to the action.
    std::wstring PromptKeyName(UObject* promptWidget, std::wstring_view action);

    // The keys an axis-input prompt shows as key caps, in reading order (up, left, down,
    // right), then its device glyph when it prints one. Empty when it shows neither.
    std::wstring AxisPromptKeys(UObject* axisPrompt);

    // Spoken key of a glyph widget: the key it prints, else the key bound to the action.
    std::wstring GlyphKeyName(UObject* glyphWidget, std::wstring_view action);

    // The current value of one of the game's list settings (`/Game/UI/GameSettings/<name>`)
    // as the index in its enum; -1 when the setting cannot be read.
    int64_t GameSettingValue(std::wstring_view assetName);

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
