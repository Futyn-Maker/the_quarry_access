#include "features/DontBreathe.hpp"

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
#include <string>
#include <vector>

namespace qa::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        struct Breath
        {
            UObject* widget = nullptr;
            std::wstring action; // the input action that holds the breath
            std::wstring prompt; // the prompt at the last poll
            int stablePolls = 0;
            std::wstring announced; // the prompt last read
            int64_t state = -1;     // 0 drawing breath, 1 holding, 2 success, 3 failure
            bool finished = false;
            double lastTickAt = -10.0;
            // When a breath would be heard. The encounter is played by holding through those
            // stretches and breathing in the gaps between them.
            std::vector<std::pair<double, double>> zones;
            bool zonesRead = false;
            bool inZone = false;
            bool warned = false;    // the coming danger has been called
            bool lowCalled = false; // the breath running out has been called
            bool demo = false;      // a tutorial playing itself out
        };
        std::vector<Breath> g_breaths;

        std::wstring Action(UObject* widget)
        {
            std::wstring action;
            obj::CallReturn(widget, L"GetDontBreatheInfo",
                            [&](void* params, FProperty* returnValue)
                            {
                                void* value = obj::ValuePtrAt(params, returnValue);
                                if (!value) return;
                                if (auto* input = obj::StructMember(returnValue, L"InputAction"))
                                    obj::ReadStringAt(obj::ValuePtrAt(value, input), obj::StructMember(input, L"Value"), action);
                            });
            return action.empty() ? std::wstring(L"HoldBreath") : action;
        }

        // The prompt as displayed: its words around the key glyph.
        std::wstring Prompt(const Breath& breath)
        {
            const auto left = ui::PropertyText(breath.widget, L"PromptActionText");
            const auto right = ui::PropertyText(breath.widget, L"PromptLabel");
            if (left.empty() && right.empty()) return {};
            std::wstring key;
            UObject* glyph = nullptr;
            if (obj::ReadObject(breath.widget, L"GlyphWidget", glyph) && obj::IsLive(glyph)) key = ui::GlyphKeyName(glyph, breath.action);
            if (key.empty()) key = input::KeyForAction(breath.action);
            return str::JoinWords({left, key, right});
        }

        // A tutorial plays the whole thing out by itself: the bars empty and the outcome
        // arrives on a fixed clock whatever the player does.
        bool InTutorial()
        {
            for (const wchar_t* cls : {L"DontBreatheTutorialOverlay_C", L"TutorialVideo_C"})
            {
                for (auto* widget : obj::FindAllLive(cls))
                {
                    if (obj::IsWidgetShown(widget, true)) return true;
                }
            }
            return false;
        }

        // The stretches of the encounter in which a breath would give the character away.
        void ReadZones(Breath& breath)
        {
            if (breath.zonesRead) return;
            breath.zonesRead = true;
            obj::CallReturn(breath.widget, L"GetDontBreatheInfo",
                            [&](void* params, FProperty* returnValue)
                            {
                                void* value = obj::ValuePtrAt(params, returnValue);
                                if (!value) return;
                                obj::ForEachArrayElement(value, obj::StructMember(returnValue, L"DangerZones"),
                                                         [&](void* element, FProperty* inner)
                                                         {
                                                             void* zone = obj::ValuePtrAt(element, inner);
                                                             if (!zone) return;
                                                             double start = 0.0, end = 0.0;
                                                             obj::ReadFloatAt(zone, obj::StructMember(inner, L"StartTime"), start);
                                                             obj::ReadFloatAt(zone, obj::StructMember(inner, L"EndTime"), end);
                                                             breath.zones.push_back({start, end});
                                                         });
                            });
            log::Info(L"breathe: {} stretch(es) of danger", breath.zones.size());
            for (const auto& [start, end] : breath.zones)
                log::Info(L"  danger from {:.1f} s to {:.1f} s", start, end);
        }

        void Add(UObject* widget)
        {
            if (!obj::IsLive(widget)) return;
            if (std::any_of(g_breaths.begin(), g_breaths.end(), [&](const Breath& b) { return b.widget == widget; })) return;
            Breath breath;
            breath.widget = widget;
            breath.action = Action(widget);
            breath.demo = InTutorial();
            log::Info(L"breathe: {} {} appeared, action \"{}\"{}", obj::ClassName(widget), obj::ObjectName(widget), breath.action,
                      breath.demo ? L", a tutorial playing itself out" : L"");
            // The moment to hold is marked by the tone of the danger, ahead of the game's own
            // prompt, which is read once it has settled; a demonstration asks nothing.
            if (breath.demo)
                speech::Announce(locale::Mod(L"breathe.demo"));
            else
                sounds::Play(sounds::Cue::Down);
            g_breaths.push_back(std::move(breath));
        }

        void OnConstruct(UObject* self, FFrame&)
        {
            Add(self);
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (ev.appeared && obj::IsA(ev.instance, L"DontBreatheWidgetSMG026")) Add(ev.instance);
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 2 != 0) return;
            const double now = gamethread::NowSeconds();
            for (auto it = g_breaths.begin(); it != g_breaths.end();)
            {
                if (!obj::IsLive(it->widget))
                {
                    it = g_breaths.erase(it);
                    continue;
                }
                Breath& breath = *it;
                // The prompt is read once it has settled, and again when the game changes it.
                const auto prompt = breath.finished ? std::wstring() : Prompt(breath);
                if (prompt != breath.prompt)
                {
                    breath.prompt = prompt;
                    breath.stablePolls = 0;
                }
                else if (!prompt.empty() && prompt != breath.announced && ++breath.stablePolls >= 2)
                {
                    breath.announced = prompt;
                    log::Info(L"breathe: prompt \"{}\"", prompt);
                    speech::Announce(prompt);
                }

                UObject* data = nullptr;
                if (!breath.finished && obj::ReadObject(breath.widget, L"InfoDataInstance", data) && obj::IsLive(data))
                {
                    int64_t state = 0;
                    double scale = 0.0;
                    double elapsed = 0.0;
                    obj::ReadInt(data, L"DontBreatheState", state);
                    obj::ReadFloat(data, L"HoldingBreathTimeScale", scale);
                    obj::ReadFloat(data, L"TimeElapsed", elapsed);
                    if (state != breath.state)
                    {
                        log::Info(L"breathe: state {} at {:.2f} s, bar {:.2f}", state, elapsed, 1.0 - scale);
                        breath.state = state;
                    }
                    ReadZones(breath);
                    // Where the danger stands in relation to the moment. The game shows this
                    // in the picture and the sound it plays as the creature draws near.
                    if (state < 2 && !breath.zones.empty())
                    {
                        bool here = false;
                        bool coming = false;
                        for (const auto& [start, end] : breath.zones)
                        {
                            if (elapsed >= start && elapsed <= end)
                                here = true;
                            else if (elapsed < start && start - elapsed <= 1.5)
                                coming = true;
                        }
                        if (here != breath.inZone)
                        {
                            breath.inZone = here;
                            breath.warned = false;
                            log::Info(L"breathe: the danger {} at {:.1f} s", here ? L"is here" : L"has passed", elapsed);
                            if (!here)
                            {
                                sounds::Play(sounds::Cue::Up);
                                speech::Announce(locale::Mod(L"breathe.safe"));
                            }
                        }
                        else if (coming && !here && !breath.warned)
                        {
                            breath.warned = true;
                            log::Info(L"breathe: the danger is coming at {:.1f} s", elapsed);
                            sounds::Play(sounds::Cue::Down);
                            speech::Announce(locale::Mod(L"breathe.danger"));
                        }
                    }
                    if (state == 1)
                    {
                        // The breath bars shrink while the breath is held: a blip follows them.
                        if (now - breath.lastTickAt >= 0.5)
                        {
                            sounds::Tick(std::clamp(1.0 - scale, 0.0, 1.0));
                            breath.lastTickAt = now;
                            log::Info(L"breathe: holding at {:.1f} s, bar {:.2f}", elapsed, 1.0 - scale);
                        }
                        // The bars are nearly empty: the character is about to gasp.
                        // A tutorial gets no such warning: nothing the player does changes it.
                        // Running out of air and the creature moving off are two different
                        // things, and they can want opposite things of the player, so the
                        // warning says which one he is in.
                        if (!breath.demo && !breath.lowCalled && 1.0 - scale < 0.25)
                        {
                            breath.lowCalled = true;
                            log::Info(L"breathe: the breath is nearly out at {:.1f} s, danger {}", elapsed, breath.inZone ? L"still here" : L"passed");
                            speech::Announce(locale::Mod(breath.inZone ? L"breathe.low.danger" : L"breathe.low"));
                        }
                    }
                    else if (state == 0)
                    {
                        breath.lowCalled = false; // a fresh breath drawn
                    }
                    else if (state >= 2)
                    {
                        breath.finished = true;
                        const bool success = state == 2;
                        log::Info(L"breathe: {} {}", obj::ObjectName(breath.widget), success ? L"succeeded" : L"failed");
                        sounds::Play(success ? sounds::Cue::Confirm : sounds::Cue::Fail);
                        if (cfg::Detailed()) speech::Announce(locale::Mod(success ? L"result.success" : L"result.failure"));
                    }
                }
                ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"dontbreathe.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void DontBreatheFeature::Install()
    {
        hooks::OnScript(L"DontBreatheWidgetSMG026_C", L"Construct", &OnConstruct);
        watch::AddHudListener(&OnHud);
        gamethread::AddPoller(L"breathe", &Poll);
    }

    void DontBreatheFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& breath : g_breaths)
        {
            if (obj::IsLive(breath.widget) && !breath.finished && !breath.announced.empty()) out.push_back(breath.announced);
        }
    }

    void DontBreatheFeature::Help(std::vector<std::wstring>& out)
    {
        for (const auto& breath : g_breaths)
        {
            if (!obj::IsLive(breath.widget) || breath.finished) continue;
            const auto key = input::KeyForAction(breath.action);
            if (!key.empty()) out.push_back(locale::Mod(L"help.breathe", key));
            return;
        }
    }
}
