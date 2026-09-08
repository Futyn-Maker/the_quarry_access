#include "watch/Watchers.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"

#include <map>
#include <set>
#include <vector>

namespace qa::watch
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UClass;

    namespace
    {
        // ---- HUD instance watcher ----
        struct HudSlot
        {
            UObject* hud = nullptr;
            std::wstring hudClass;
            FProperty* property = nullptr;
            std::wstring propertyName;
            bool isArray = false;
        };
        struct HudState
        {
            UObject* instance = nullptr;
            bool visible = false;
        };

        std::vector<HudListener> g_hudListeners;
        UObject* g_watchedController = nullptr;
        std::vector<HudSlot> g_slots;
        std::map<std::pair<UObject*, FProperty*>, HudState> g_hudState;
        std::map<std::pair<UObject*, FProperty*>, std::set<UObject*>> g_hudArrayState;
        unsigned long long g_lastSlotScan = 0;

        void Emit(const HudSlot& slot, UObject* instance, bool appeared)
        {
            HudEvent ev;
            ev.hud = slot.hud;
            ev.hudClass = slot.hudClass;
            ev.property = slot.propertyName;
            ev.instance = instance;
            ev.instanceClass = (appeared && instance) ? obj::ClassName(instance) : std::wstring();
            ev.appeared = appeared;
            log::Verbose(L"hud: {} {}.{} -> {} {}", appeared ? L"appeared" : L"gone", slot.hudClass, slot.propertyName, ev.instanceClass,
                         appeared ? obj::ObjectName(instance) : std::wstring());
            for (auto& listener : g_hudListeners)
            {
                try
                {
                    listener(ev);
                }
                catch (...)
                {
                    log::Error(L"hud listener failed");
                }
            }
        }

        void RescanSlots(UObject* controller)
        {
            g_slots.clear();
            for (const wchar_t* componentName : {L"MainHUDComponentSMG026", L"MainHUDComponent"})
            {
                UObject* component = nullptr;
                if (!obj::ReadObject(controller, componentName, component) || !component) continue;
                UClass* compClass = component->GetClassPrivate();
                if (!compClass) continue;
                for (auto* prop : compClass->ForEachPropertyInChain())
                {
                    if (!prop || obj::PropertyTypeName(prop) != L"ObjectProperty") continue;
                    UObject* hud = nullptr;
                    if (!obj::ReadObjectAt(component, prop, hud) || !hud) continue;
                    if (!obj::IsA(hud, L"ActionHUDBase")) continue;
                    UClass* hudClass = hud->GetClassPrivate();
                    const std::wstring hudClassName = obj::ClassName(hud);
                    for (auto* hp : hudClass->ForEachPropertyInChain())
                    {
                        if (!hp) continue;
                        const auto type = obj::PropertyTypeName(hp);
                        const auto name = hp->GetName();
                        const bool looksInstance = str::EndsWith(name, L"Instance") || str::EndsWith(name, L"Instances") || name == L"CachedLoadingScreen";
                        if (!looksInstance) continue;
                        if (type == L"ObjectProperty" || type == L"WeakObjectProperty")
                        {
                            if (type == L"WeakObjectProperty") continue; // not readable as a raw pointer
                            g_slots.push_back(HudSlot{hud, hudClassName, hp, name, false});
                        }
                        else if (type == L"ArrayProperty")
                        {
                            g_slots.push_back(HudSlot{hud, hudClassName, hp, name, true});
                        }
                    }
                }
            }
            log::Info(L"hud watcher: {} instance slots on {}", g_slots.size(), obj::ObjectName(controller));
        }

        void PollHud(float)
        {
            UObject* controller = obj::LocalPlayerController();
            if (!controller)
            {
                if (g_watchedController)
                {
                    g_watchedController = nullptr;
                    g_slots.clear();
                    g_hudState.clear();
                    g_hudArrayState.clear();
                }
                return;
            }
            if (controller != g_watchedController || (g_slots.empty() && gamethread::FrameCount() - g_lastSlotScan > 120))
            {
                g_watchedController = controller;
                g_hudState.clear();
                g_hudArrayState.clear();
                g_lastSlotScan = gamethread::FrameCount();
                RescanSlots(controller);
            }
            for (const auto& slot : g_slots)
            {
                if (!obj::IsLive(slot.hud)) continue;
                const auto key = std::make_pair(slot.hud, slot.property);
                if (!slot.isArray)
                {
                    UObject* instance = nullptr;
                    obj::ReadObjectAt(slot.hud, slot.property, instance);
                    bool visible = instance && obj::IsLive(instance) && obj::IsWidgetVisible(instance);
                    if (!instance || !obj::IsLive(instance))
                    {
                        instance = nullptr;
                        visible = false;
                    }
                    auto& state = g_hudState[key];
                    if (state.instance != instance || state.visible != visible)
                    {
                        if (state.instance && state.visible) Emit(slot, state.instance, false);
                        if (instance && visible) Emit(slot, instance, true);
                        state.instance = instance;
                        state.visible = visible;
                    }
                }
                else
                {
                    std::vector<UObject*> items;
                    auto* arr = slot.property->ContainerPtrToValuePtr<RC::Unreal::TArray<UObject*>>(slot.hud);
                    if (arr && arr->Num() > 0 && arr->Num() < 256 && arr->GetData())
                    {
                        for (int32_t i = 0; i < arr->Num(); ++i)
                        {
                            UObject* item = arr->GetData()[i];
                            if (item && obj::IsLive(item) && obj::IsWidgetVisible(item)) items.push_back(item);
                        }
                    }
                    auto& known = g_hudArrayState[key];
                    std::set<UObject*> current(items.begin(), items.end());
                    for (auto* old : known)
                    {
                        if (!current.contains(old)) Emit(slot, old, false);
                    }
                    for (auto* item : current)
                    {
                        if (!known.contains(item)) Emit(slot, item, true);
                    }
                    known = std::move(current);
                }
            }
        }

        // ---- text watcher ----
        struct TextWatch
        {
            int id = 0;
            UObject* widget = nullptr;
            TextListener listener;
            std::wstring last;
            bool initialized = false;
        };
        std::vector<TextWatch> g_textWatches;
        int g_nextTextId = 1;

        void PollText(float)
        {
            if (gamethread::FrameCount() % 2 != 0) return;
            for (auto it = g_textWatches.begin(); it != g_textWatches.end();)
            {
                if (!obj::IsLive(it->widget))
                {
                    it = g_textWatches.erase(it);
                    continue;
                }
                const auto text = str::StripMarkup(obj::TextOf(it->widget));
                if (!it->initialized || text != it->last)
                {
                    const bool fire = it->initialized || !text.empty();
                    it->initialized = true;
                    it->last = text;
                    if (fire)
                    {
                        try
                        {
                            it->listener(it->widget, text);
                        }
                        catch (...)
                        {
                            log::Error(L"text listener failed");
                        }
                    }
                }
                ++it;
            }
        }

        // ---- focus watcher ----
        std::vector<FocusListener> g_focusListeners;
        UObject* g_screen = nullptr;
        UObject* g_focused = nullptr;
        std::vector<UObject*> g_screenCandidates;
        unsigned long long g_lastScreenScan = 0;

        void PollFocus(float)
        {
            const auto frame = gamethread::FrameCount();
            if (frame % 3 != 0) return;
            if (frame - g_lastScreenScan > 45 || g_screenCandidates.empty())
            {
                g_lastScreenScan = frame;
                g_screenCandidates = obj::FindAllLive(L"SMGUIWidget");
            }
            UObject* screen = nullptr;
            UObject* focused = nullptr;
            for (auto* candidate : g_screenCandidates)
            {
                if (!obj::IsLive(candidate) || !obj::IsWidgetShown(candidate)) continue;
                UObject* last = nullptr;
                obj::ReadObject(candidate, L"LastFocusedWidget", last);
                if (last && obj::IsLive(last) && obj::IsWidgetShown(last))
                {
                    screen = candidate;
                    focused = last;
                    break;
                }
                if (!screen) screen = candidate;
            }
            if (screen != g_screen || focused != g_focused)
            {
                g_screen = screen;
                g_focused = focused;
                log::Verbose(L"focus: screen={} focused={} {}", obj::ClassName(screen), obj::ClassName(focused), obj::ObjectName(focused));
                for (auto& listener : g_focusListeners)
                {
                    try
                    {
                        listener(screen, focused);
                    }
                    catch (...)
                    {
                        log::Error(L"focus listener failed");
                    }
                }
            }
        }
    }

    void AddHudListener(HudListener listener)
    {
        g_hudListeners.push_back(std::move(listener));
    }

    std::vector<std::pair<std::wstring, UObject*>> LiveHudInstances()
    {
        std::vector<std::pair<std::wstring, UObject*>> out;
        for (const auto& [key, state] : g_hudState)
        {
            if (state.instance && state.visible && obj::IsLive(state.instance))
            {
                for (const auto& slot : g_slots)
                {
                    if (slot.hud == key.first && slot.property == key.second)
                    {
                        out.emplace_back(slot.hudClass + L"." + slot.propertyName, state.instance);
                        break;
                    }
                }
            }
        }
        for (const auto& [key, items] : g_hudArrayState)
        {
            for (auto* item : items)
            {
                if (!obj::IsLive(item)) continue;
                for (const auto& slot : g_slots)
                {
                    if (slot.hud == key.first && slot.property == key.second)
                    {
                        out.emplace_back(slot.hudClass + L"." + slot.propertyName, item);
                        break;
                    }
                }
            }
        }
        return out;
    }

    int WatchText(UObject* textWidget, TextListener listener, bool fireInitial)
    {
        TextWatch w;
        w.id = g_nextTextId++;
        w.widget = textWidget;
        w.listener = std::move(listener);
        if (!fireInitial)
        {
            w.last = str::StripMarkup(obj::TextOf(textWidget));
            w.initialized = true;
        }
        g_textWatches.push_back(std::move(w));
        return g_textWatches.back().id;
    }

    void UnwatchText(int id)
    {
        for (auto it = g_textWatches.begin(); it != g_textWatches.end(); ++it)
        {
            if (it->id == id)
            {
                g_textWatches.erase(it);
                return;
            }
        }
    }

    void AddFocusListener(FocusListener listener)
    {
        g_focusListeners.push_back(std::move(listener));
    }

    UObject* CurrentScreen()
    {
        return obj::IsLive(g_screen) ? g_screen : nullptr;
    }

    UObject* CurrentFocused()
    {
        return obj::IsLive(g_focused) ? g_focused : nullptr;
    }

    void Install()
    {
        gamethread::AddPoller(L"hud", &PollHud);
        gamethread::AddPoller(L"text", &PollText);
        gamethread::AddPoller(L"focus", &PollFocus);
        log::Info(L"watchers installed");
    }

    void Reset()
    {
        g_watchedController = nullptr;
        g_slots.clear();
        g_hudState.clear();
        g_hudArrayState.clear();
        g_textWatches.clear();
        g_screenCandidates.clear();
        g_screen = nullptr;
        g_focused = nullptr;
    }
}
