#include "features/Choices.hpp"

#include "core/Flow.hpp"
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
            double commit = 0.0; // how far its own bar had filled at the last poll
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
            bool headingSaid = false; // the heading was said by itself, the options still to come
            double seenAt = -1.0;     // when the options were first seen
            bool finished = false;    // decided: nothing more to read until new options come
            std::wstring keys;        // the key hints last read
            std::wstring message;     // the countdown message or the time remaining last read
            std::wstring lastCurrent; // the option the player was last pushing toward
        };

        std::vector<Choice> g_choices;

        // What was chosen, waiting for the held key to be released. It is kept apart from the
        // choice it came from: a four-way choice takes its widgets off screen the moment it is
        // committed, and the word must still be said.
        std::wstring g_pendingChosen;

        const Option kTwoWay[] = {
            {L"ChoiceAWidgetInstance", L"ChoiceLeft", L"dir.left", L"ButtonPromptLeft", L"ChoiceCommitLeft"},
            {L"ChoiceBWidgetInstance", L"ChoiceRight", L"dir.right", L"ButtonPromptRight", L"ChoiceCommitRight"},
            {L"ChoiceTimeoutWidgetInstance", L"ChoiceTimeout", nullptr, nullptr, nullptr},
        };
        // A four-way choice puts its options in the four corners of the screen, not above,
        // below and beside one another, and the widgets are not named after the corners they
        // sit in: the top row holds ButtonPromptDown and then ButtonPromptUp, the bottom row
        // ButtonPromptLeft and then ButtonPromptRight. The places below are those corners, in
        // reading order, as the widget's own layout sets them out.
        const Option kMulti[] = {
            {L"ChoiceWidgetInstance", L"ButtonPromptDown", L"dir.topleft", nullptr, nullptr},
            {L"ChoiceWidgetInstance", L"ButtonPromptUp", L"dir.topright", nullptr, nullptr},
            {L"ChoiceWidgetInstance", L"ButtonPromptLeft", L"dir.bottomleft", nullptr, nullptr},
            {L"ChoiceWidgetInstance", L"ButtonPromptRight", L"dir.bottomright", nullptr, nullptr},
        };

        // The cage doors of the Hackett basement that stand open, which the room shows plainly
        // once a door has swung. The scene's flow keeps one bool per door and branches on them.
        std::wstring OpenCages()
        {
            struct Door
            {
                const wchar_t* variable;
                const wchar_t* number;
            };
            const Door doors[] = {
                {L"b_jacobdooropen", L"3"},
                {L"b_middledooropen", L"5"},
                {L"b_nickdooropen", L"7"},
            };
            std::vector<std::wstring> open;
            for (const auto& door : doors)
            {
                bool value = false;
                if (flow::Bool(door.variable, value) && value) open.push_back(door.number);
            }
            return open.empty() ? std::wstring() : locale::Mod(L"choice.cages.open", str::Join(open, L", "));
        }

        // A choice whose options stand for something painted on the set rather than named in
        // the interface. The numbers on the cages of the Hackett basement are such a case:
        // the breakers are labelled, the cages they belong to are not, and what tells them
        // apart is painted above the cages, where only the eye reaches it. The option's own
        // locale key says which choice this is; the note tells what is written there and who
        // is behind which number, and what the room has come to since, and no more than that.
        struct Note
        {
            const wchar_t* optionKey;  // what the option's locale key starts with
            const wchar_t* text;       // the mod string saying what the set shows
            std::wstring (*changed)(); // what has changed on the set since, empty when nothing has
        };
        const Note kNotes[] = {
            {L"SMG_CHOICE_ACT_8_HACKETTBASEMENT_BASEMENTENCOUNTER_SWITCH_", L"choice.cages", &OpenCages},
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

        // The option's first label alone, which is what the choice is called. The second label
        // is the line the character will say, and the word that a choice has been taken names
        // only the first, since the line has just been read out with the options.
        std::wstring OptionTitle(UObject* option)
        {
            const auto title = str::Trim(ui::PropertyText(option, L"TitleText"));
            return title.empty() ? OptionText(option) : title;
        }

        // The locale key an option's first label was resolved from: the game's own name for
        // the option, which does not change with the language.
        std::wstring OptionKey(UObject* option)
        {
            auto* info = obj::FindProperty(option, L"Info");
            if (!info || obj::PropertyTypeName(info) != L"StructProperty") return {};
            auto* label = obj::StructMember(info, L"LocaleLabel1");
            if (!label) return {};
            std::wstring key;
            obj::ReadStringAt(obj::ValuePtrAt(obj::ValuePtr(option, info), label), obj::StructMember(label, L"Key"), key);
            return key;
        }

        // What the set says, for a choice the mod holds a note for.
        std::wstring NoteText(const Choice& choice)
        {
            for (const auto& option : choice.options)
            {
                if (!Shown(option.widget)) continue;
                const auto key = OptionKey(option.widget);
                for (const auto& note : kNotes)
                {
                    if (!key.starts_with(note.optionKey)) continue;
                    return str::JoinSentences({locale::Mod(note.text), note.changed ? note.changed() : std::wstring()});
                }
            }
            return {};
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

        // How far an option's own bar has filled toward taking it. A four-way choice is
        // committed by holding, and each option carries the fraction of its hold; it never
        // marks an option chosen the way a two-way choice does, so this is the only word the
        // game gives that such a choice has been made.
        double CommitOf(UObject* option)
        {
            auto* info = obj::FindProperty(option, L"Info");
            if (!info || obj::PropertyTypeName(info) != L"StructProperty") return 0.0;
            double value = 0.0;
            if (!obj::ReadFloatAt(obj::ValuePtr(option, info), obj::StructMember(info, L"CommitFraction"), value)) return 0.0;
            return value;
        }

        // What is done once a choice has been taken, whichever way it was taken: the tone, the
        // word after the key is released, and, for a four-way choice, a fresh reading, since
        // the next of a chain comes back on the same widget with a option gone from it.
        void Committed(Choice& choice, const std::wstring& text)
        {
            sounds::Play(sounds::Cue::Confirm);
            if (!text.empty()) g_pendingChosen = locale::Mod(L"choice.chosen", text);
            if (choice.kind != Kind::Multi)
            {
                choice.finished = true;
                return;
            }
            choice.announced = false;
            choice.headingSaid = false;
            choice.seenAt = -1.0;
            choice.labels.clear();
            choice.lastCurrent.clear();
            for (auto& option : choice.options)
            {
                option.current = option.chosen = false;
                // The bar is left full so that one commit is not read twice, once as it fills
                // and once as the option comes back empty.
                option.commit = 1.0;
            }
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

        // The heading: what kind of choice it is (with the timer when it has one) and its
        // question when the game shows one.
        std::wstring HeadingText(const Choice& choice)
        {
            std::vector<std::wstring> parts;
            bool timed = false;
            obj::ReadBool(choice.widget, L"bHasTimer", timed);
            parts.push_back(locale::Mod(timed ? L"choice.timed" : L"choice.heading"));
            const auto question = ui::PropertyText(choice.widget, L"TitleText");
            if (!question.empty()) parts.push_back(question);
            return str::JoinSentences(parts);
        }

        // The options: each with its place and key, and the timeout option.
        std::wstring OptionsText(const Choice& choice)
        {
            std::vector<std::wstring> parts;
            for (const auto& option : choice.options)
            {
                if (!Shown(option.widget)) continue;
                const auto labels = OptionText(option.widget);
                if (labels.empty()) continue;
                std::wstring line = option.direction ? locale::Mod(option.direction) + L": " + labels : labels;
                const auto key = KeyOf(choice, option);
                if (!key.empty()) line += L", " + key;
                parts.push_back(line);
            }
            return str::JoinSentences(parts);
        }

        // The whole readout: the heading, what the set shows when the choice needs it, then
        // the options.
        std::wstring ChoiceText(const Choice& choice)
        {
            const auto options = OptionsText(choice);
            if (options.empty()) return {};
            return str::JoinSentences({HeadingText(choice), NoteText(choice), options});
        }

        // Whether an option shows every label the game holds for it. The game writes both
        // labels on the option at once but fades the phrase in later than the title: the
        // option's Show animation keeps the phrase's box at zero opacity for its first 0.4 s
        // and brings it in over the next 0.2 s. The labels are taken from the option's own
        // data, so an option without a phrase is complete as soon as its title is in.
        bool Revealed(UObject* option)
        {
            auto* info = obj::FindProperty(option, L"Info");
            if (!info || obj::PropertyTypeName(info) != L"StructProperty") return true;
            void* value = obj::ValuePtr(option, info);
            const auto held = [&](const wchar_t* label, const wchar_t* localeLabel)
            {
                std::wstring text;
                if (obj::ReadStringAt(value, obj::StructMember(info, label), text) && !str::Trim(text).empty()) return true;
                // The key the label is resolved from, should the game fill the label in late.
                auto* locale = obj::StructMember(info, localeLabel);
                return locale && obj::ReadStringAt(obj::ValuePtrAt(value, locale), obj::StructMember(locale, L"Key"), text) && !str::Trim(text).empty();
            };
            if (held(L"Label1", L"LocaleLabel1") && ui::PropertyText(option, L"TitleText").empty()) return false;
            if (held(L"Label2", L"LocaleLabel2") && ui::PropertyText(option, L"SubtitleText").empty()) return false;
            return true;
        }

        bool Revealed(const Choice& choice)
        {
            for (const auto& option : choice.options)
            {
                if (Shown(option.widget) && !Revealed(option.widget)) return false;
            }
            return true;
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

            const auto labels = Labels(choice);
            if (labels != choice.labels)
            {
                choice.labels = labels;
                choice.stablePolls = 0;
                // A decided choice goes on changing while its options fade; only options with
                // nothing chosen among them are a new choice. Options still being revealed
                // change too, and those are the same choice.
                if (!AnyChosen(choice))
                {
                    if (choice.announced)
                    {
                        choice.headingSaid = false;
                        choice.seenAt = -1.0;
                    }
                    choice.announced = false;
                    choice.finished = false;
                    choice.keys.clear();
                    for (auto& option : choice.options)
                    {
                        option.current = option.chosen = false;
                        option.commit = 0.0;
                    }
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
                if (labels.empty()) return;
                const double now = gamethread::NowSeconds();
                if (choice.seenAt < 0.0) choice.seenAt = now;
                // The choice is announced the moment its options are on screen; the options
                // themselves are read once, when the last of their phrases is in, so that
                // they are never heard twice. A choice whose phrases are all in at first
                // sight is read whole.
                const bool revealed = Revealed(choice);
                if (!revealed && !choice.headingSaid)
                {
                    choice.headingSaid = true;
                    log::Info(L"choices: {} announced, its phrases still to come", obj::ClassName(choice.widget));
                    speech::Announce(HeadingText(choice));
                }
                // Should a phrase never come in, the options are read as they are after a
                // moment rather than never.
                if (!revealed && now - choice.seenAt < 2.0)
                {
                    choice.stablePolls = 0;
                    return;
                }
                if (++choice.stablePolls < 2) return;
                std::wstring text;
                if (choice.headingSaid)
                {
                    const auto options = OptionsText(choice);
                    if (!options.empty()) text = str::JoinSentences({NoteText(choice), options});
                }
                else
                {
                    text = ChoiceText(choice);
                }
                if (text.empty()) return;
                choice.announced = true;
                choice.keys = KeyHints(choice);
                log::Info(L"choices: {} read{}", obj::ClassName(choice.widget), revealed ? L"" : L" with a phrase still to come");
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

            // The option lights up under the stick pushed toward it and under the mouse
            // pointer alike, and either way it is the one the player is on. It is taken by the
            // choice marking it chosen or by the click of its button.
            std::wstring taken;
            for (auto& option : choice.options)
            {
                if (!obj::IsLive(option.widget)) continue;
                bool current = false;
                bool pointer = false;
                bool chosen = false;
                bool clicked = false;
                obj::ReadBool(option.widget, L"bCurrentChoice", current);
                obj::ReadBool(option.widget, L"bIsMouseHighlighted", pointer);
                obj::ReadBool(option.widget, L"bChosen", chosen);
                obj::ReadBool(option.widget, L"bIsClicked", clicked);
                const bool lit = current || pointer;
                if (lit && !option.current && Shown(option.widget))
                {
                    const auto text = OptionText(option.widget);
                    if (!text.empty())
                    {
                        choice.lastCurrent = OptionTitle(option.widget);
                        speech::Focus(text);
                    }
                }
                const double commit = CommitOf(option.widget);
                if ((chosen || clicked) && !option.chosen)
                {
                    taken = OptionTitle(option.widget);
                    log::Info(L"choices: chosen \"{}\"{}", taken, clicked ? L" by its button" : L"");
                }
                else if (commit >= 0.995 && option.commit < 0.995)
                {
                    taken = OptionTitle(option.widget);
                    log::Info(L"choices: held to the end on \"{}\"", taken);
                }
                option.current = lit;
                option.chosen = option.chosen || chosen || clicked;
                option.commit = commit;
            }
            if (!taken.empty())
            {
                Committed(choice, taken);
                return;
            }
            if (choice.finished) return;
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
                for (const auto& choice : g_choices)
                {
                    if (choice.widget != ev.instance || choice.kind != Kind::Multi || !g_pendingChosen.empty()) continue;
                    double most = 0.0;
                    for (const auto& option : choice.options)
                        if (option.commit > most) most = option.commit;
                    // The choice can go off screen in the very instant its bar fills, before a
                    // poll sees it full; a bar well on its way and then gone is a commit.
                    if (most < 0.5 || choice.lastCurrent.empty()) continue;
                    log::Info(L"choices: gone at {:.0f}% on \"{}\", taken as chosen", most * 100.0, choice.lastCurrent);
                    sounds::Play(sounds::Cue::Confirm);
                    g_pendingChosen = locale::Mod(L"choice.chosen", choice.lastCurrent);
                }
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

            // A tone marks the commit itself, since a held key keeps the reader silent; what
            // was chosen is said once the key is released.
            if (!g_pendingChosen.empty() && !input::InputHeld())
            {
                speech::Announce(g_pendingChosen);
                g_pendingChosen.clear();
            }
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
            obj::SafeInvokeLogged(L"choices.PollImpl", [](void*) { PollImpl(); }, nullptr);
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
                out.push_back(locale::Mod(L"help.multichoice"));
            }
        }
    }
}
