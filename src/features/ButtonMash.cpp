#include "features/ButtonMash.hpp"

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
        struct Mash
        {
            UObject* widget = nullptr;
            int polls = 0;
            bool announced = false;
            std::wstring text; // what was said for it
            bool finished = false;
            double lastFraction = 0.0; // the ring at the last blip
            double lastTickAt = -10.0;
        };
        std::vector<Mash> g_mashes;

        struct Info
        {
            std::wstring action; // the input action that counts
            int64_t type = 0;    // 0 mash to a score, 1 keep a rate above a threshold
            double timeLimit = 0.0;
        };

        Info ReadInfo(UObject* widget)
        {
            Info info;
            obj::CallReturn(widget, L"GetButtonMashInfo",
                            [&](void* params, FProperty* returnValue)
                            {
                                void* value = obj::ValuePtrAt(params, returnValue);
                                if (!value) return;
                                if (auto* action = obj::StructMember(returnValue, L"SuccessAction"))
                                    obj::ReadStringAt(obj::ValuePtrAt(value, action), obj::StructMember(action, L"Value"), info.action);
                                obj::ReadIntAt(value, obj::StructMember(returnValue, L"ButtonMashType"), info.type);
                                obj::ReadFloatAt(value, obj::StructMember(returnValue, L"TimeLimit"), info.timeLimit);
                            });
            return info;
        }

        // The key as the glyph prints it, else the key bound to the action.
        std::wstring Key(UObject* widget, const std::wstring& action)
        {
            std::wstring key;
            UObject* glyph = nullptr;
            if (obj::ReadObject(widget, L"Glyph", glyph) && obj::IsLive(glyph)) key = ui::GlyphKeyName(glyph, action);
            if (key.empty()) key = input::KeyForAction(action.empty() ? L"ButtonMash" : action);
            return key;
        }

        // The button, said the way the game's button mash setting wants it pressed.
        std::wstring PromptText(const std::wstring& key, int64_t mode)
        {
            switch (mode)
            {
            case 1: return locale::Mod(L"mash.hold", key);
            case 2: return locale::Mod(L"mash.tap", key);
            case 3: return locale::Mod(L"mash.auto", key);
            default: return locale::Mod(L"mash.mash", key);
            }
        }

        void Read(Mash& mash)
        {
            const Info info = ReadInfo(mash.widget);
            const auto key = Key(mash.widget, info.action);
            if (key.empty()) return;
            const int64_t mode = ui::GameSettingValue(L"ButtonMashModeSetting");
            mash.announced = true;
            mash.text = PromptText(key, mode);
            log::Info(L"mash: {} action \"{}\" type {} limit {:.2f} s key \"{}\" mode {}", obj::ObjectName(mash.widget), info.action, info.type, info.timeLimit,
                      key, mode);
            speech::Announce(mash.text);
        }

        void Add(UObject* widget)
        {
            if (!obj::IsLive(widget)) return;
            if (std::any_of(g_mashes.begin(), g_mashes.end(), [&](const Mash& m) { return m.widget == widget; })) return;
            log::Info(L"mash: {} {} appeared", obj::ClassName(widget), obj::ObjectName(widget));
            g_mashes.push_back(Mash{widget});
        }

        void OnConstruct(UObject* self, FFrame&)
        {
            Add(self);
        }

        void OnHud(const watch::HudEvent& ev)
        {
            if (ev.appeared && obj::IsA(ev.instance, L"GFButtonMashWidgetSMG026")) Add(ev.instance);
        }

        void PollImpl()
        {
            if (gamethread::FrameCount() % 2 != 0) return;
            const double now = gamethread::NowSeconds();
            for (auto it = g_mashes.begin(); it != g_mashes.end();)
            {
                if (!obj::IsLive(it->widget))
                {
                    it = g_mashes.erase(it);
                    continue;
                }
                Mash& mash = *it;
                if (!mash.announced && ++mash.polls <= 60) Read(mash);
                UObject* data = nullptr;
                if (!mash.finished && obj::ReadObject(mash.widget, L"InfoDataInstance", data) && obj::IsLive(data))
                {
                    int64_t state = 0;
                    double fraction = 0.0;
                    obj::ReadInt(data, L"ButtonMashState", state);
                    obj::ReadFloat(data, L"CommitFraction", fraction);
                    if (state == 0)
                    {
                        // The ring grows with the presses and shrinks while they stop: a blip
                        // follows it whenever it has moved.
                        if (fraction != mash.lastFraction && now - mash.lastTickAt >= 0.2)
                        {
                            sounds::Tick(fraction);
                            mash.lastFraction = fraction;
                            mash.lastTickAt = now;
                        }
                    }
                    else
                    {
                        mash.finished = true;
                        const bool success = state == 1;
                        log::Info(L"mash: {} {} at {:.2f}", obj::ObjectName(mash.widget), success ? L"succeeded" : L"failed", fraction);
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

    void ButtonMashFeature::Install()
    {
        hooks::OnScript(L"ButtonMashWidgetSMG026_C", L"Construct", &OnConstruct);
        watch::AddHudListener(&OnHud);
        gamethread::AddPoller(L"mash", &Poll);
    }

    void ButtonMashFeature::Describe(std::vector<std::wstring>& out)
    {
        for (const auto& mash : g_mashes)
        {
            if (obj::IsLive(mash.widget) && mash.announced && !mash.finished && !mash.text.empty()) out.push_back(mash.text);
        }
    }

    void ButtonMashFeature::Help(std::vector<std::wstring>& out)
    {
        for (const auto& mash : g_mashes)
        {
            if (!obj::IsLive(mash.widget) || mash.finished) continue;
            const auto key = Key(mash.widget, ReadInfo(mash.widget).action);
            if (!key.empty()) out.push_back(locale::Mod(L"help.mash", key));
            return;
        }
    }
}
