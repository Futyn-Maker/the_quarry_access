#include "features/DontBreathe.hpp"

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

        void Add(UObject* widget)
        {
            if (!obj::IsLive(widget)) return;
            if (std::any_of(g_breaths.begin(), g_breaths.end(), [&](const Breath& b) { return b.widget == widget; })) return;
            Breath breath;
            breath.widget = widget;
            breath.action = Action(widget);
            log::Info(L"breathe: {} {} appeared, action \"{}\"", obj::ClassName(widget), obj::ObjectName(widget), breath.action);
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
                    if (state == 1)
                    {
                        // The breath bars shrink while the breath is held: a blip follows them.
                        if (now - breath.lastTickAt >= 0.5)
                        {
                            sounds::Tick(std::clamp(1.0 - scale, 0.0, 1.0));
                            breath.lastTickAt = now;
                            log::Info(L"breathe: holding at {:.1f} s, bar {:.2f}", elapsed, 1.0 - scale);
                        }
                    }
                    else if (state >= 2)
                    {
                        breath.finished = true;
                        const bool success = state == 2;
                        log::Info(L"breathe: {} {}", obj::ObjectName(breath.widget), success ? L"succeeded" : L"failed");
                        sounds::Play(success ? sounds::Cue::Confirm : sounds::Cue::Fail);
                        speech::Announce(locale::Mod(success ? L"result.success" : L"result.failure"));
                    }
                }
                ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvoke([](void*) { PollImpl(); }, nullptr);
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
