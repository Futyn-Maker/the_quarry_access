#include "features/Prompts.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/ParamReader.hpp"
#include "core/Strings.hpp"
#include "features/Exploration.hpp"
#include "hooks/HookDispatcher.hpp"
#include "input/InputNames.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <map>
#include <string>
#include <vector>

namespace qa::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    namespace
    {
        // What the game set a button prompt up with, kept per prompt widget.
        struct Setup
        {
            std::wstring action;
            int64_t style = 0;
        };
        std::map<UObject*, Setup> g_setups;

        // One prompt slot of the interaction prompt widget.
        struct Slot
        {
            const wchar_t* property;
            std::wstring text; // its readout at the last poll
            int stablePolls = 0;
            bool announced = false;
        };
        UObject* g_widget = nullptr; // the interaction prompt widget while the game shows it
        std::vector<Slot> g_slots;

        const wchar_t* const kSlots[] = {L"ButtonPromptCenter",      L"ButtonPromptLeft", L"ButtonPromptRight",
                                         L"ButtonPromptScreenSpace", L"AxisInputPrompt",  L"VotingResultLabel"};

        void OnSetButtonPrompt(UObject* self, FFrame& stack)
        {
            Setup setup;
            params::String(stack, L"ButtonInputMappingName", setup.action);
            params::Int(stack, L"Style", setup.style);
            log::Info(L"prompts: {} set up with {} style {}", obj::ObjectName(self), setup.action, setup.style);
            // An aiming prompt: the game's own aiming setting decides how much of the work it
            // does for the player, so it is worth knowing which one was in force.
            if (setup.style == 5) log::Info(L"prompts: the aiming setting is {}", ui::GameSettingValue(L"CombatAimSetting"));
            if (str::StartsWith(setup.action, L"ExporationDestination")) NoteDestinationPrompt();
            g_setups[self] = setup;
            if (g_setups.size() > 64)
            {
                std::erase_if(g_setups, [](const auto& entry) { return !obj::IsLive(entry.first); });
            }
        }

        // A button prompt reads as its labels around the key: "Use E to walk", or the label
        // under the glyph followed by the key: "INTERRUPT? A".
        std::wstring ButtonPromptText(UObject* prompt)
        {
            const auto left = ui::PropertyText(prompt, L"LabelLeft");
            const auto right = ui::PropertyText(prompt, L"LabelRight");
            const auto bottom = ui::PropertyText(prompt, L"LabelBottom");
            const auto key = ui::PromptKeyName(prompt, PromptAction(prompt));
            if (!left.empty() || !right.empty()) return str::JoinWords({left, key, right, bottom});
            if (bottom.empty()) return {}; // a key with no words, once its label has gone
            return str::JoinWords({bottom, key});
        }

        // The glyph the game floats over a use location carries no words of its own; the
        // label of the use location the character stands in is said with its key.
        std::wstring GlyphPromptText(UObject* prompt)
        {
            const auto label = CurrentUseLocationLabel();
            if (label.empty()) return {};
            return str::JoinWords({label + L",", ui::PromptKeyName(prompt, PromptAction(prompt))});
        }

        // A prompt for the keys of an axis, the mouse or the stick reads as its labels around
        // the key caps it shows, else around the device.
        std::wstring AxisPromptText(UObject* prompt)
        {
            const auto left = ui::PropertyText(prompt, L"LeftLabel");
            const auto right = ui::PropertyText(prompt, L"RightLabel");
            auto keys = ui::AxisPromptKeys(prompt);
            if (keys.empty()) keys = input::PointerName();
            if (left.empty() && right.empty()) return keys;
            return str::JoinWords({left, keys, right});
        }

        std::wstring SlotText(const Slot& slot)
        {
            UObject* widget = nullptr;
            if (!obj::ReadObject(g_widget, slot.property, widget) || !obj::IsLive(widget) || !obj::IsWidgetShown(widget, true)) return {};
            const std::wstring_view property = slot.property;
            if (property == L"AxisInputPrompt") return AxisPromptText(widget);
            if (property == L"VotingResultLabel") return ui::PropertyText(g_widget, slot.property);
            // The glyph keeps the name it was shown with: the character can stand at the edge
            // of a use location while the game still offers it.
            if (property == L"ButtonPromptScreenSpace")
            {
                const auto text = GlyphPromptText(widget);
                return text.empty() ? slot.text : text;
            }
            return ButtonPromptText(widget);
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (ev.appeared)
            {
                if (!obj::IsA(ev.instance, L"InteractionPromptWidgetSMG026")) return;
                g_widget = ev.instance;
                g_slots.clear();
                for (const wchar_t* property : kSlots)
                    g_slots.push_back(Slot{property});
                log::Info(L"prompts: {} shown", ev.instanceClass);
            }
            else if (ev.instance == g_widget)
            {
                g_widget = nullptr;
                g_slots.clear();
            }
        }

        // A prompt is read once its labels and key have settled, and again whenever the game
        // shows it anew or changes what it says.
        void PollImpl()
        {
            if (gamethread::FrameCount() % 2 != 0 || !obj::IsLive(g_widget)) return;
            for (auto& slot : g_slots)
            {
                const auto text = SlotText(slot);
                if (text != slot.text)
                {
                    slot.text = text;
                    slot.stablePolls = 0;
                    slot.announced = false;
                }
                else if (!text.empty() && !slot.announced && ++slot.stablePolls >= 2)
                {
                    slot.announced = true;
                    log::Verbose(L"prompts: {} \"{}\"", slot.property, text);
                    speech::Announce(text);
                }
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"prompts.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    std::wstring PromptAction(UObject* promptWidget)
    {
        const auto it = g_setups.find(promptWidget);
        return it == g_setups.end() ? std::wstring() : it->second.action;
    }

    void PromptsFeature::Install()
    {
        hooks::OnScript(L"InputActionInteractionPromptWidgetSMG026_C", L"SetButtonPrompt", &OnSetButtonPrompt);
        watch::AddHudListener(&OnHud);
        gamethread::AddPoller(L"prompts", &Poll);
    }

    void PromptsFeature::Describe(std::vector<std::wstring>& out)
    {
        if (!obj::IsLive(g_widget)) return;
        for (const auto& slot : g_slots)
        {
            if (slot.announced && !slot.text.empty()) out.push_back(slot.text);
        }
    }
}
