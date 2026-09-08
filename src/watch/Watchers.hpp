#pragma once
// Per-frame pollers (game thread):
//  - HudInstanceWatcher: watches the *WidgetInstance pointers of every ActionHUD
//    on the player controller's HUD components; emits appeared/disappeared events.
//  - TextWatcher: watches registered text widgets for text changes.
//  - FocusWatcher: tracks the current screen widget and its LastFocusedWidget.

#include <Unreal/UObject.hpp>

#include <functional>
#include <string>
#include <vector>

namespace qa::watch
{
    using RC::Unreal::UObject;

    struct HudEvent
    {
        UObject* hud = nullptr;      // the UActionHUD* object
        std::wstring hudClass;       // e.g. "ActionHUDChoiceSMG026"
        std::wstring property;       // e.g. "ChoiceContainerWidgetInstance"
        UObject* instance = nullptr; // the widget (valid on appear; may be dead on disappear)
        std::wstring instanceClass;
        bool appeared = false;
    };
    using HudListener = std::function<void(const HudEvent&)>;
    void AddHudListener(HudListener listener);
    // Currently live HUD widget instances: (property name, instance).
    std::vector<std::pair<std::wstring, UObject*>> LiveHudInstances();

    using TextListener = std::function<void(UObject* textWidget, const std::wstring& text)>;
    int WatchText(UObject* textWidget, TextListener listener, bool fireInitial = false);
    void UnwatchText(int id);

    using FocusListener = std::function<void(UObject* screen, UObject* focused)>;
    void AddFocusListener(FocusListener listener);
    UObject* CurrentScreen();
    UObject* CurrentFocused();

    void Install();
    void Reset(); // after map load
}
