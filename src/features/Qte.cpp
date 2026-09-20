#include "features/Qte.hpp"

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
#include "watch/Watchers.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace qa::features
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;

    namespace
    {
        enum class Outcome
        {
            Pending,
            Success,
            Failure,
        };

        // One quick-time event widget. The game may show the same event through more than
        // one widget, so what is said is keyed by the event, not the widget.
        struct Qte
        {
            UObject* widget = nullptr;
            int polls = 0;
            bool announced = false;
            std::wstring text; // what was said for it
            sounds::Cue cue = sounds::Cue::Up;
            bool speaker = false;    // the widget that reads the event, when several show it
            double spokenAt = -10.0; // when it was last said
            double appearedAt = 0.0;
            bool opened = false; // the game has begun taking the press
            Outcome outcome = Outcome::Pending;
            bool finished = false;
        };
        std::vector<Qte> g_qtes;

        struct Event
        {
            std::wstring identity; // angle and start time
            double at = -10.0;
        };
        Event g_lastRead;   // the event last read out
        Event g_lastResult; // the event whose result was last played

        struct Info
        {
            bool valid = false;
            int64_t angle = 0;
            double timer = 0.0;
            double elapsed = 0.0;
            double start = 0.0;
            bool timedOut = false;
        };

        Info ReadInfo(UObject* widget)
        {
            Info info;
            obj::CallReturn(widget, L"GetDirectionQTESMG026Info",
                            [&](void* params, FProperty* returnValue)
                            {
                                void* value = obj::ValuePtrAt(params, returnValue);
                                if (!value) return;
                                info.valid = obj::ReadIntAt(value, obj::StructMember(returnValue, L"Angle"), info.angle);
                                obj::ReadFloatAt(value, obj::StructMember(returnValue, L"Timer"), info.timer);
                                obj::ReadFloatAt(value, obj::StructMember(returnValue, L"TimeElapsed"), info.elapsed);
                                obj::ReadFloatAt(value, obj::StructMember(returnValue, L"StartTime"), info.start);
                                obj::ReadBoolAt(value, obj::StructMember(returnValue, L"bTimedOut"), info.timedOut);
                            });
            return info;
        }

        std::wstring Identity(const Info& info)
        {
            return std::to_wstring(info.angle) + L"|" + std::to_wstring(info.start);
        }

        // The input action the game accepts for the event, as it names it ("DirectionQTEUp").
        std::wstring SuccessAction(UObject* widget)
        {
            std::wstring action;
            obj::CallReturn(widget, L"GetActionMappingSuccess",
                            [&](void* params, FProperty* returnValue)
                            {
                                void* value = obj::ValuePtrAt(params, returnValue);
                                if (value) obj::ReadStringAt(value, obj::StructMember(returnValue, L"Value"), action);
                            });
            return action;
        }

        // The input action of a direction, as the game names them.
        const wchar_t* ActionOfDirection(std::wstring_view direction)
        {
            if (direction == L"dir.down") return L"DirectionQTEDown";
            if (direction == L"dir.left") return L"DirectionQTELeft";
            if (direction == L"dir.right") return L"DirectionQTERight";
            return L"DirectionQTEUp";
        }

        const wchar_t* DirectionOfAction(const std::wstring& action)
        {
            if (str::EndsWith(action, L"Up")) return L"dir.up";
            if (str::EndsWith(action, L"Down")) return L"dir.down";
            if (str::EndsWith(action, L"Left")) return L"dir.left";
            if (str::EndsWith(action, L"Right")) return L"dir.right";
            return nullptr;
        }

        // The arrow points up at angle 0 and turns clockwise with the angle.
        const wchar_t* DirectionOfAngle(int64_t angle)
        {
            const int64_t a = ((angle % 360) + 360) % 360;
            if (a < 45 || a >= 315) return L"dir.up";
            if (a < 135) return L"dir.right";
            if (a < 225) return L"dir.down";
            return L"dir.left";
        }

        sounds::Cue CueOf(std::wstring_view direction)
        {
            if (direction == L"dir.down") return sounds::Cue::Down;
            if (direction == L"dir.left") return sounds::Cue::Left;
            if (direction == L"dir.right") return sounds::Cue::Right;
            return sounds::Cue::Up;
        }

        void Speak(Qte& qte, double now);

        // True while the named animation of the widget plays.
        bool AnimationPlaying(UObject* widget, const wchar_t* animation)
        {
            UObject* anim = nullptr;
            if (!obj::ReadObject(widget, animation, anim) || !obj::IsLive(anim)) return false;
            auto* fn = obj::FindFunction(widget, L"IsAnimationPlaying");
            if (!fn) return false;
            bool playing = false;
            obj::Call(
                widget, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && obj::PropertyTypeName(prop) == L"ObjectProperty") *static_cast<UObject**>(obj::ValuePtrAt(params, prop)) = anim;
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadBoolAt(params, prop, playing);
                    }
                });
            return playing;
        }

        // The direction is said and played as soon as the widget reports its event, and
        // again every second until the event is resolved, since the arrow stays on screen the
        // whole time; the key is named when the game shows a key cap, which it does for the
        // keyboard only.
        void Read(Qte& qte)
        {
            const Info info = ReadInfo(qte.widget);
            if (!info.valid) return;
            const auto action = SuccessAction(qte.widget);
            const wchar_t* direction = DirectionOfAction(action);
            if (!direction) direction = DirectionOfAngle(info.angle);
            std::wstring key;
            if (input::CurrentScheme() != input::Scheme::Gamepad) key = input::KeyForAction(action.empty() ? ActionOfDirection(direction) : action);
            std::wstring text = locale::Mod(direction);
            if (!key.empty()) text += L", " + key;
            text += L".";
            bool primary = false;
            obj::ReadBool(qte.widget, L"bIsPrimaryDisplay", primary);
            log::Info(L"qte: {} angle {} action \"{}\" -> {} key \"{}\" timer {:.2f} start {:.2f} primary {} shown {}", obj::ObjectName(qte.widget), info.angle,
                      action, direction, key, info.timer, info.start, primary, obj::IsWidgetShown(qte.widget, true));
            qte.announced = true;
            qte.text = text;
            qte.cue = CueOf(direction);
            const auto identity = Identity(info);
            const double now = gamethread::NowSeconds();
            if (identity == g_lastRead.identity && now - g_lastRead.at < 3.0)
            {
                log::Info(L"qte: another widget of the same event, not read again");
                return;
            }
            g_lastRead = Event{identity, now};
            qte.speaker = true;
            Speak(qte, now);
        }

        void Speak(Qte& qte, double now)
        {
            qte.spokenAt = now;
            sounds::Play(qte.cue);
            speech::Announce(qte.text);
        }

        void Finish(Qte& qte)
        {
            qte.finished = true;
            const Info info = ReadInfo(qte.widget);
            const bool success = qte.outcome == Outcome::Success;
            log::Info(L"qte: {} {} after {:.2f} of {:.2f} s", obj::ObjectName(qte.widget), success ? L"hit" : (info.timedOut ? L"timed out" : L"missed"),
                      info.elapsed, info.timer);
            const auto identity = Identity(info);
            const double now = gamethread::NowSeconds();
            if (identity == g_lastResult.identity && now - g_lastResult.at < 3.0) return;
            g_lastResult = Event{identity, now};
            sounds::Play(success ? sounds::Cue::Confirm : sounds::Cue::Fail);
            if (cfg::Detailed()) speech::Announce(locale::Mod(success ? L"result.success" : L"result.failure"));
        }

        void Add(UObject* widget)
        {
            if (!obj::IsLive(widget)) return;
            if (std::any_of(g_qtes.begin(), g_qtes.end(), [&](const Qte& q) { return q.widget == widget; })) return;
            log::Info(L"qte: {} {} appeared", obj::ClassName(widget), obj::ObjectName(widget));
            Qte qte{widget};
            qte.appearedAt = gamethread::NowSeconds();
            g_qtes.push_back(qte);
        }

        void Mark(UObject* widget, Outcome outcome)
        {
            for (auto& qte : g_qtes)
            {
                if (qte.widget == widget && qte.outcome == Outcome::Pending) qte.outcome = outcome;
            }
        }

        void OnConstruct(UObject* self, FFrame&)
        {
            Add(self);
        }

        void OnSuccessAnimation(UObject* self, FFrame&)
        {
            Mark(self, Outcome::Success);
        }

        void OnFailureAnimation(UObject* self, FFrame&)
        {
            Mark(self, Outcome::Failure);
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (ev.appeared && obj::IsA(ev.instance, L"GFDirectionQTEWidgetSMG026")) Add(ev.instance);
        }

        void PollImpl()
        {
            for (auto it = g_qtes.begin(); it != g_qtes.end();)
            {
                if (!obj::IsLive(it->widget))
                {
                    it = g_qtes.erase(it);
                    continue;
                }
                Qte& qte = *it;
                // The event is read as soon as the widget reports it; a widget that never does
                // is left alone.
                if (!qte.announced && ++qte.polls <= 60) Read(qte);
                if (!qte.finished)
                {
                    const double now = gamethread::NowSeconds();
                    // The game takes the press only once the marker has finished arriving,
                    // which its clock marks by starting to count; a press before that is
                    // ignored, for everyone. The moment it starts counting is the moment the
                    // marker settles on screen, and the tone marks it.
                    if (qte.announced && !qte.opened)
                    {
                        const Info info = ReadInfo(qte.widget);
                        if (info.valid && info.elapsed > 0.0)
                        {
                            qte.opened = true;
                            log::Info(L"qte: {} takes the press from {:.2f} s after it appeared", obj::ObjectName(qte.widget), now - qte.appearedAt);
                            if (qte.speaker)
                            {
                                sounds::Play(qte.cue);
                                qte.spokenAt = now;
                            }
                        }
                    }
                    if (qte.speaker && now - qte.spokenAt >= 1.0) Speak(qte, now);
                    // The result animations are the game's own verdict; the hooks on their
                    // events come first, the animations themselves are the fallback.
                    if (qte.outcome == Outcome::Pending)
                    {
                        if (AnimationPlaying(qte.widget, L"SuccessAnim"))
                            qte.outcome = Outcome::Success;
                        else if (AnimationPlaying(qte.widget, L"FailureAnim"))
                            qte.outcome = Outcome::Failure;
                    }
                    if (qte.outcome != Outcome::Pending) Finish(qte);
                }
                ++it;
            }
        }

        void Poll(float)
        {
            obj::SafeInvokeLogged(L"qte.PollImpl", [](void*) { PollImpl(); }, nullptr);
        }
    }

    void QteFeature::Install()
    {
        hooks::OnScript(L"DirectionQTEViewportWidgetSMG026_C", L"Construct", &OnConstruct);
        hooks::OnScript(L"DirectionQTEViewportWidgetSMG026_C", L"WidgetAnimationEvt_SuccessAnim_K2Node_WidgetAnimationEvent_8", &OnSuccessAnimation);
        hooks::OnScript(L"DirectionQTEViewportWidgetSMG026_C", L"WidgetAnimationEvt_FailureAnim_K2Node_WidgetAnimationEvent_9", &OnFailureAnimation);
        watch::AddHudListener(&OnHud);
        gamethread::AddPoller(L"qte", &Poll);
    }

    void QteFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& qte : g_qtes)
        {
            if (obj::IsLive(qte.widget) && qte.announced && !qte.finished && !qte.text.empty()) out.push_back(qte.text);
        }
    }

    void QteFeature::Help(std::vector<std::wstring>& out)
    {
        if (std::none_of(g_qtes.begin(), g_qtes.end(), [](const Qte& q) { return obj::IsLive(q.widget) && !q.finished; })) return;
        if (input::CurrentScheme() == input::Scheme::Gamepad)
        {
            out.push_back(locale::Mod(L"help.qte.pad"));
            return;
        }
        std::vector<std::wstring> keys;
        for (const wchar_t* action : {L"DirectionQTEUp", L"DirectionQTELeft", L"DirectionQTEDown", L"DirectionQTERight"})
            keys.push_back(input::KeyForAction(action));
        if (std::none_of(keys.begin(), keys.end(), [](const std::wstring& k) { return k.empty(); })) out.push_back(locale::Mod(L"help.qte", keys));
    }
}
