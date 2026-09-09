#include "features/Choices.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "features/Prompts.hpp"
#include "input/InputNames.hpp"
#include "locale/Locale.hpp"
#include "speech/Sounds.hpp"
#include "speech/Speech.hpp"
#include "ui/Widgets.hpp"
#include "watch/Watchers.hpp"

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

namespace qa::features
{
    using RC::Unreal::UObject;

    namespace
    {
        enum class Kind
        {
            TwoWay,
            Multi,
            Impending,
        };

        // One option of a choice: the widget with its labels, the place it sits in and the
        // prompt widget that shows the key committing it. Which option widgets are in play
        // is taken from the HUD element that owns the choice: the container keeps widgets
        // for every possible option, set up or not.
        struct Option
        {
            const wchar_t* instance;  // property of the HUD element holding the option in play
            const wchar_t* property;  // the option widget, a property of the choice widget
            const wchar_t* direction; // locale key of its place, null for the timeout option
            const wchar_t* prompt;    // property of the prompt widget with its key glyph, null when none
            const wchar_t* action;    // the input action that glyph stands for
            UObject* widget = nullptr;
            bool current = false; // highlighted at the last poll
            bool chosen = false;
        };

        struct Choice
        {
            UObject* hud = nullptr; // the HUD element
            UObject* widget = nullptr;
            Kind kind = Kind::TwoWay;
            std::vector<Option> options;
            std::wstring labels; // the option labels at the last poll, the choice's identity
            int stablePolls = 0;
            bool announced = false;
            bool finished = false;      // decided: nothing more to read until new options come
            std::wstring keys;          // the key hints last read
            std::wstring message;       // the countdown message or the time remaining last read
            std::wstring pendingChosen; // what was chosen, waiting for the held key to be released
        };

        std::vector<Choice> g_choices;

        const Option kTwoWay[] = {
            {L"ChoiceAWidgetInstance", L"ChoiceLeft", L"dir.left", L"ButtonPromptLeft", L"ChoiceCommitLeft"},
            {L"ChoiceBWidgetInstance", L"ChoiceRight", L"dir.right", L"ButtonPromptRight", L"ChoiceCommitRight"},
            {L"ChoiceTimeoutWidgetInstance", L"ChoiceTimeout", nullptr, nullptr, nullptr},
        };
        const Option kMulti[] = {
            {L"ChoiceWidgetInstance", L"ButtonPromptUp", L"dir.up", nullptr, nullptr},
            {L"ChoiceWidgetInstance", L"ButtonPromptRight", L"dir.right", nullptr, nullptr},
            {L"ChoiceWidgetInstance", L"ButtonPromptDown", L"dir.down", nullptr, nullptr},
            {L"ChoiceWidgetInstance", L"ButtonPromptLeft", L"dir.left", nullptr, nullptr},
        };

        bool Shown(UObject* widget)
        {
            return obj::IsLive(widget) && obj::IsWidgetShown(widget, true);
        }

        // The option widgets in play: those the HUD element points at. A pointer that is null
        // (the timeout option of a choice that cannot be left unanswered) leaves the option
        // out, whatever its widget still shows.
        void ResolveOptions(Choice& choice)
        {
            std::vector<UObject*> inPlay;
            for (auto& option : choice.options)
            {
                UObject* widget = nullptr;
                obj::ReadObject(choice.widget, option.property, widget);
                if (!obj::IsLive(widget)) widget = nullptr;
                if (choice.kind == Kind::TwoWay)
                {
                    UObject* instance = nullptr;
                    obj::ReadObject(choice.hud, option.instance, instance);
                    option.widget = (instance && instance == widget) ? widget : nullptr;
                }
                else
                {
                    if (inPlay.empty()) obj::ReadObjectArray(choice.hud, option.instance, inPlay);
                    option.widget = (widget && std::find(inPlay.begin(), inPlay.end(), widget) != inPlay.end()) ? widget : nullptr;
                }
            }
        }

        // The two labels of an option as displayed, with its padlock when it shows one.
        std::wstring OptionText(UObject* option)
        {
            std::vector<std::wstring> parts;
            for (const wchar_t* property : {L"TitleText", L"SubtitleText"})
            {
                const auto text = ui::PropertyText(option, property);
                if (!text.empty()) parts.push_back(text);
            }
            if (parts.empty()) return {};
            if (auto* info = obj::FindProperty(option, L"Info"))
            {
                int64_t padlock = 0;
                if (obj::ReadIntAt(obj::ValuePtr(option, info), obj::StructMember(info, L"PadlockIconType"), padlock) && padlock != 0)
                {
                    parts.push_back(locale::Mod(L"ui.state.locked"));
                }
            }
            return str::Join(parts, L", ");
        }

        // The key shown next to an option, when the game shows it: what its prompt glyph
        // prints, else the key bound to the action the prompt was set up with.
        std::wstring KeyOf(const Choice& choice, const Option& option)
        {
            if (!option.prompt) return {};
            UObject* prompt = nullptr;
            if (!obj::ReadObject(choice.widget, option.prompt, prompt) || !Shown(prompt)) return {};
            const auto action = PromptAction(prompt);
            return ui::PromptKeyName(prompt, action.empty() ? std::wstring(option.action) : action);
        }

        // The option labels alone: what identifies one choice against the next.
        std::wstring Labels(const Choice& choice)
        {
            std::vector<std::wstring> parts;
            for (const auto& option : choice.options)
            {
                if (Shown(option.widget)) parts.push_back(OptionText(option.widget));
            }
            return str::Join(parts, L" | ");
        }

        // The key hints the game shows for the options, as long as it shows them: the key
        // caps of its axis-input prompt (left and right for a two-way choice), else the key
        // glyphs next to the options.
        std::wstring KeyHints(const Choice& choice)
        {
            UObject* axis = nullptr;
            if (obj::ReadObject(choice.widget, L"AxisInputPrompt", axis) && Shown(axis))
            {
                const auto keys = ui::AxisPromptKeys(axis);
                if (!keys.empty()) return keys;
            }
            std::vector<std::wstring> parts;
            for (const auto& option : choice.options)
            {
                if (!option.direction || !Shown(option.widget)) continue;
                const auto key = KeyOf(choice, option);
                if (!key.empty()) parts.push_back(locale::Mod(option.direction) + L": " + key);
            }
            return str::JoinSentences(parts);
        }

        // The whole readout: the heading (with the timer when the choice has one), the
        // question, each option with its place and key, and the timeout option.
        std::wstring ChoiceText(const Choice& choice)
        {
            std::vector<std::wstring> parts;
            bool timed = false;
            obj::ReadBool(choice.widget, L"bHasTimer", timed);
            parts.push_back(locale::Mod(timed ? L"choice.timed" : L"choice.heading"));
            const auto question = ui::PropertyText(choice.widget, L"TitleText");
            if (!question.empty()) parts.push_back(question);
            bool any = false;
            for (const auto& option : choice.options)
            {
                if (!Shown(option.widget)) continue;
                const auto labels = OptionText(option.widget);
                if (labels.empty()) continue;
                any = true;
                std::wstring line = option.direction ? locale::Mod(option.direction) + L": " + labels : labels;
                const auto key = KeyOf(choice, option);
                if (!key.empty()) line += L", " + key;
                parts.push_back(line);
            }
            if (!any) return {};
            return str::JoinSentences(parts);
        }

        bool AnyChosen(const Choice& choice)
        {
            for (const auto& option : choice.options)
            {
                bool chosen = false;
                if (obj::IsLive(option.widget) && obj::ReadBool(option.widget, L"bChosen", chosen) && chosen) return true;
            }
            return false;
        }

        std::wstring WithoutDigits(std::wstring text)
        {
            std::erase_if(text, [](wchar_t c) { return iswdigit(c) != 0; });
            return text;
        }

        // The countdown before a choice is read when it appears and when its words change;
        // the seconds ticking down are not read one by one.
        void PollImpending(Choice& choice)
        {
            const auto text = ui::PropertyText(choice.widget, L"MessageText");
            if (text.empty() || text == choice.message) return;
            const bool onlyDigits = !choice.message.empty() && WithoutDigits(text) == WithoutDigits(choice.message);
            choice.message = text;
            if (!onlyDigits) speech::Announce(text);
        }

        void PollChoice(Choice& choice)
        {
            ResolveOptions(choice);

            // A tone marks the commit itself, since a held key keeps the reader silent; what
            // was chosen is said once the key is released.
            if (!choice.pendingChosen.empty() && !input::InputHeld())
            {
                speech::Announce(choice.pendingChosen);
                choice.pendingChosen.clear();
            }

            const auto labels = Labels(choice);
            if (labels != choice.labels)
            {
                choice.labels = labels;
                choice.stablePolls = 0;
                // A decided choice goes on changing while its options fade; only options with
                // nothing chosen among them are a new choice.
                if (!AnyChosen(choice))
                {
                    choice.announced = false;
                    choice.finished = false;
                    choice.keys.clear();
                    for (auto& option : choice.options)
                        option.current = option.chosen = false;
                }
            }
            if (choice.finished) return;

            if (!choice.announced)
            {
                // A choice already decided when first seen (its widgets come back after a
                // pause) is not read again.
                if (AnyChosen(choice))
                {
                    choice.announced = choice.finished = true;
                    log::Info(L"choices: {} already decided", obj::ClassName(choice.widget));
                    return;
                }
                if (labels.empty() || ++choice.stablePolls < 2) return;
                const auto text = ChoiceText(choice);
                if (text.empty()) return;
                choice.announced = true;
                choice.keys = KeyHints(choice);
                log::Info(L"choices: {} read", obj::ClassName(choice.widget));
                speech::Announce(text);
                return;
            }

            // The key hints the game adds while the player hesitates.
            const auto keys = KeyHints(choice);
            if (keys != choice.keys)
            {
                choice.keys = keys;
                if (!keys.empty()) speech::Announce(keys);
            }

            // The option the player is committing lights up; the chosen one stays lit.
            for (auto& option : choice.options)
            {
                if (!Shown(option.widget)) continue;
                bool current = false;
                bool chosen = false;
                obj::ReadBool(option.widget, L"bCurrentChoice", current);
                obj::ReadBool(option.widget, L"bChosen", chosen);
                if (current && !option.current)
                {
                    const auto text = OptionText(option.widget);
                    if (!text.empty()) speech::Focus(text);
                }
                if (chosen && !option.chosen)
                {
                    sounds::Play(sounds::Cue::Confirm);
                    const auto text = OptionText(option.widget);
                    if (!text.empty()) choice.pendingChosen = locale::Mod(L"choice.chosen", text);
                    choice.finished = true;
                    log::Info(L"choices: chosen \"{}\"", text);
                }
                option.current = current;
                option.chosen = chosen;
            }
            if (choice.finished) return;

            // The seconds left, once the game starts showing them.
            const auto remaining = ui::PropertyText(choice.widget, L"TimeRemaining");
            if (remaining != choice.message)
            {
                choice.message = remaining;
                if (!remaining.empty()) speech::Announce(remaining);
            }
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (!ev.appeared)
            {
                std::erase_if(g_choices, [&](const Choice& c) { return c.widget == ev.instance; });
                return;
            }
            if (obj::IsA(ev.instance, L"TimerBarWidgetSMG026"))
            {
                speech::Announce(locale::Mod(L"choice.timer"));
                return;
            }
            Choice choice;
            choice.hud = ev.hud;
            choice.widget = ev.instance;
            if (obj::IsA(ev.instance, L"GFChoiceContainerWidgetSMG026"))
            {
                choice.kind = Kind::TwoWay;
                choice.options.assign(std::begin(kTwoWay), std::end(kTwoWay));
            }
            else if (obj::IsA(ev.instance, L"MultiChoiceWidgetSMG026"))
            {
                choice.kind = Kind::Multi;
                choice.options.assign(std::begin(kMulti), std::end(kMulti));
            }
            else if (obj::IsA(ev.instance, L"ImpendingChoiceWidgetSMG026"))
            {
                choice.kind = Kind::Impending;
            }
            else
            {
                return;
            }
            if (std::any_of(g_choices.begin(), g_choices.end(), [&](const Choice& c) { return c.widget == ev.instance; })) return;
            log::Info(L"choices: {} appeared", ev.instanceClass);
            g_choices.push_back(std::move(choice));
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 2 != 0) return;
            for (auto it = g_choices.begin(); it != g_choices.end();)
            {
                if (!obj::IsLive(it->widget) || !obj::IsLive(it->hud))
                {
                    it = g_choices.erase(it);
                    continue;
                }
                if (it->kind == Kind::Impending)
                    PollImpending(*it);
                else
                    PollChoice(*it);
                ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvoke([](void*) { PollImpl(); }, nullptr);
        }
    }

    void ChoicesFeature::Install()
    {
        watch::AddHudListener(&OnHud);
        gamethread::AddPoller(L"choices", &Poll);
    }

    void ChoicesFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& choice : g_choices)
        {
            if (!obj::IsLive(choice.widget) || choice.finished) continue;
            const auto text = choice.kind == Kind::Impending ? choice.message : ChoiceText(choice);
            if (!text.empty()) out.push_back(text);
        }
    }

    void ChoicesFeature::Help(std::vector<std::wstring>& out)
    {
        for (const auto& choice : g_choices)
        {
            if (!obj::IsLive(choice.widget) || choice.finished) continue;
            if (choice.kind == Kind::TwoWay)
            {
                const auto left = input::KeyForAction(L"ChoiceCommitLeft");
                const auto right = input::KeyForAction(L"ChoiceCommitRight");
                if (!left.empty() && !right.empty()) out.push_back(locale::Mod(L"help.choice", left, right));
            }
            else if (choice.kind == Kind::Multi)
            {
                std::vector<std::wstring> keys;
                for (const wchar_t* action : {L"TarotSelectionUp", L"TarotSelectionRight", L"TarotSelectionDown", L"TarotSelectionLeft"})
                    keys.push_back(input::KeyForAction(action));
                if (std::none_of(keys.begin(), keys.end(), [](const std::wstring& k) { return k.empty(); }))
                    out.push_back(locale::Mod(L"help.multichoice", keys));
            }
        }
    }
}
