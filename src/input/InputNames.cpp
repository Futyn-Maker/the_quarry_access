#include "input/InputNames.hpp"

#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "locale/Locale.hpp"
#include "watch/Watchers.hpp"

#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>

namespace qa::input
{
    using RC::Unreal::FName;
    using RC::Unreal::FString;
    using RC::Unreal::FText;
    using RC::Unreal::TArray;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    namespace
    {
        std::mutex g_mutex;
        std::map<std::pair<int, std::wstring>, std::wstring> g_actionCache;
        std::map<std::wstring, std::wstring> g_keyNameCache;
        UObject* g_widget = nullptr; // any live USMGUIUserWidgetBase for GetKeysFromActionMapping

        // Size of one FKey, taken from the engine's own reflection rather than assumed,
        // so the returned key array is walked with the right stride.
        size_t FKeyStride()
        {
            static size_t cached = 0;
            if (cached) return cached;
            cached = 0x18;
            if (auto* lib = obj::FindObject(L"/Script/Engine.Default__KismetInputLibrary"))
            {
                if (auto* fn = obj::FindFunction(lib, L"Key_GetDisplayName"))
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Key" && prop->GetSize() > 0)
                        {
                            cached = static_cast<size_t>(prop->GetSize());
                            break;
                        }
                    }
                }
            }
            return cached;
        }

        // The widget used to ask the game which keys an action is bound to. Only the screen
        // currently on display is used: the native helper dereferences the widget's owning
        // player, which is not safe on pooled or half-torn-down widgets.
        UObject* AnyUiWidget()
        {
            UObject* screen = watch::CurrentScreen();
            if (screen && obj::IsLive(screen) && obj::FindFunction(screen, L"GetKeysFromActionMapping"))
            {
                if (screen != g_widget)
                {
                    g_widget = screen;
                    log::Verbose(L"input: key-mapping widget {} {}", obj::ClassName(g_widget), obj::ObjectName(g_widget));
                }
                return g_widget;
            }
            // Screens without a focused control (the title screen) still need key names.
            for (auto* candidate : watch::CurrentScreens())
            {
                if (obj::FindFunction(candidate, L"GetKeysFromActionMapping"))
                {
                    g_widget = candidate;
                    return g_widget;
                }
            }
            if (g_widget && obj::IsLive(g_widget)) return g_widget;
            g_widget = nullptr;
            return nullptr;
        }

        std::wstring Humanize(std::wstring_view keyName)
        {
            std::wstring s(keyName);
            if (str::StartsWith(s, L"Gamepad_")) s.erase(0, 8);
            // Split CamelCase and underscores into words.
            std::wstring out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                const wchar_t c = s[i];
                if (c == L'_')
                {
                    out += L' ';
                    continue;
                }
                if (i > 0 && iswupper(c) && iswlower(s[i - 1])) out += L' ';
                out += c;
            }
            return str::CollapseWhitespace(out);
        }

        std::wstring EngineDisplayName(std::wstring_view keyName)
        {
            auto* lib = obj::FindObject(L"/Script/Engine.Default__KismetInputLibrary");
            if (!lib) return {};
            auto* fn = obj::FindFunction(lib, L"Key_GetDisplayName");
            if (!fn) return {};
            std::wstring result;
            const std::wstring name(keyName);
            obj::Call(
                lib, fn,
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"Key")
                        {
                            auto* key = prop->ContainerPtrToValuePtr<uint8_t>(params);
                            std::construct_at(reinterpret_cast<FName*>(key), name.c_str(), RC::Unreal::FNAME_Add);
                        }
                    }
                },
                [&](void* params)
                {
                    for (auto* prop : fn->ForEachProperty())
                    {
                        if (prop && prop->GetName() == L"ReturnValue") obj::ReadStringAt(params, prop, result);
                    }
                });
            return result;
        }
    }

    Scheme CurrentScheme()
    {
        auto* pc = obj::LocalPlayerController();
        if (!pc) return Scheme::Unknown;
        int64_t scheme = 0;
        if (!obj::ReadInt(pc, L"CurrentControlScheme", scheme)) return Scheme::Unknown;
        switch (scheme)
        {
        case 1: return Scheme::Gamepad;
        case 2: return Scheme::MouseKeyboard;
        case 3: return Scheme::Touch;
        default: return Scheme::Unknown;
        }
    }

    std::wstring SchemeName(Scheme scheme)
    {
        switch (scheme)
        {
        case Scheme::Gamepad: return L"Gamepad";
        case Scheme::MouseKeyboard: return L"MouseKeyboard";
        case Scheme::Touch: return L"Touch";
        default: return L"Unknown";
        }
    }

    std::wstring KeyDisplayName(std::wstring_view keyName)
    {
        if (keyName.empty() || keyName == L"None") return {};
        const std::wstring name(keyName);
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_keyNameCache.find(name);
            if (it != g_keyNameCache.end()) return it->second;
        }
        // 1. Mod table (localized, curated names).
        const std::wstring localeKey = L"key." + str::ToLower(name);
        std::wstring result;
        if (locale::Has(localeKey)) result = locale::Mod(localeKey);
        // 2. Engine display name.
        if (result.empty()) result = EngineDisplayName(name);
        // 3. Humanized engine key name.
        if (result.empty()) result = Humanize(name);
        std::lock_guard lock(g_mutex);
        g_keyNameCache[name] = result;
        return result;
    }

    std::wstring KeyForAction(std::wstring_view actionName)
    {
        const Scheme scheme = CurrentScheme();
        const std::wstring action(actionName);
        const auto cacheKey = std::make_pair(static_cast<int>(scheme), action);
        {
            std::lock_guard lock(g_mutex);
            const auto it = g_actionCache.find(cacheKey);
            if (it != g_actionCache.end()) return it->second;
        }
        auto* widget = AnyUiWidget();
        if (!widget)
        {
            log::Verbose(L"input: no owned UI widget yet; cannot resolve {}", action);
            return {};
        }
        auto* fn = obj::FindFunction(widget, L"GetKeysFromActionMapping");
        if (!fn) return {};
        log::Verbose(L"input: resolving {} via {} {} (params {} bytes)", action, obj::ClassName(widget), obj::ObjectName(widget), fn->GetPropertiesSize());

        std::vector<std::wstring> keys;
        obj::Call(
            widget, fn,
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (!prop) continue;
                    if (obj::PropertyTypeName(prop) == L"StructProperty" &&
                        !prop->HasAnyPropertyFlags(static_cast<uint64_t>(RC::Unreal::EPropertyFlags::CPF_ReturnParm)))
                    {
                        // FActionMappingReference { FString Value; }
                        auto* value = prop->ContainerPtrToValuePtr<FString>(params);
                        std::construct_at(value, action.c_str());
                    }
                }
            },
            [&](void* params)
            {
                for (auto* prop : fn->ForEachProperty())
                {
                    if (!prop || prop->GetName() != L"ReturnValue") continue;
                    auto* arr = prop->ContainerPtrToValuePtr<TArray<uint8_t>>(params);
                    if (!arr || arr->Num() <= 0 || !arr->GetData()) continue;
                    const int32_t n = std::min<int32_t>(arr->Num(), 16);
                    for (int32_t i = 0; i < n; ++i)
                    {
                        auto* key = reinterpret_cast<FName*>(arr->GetData() + static_cast<size_t>(i) * FKeyStride());
                        keys.push_back(key->ToString());
                    }
                }
            });

        std::wstring chosen;
        const bool wantPad = scheme == Scheme::Gamepad;
        for (const auto& k : keys)
        {
            const bool isPad = str::StartsWith(k, L"Gamepad_");
            const bool isTouch = str::StartsWith(k, L"Touch");
            if (isTouch) continue;
            if (isPad == wantPad)
            {
                chosen = k;
                break;
            }
        }
        if (chosen.empty() && !keys.empty()) chosen = keys.front();
        const std::wstring display = KeyDisplayName(chosen);
        log::Verbose(L"input: action {} -> keys [{}] -> \"{}\" (scheme {})", action, str::Join(keys, L", "), display, SchemeName(scheme));
        std::lock_guard lock(g_mutex);
        g_actionCache[cacheKey] = display;
        return display;
    }

    std::wstring PointerName()
    {
        return locale::Mod(CurrentScheme() == Scheme::Gamepad ? L"input.rightstick" : L"input.mouse");
    }

    void InvalidateCache()
    {
        std::lock_guard lock(g_mutex);
        g_actionCache.clear();
        g_keyNameCache.clear();
        g_widget = nullptr;
    }

    // ---- input activity ----------------------------------------------------

    namespace
    {
        std::atomic<long long> g_lastInputAt{0}; // steady clock milliseconds; 0 = nothing yet
        POINT g_lastCursor{};
        bool g_haveCursor = false;

        long long NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        bool GameWindowInForeground()
        {
            HWND foreground = GetForegroundWindow();
            if (!foreground) return false;
            DWORD pid = 0;
            GetWindowThreadProcessId(foreground, &pid);
            return pid == GetCurrentProcessId();
        }

        // Everything the game can be driven with, including rebindable keys. A key that is
        // held down counts too, so holding a direction to scroll a menu stays "activity",
        // and so does a tap released between two polls (the low bit of the key state) when
        // the mask asks for it.
        bool AnyKeyDown(int mask)
        {
            static constexpr std::pair<int, int> kRanges[] = {
                {VK_LBUTTON, VK_XBUTTON2},
                {VK_BACK, VK_TAB},
                {VK_RETURN, VK_RETURN},
                {VK_SHIFT, VK_MENU},
                {VK_ESCAPE, VK_ESCAPE},
                {VK_SPACE, VK_HELP},
                {'0', '9'},
                {'A', 'Z'},
                {VK_NUMPAD0, VK_DIVIDE},
                {VK_F1, VK_F24},
                {VK_OEM_1, VK_OEM_102},
            };
            for (const auto& [first, last] : kRanges)
            {
                for (int vk = first; vk <= last; ++vk)
                {
                    if (GetAsyncKeyState(vk) & mask) return true;
                }
            }
            return false;
        }

        struct XInputGamepad
        {
            uint16_t buttons;
            uint8_t leftTrigger;
            uint8_t rightTrigger;
            int16_t thumbLX;
            int16_t thumbLY;
            int16_t thumbRX;
            int16_t thumbRY;
        };
        struct XInputState
        {
            uint32_t packetNumber;
            XInputGamepad gamepad;
        };
        using XInputGetStateFn = uint32_t(__stdcall*)(uint32_t, XInputState*);

        XInputGetStateFn XInput()
        {
            static XInputGetStateFn function = nullptr;
            static bool resolved = false;
            if (resolved) return function;
            resolved = true;
            for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"})
            {
                HMODULE module = GetModuleHandleW(name);
                if (!module) module = LoadLibraryW(name);
                if (!module) continue;
                function = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
                if (function) break;
            }
            log::Info(L"input: gamepad activity {}", function ? L"tracked" : L"unavailable");
            return function;
        }

        // Asking an empty controller slot for its state is slow, so slots are only
        // re-checked once a second until one answers.
        bool AnyPadInput()
        {
            auto* getState = XInput();
            if (!getState) return false;
            static bool connected[4] = {true, true, true, true};
            static long long nextScanAt = 0;
            const long long now = NowMs();
            const bool scanAll = now >= nextScanAt;
            if (scanAll) nextScanAt = now + 1000;
            constexpr int16_t kDeadZone = 12000;
            bool active = false;
            for (uint32_t pad = 0; pad < 4; ++pad)
            {
                if (!connected[pad] && !scanAll) continue;
                XInputState state{};
                connected[pad] = getState(pad, &state) == ERROR_SUCCESS;
                if (!connected[pad]) continue;
                const auto& g = state.gamepad;
                if (g.buttons != 0 || g.leftTrigger > 40 || g.rightTrigger > 40) active = true;
                if (g.thumbLX > kDeadZone || g.thumbLX < -kDeadZone || g.thumbLY > kDeadZone || g.thumbLY < -kDeadZone) active = true;
                if (g.thumbRX > kDeadZone || g.thumbRX < -kDeadZone || g.thumbRY > kDeadZone || g.thumbRY < -kDeadZone) active = true;
            }
            return active;
        }

        bool CursorMoved()
        {
            POINT cursor{};
            if (!GetCursorPos(&cursor)) return false;
            const bool moved = g_haveCursor && (cursor.x != g_lastCursor.x || cursor.y != g_lastCursor.y);
            g_lastCursor = cursor;
            g_haveCursor = true;
            return moved;
        }

        void PollActivity(float)
        {
            if (!GameWindowInForeground()) return;
            if (AnyKeyDown(0x8001) || CursorMoved() || AnyPadInput()) g_lastInputAt.store(NowMs(), std::memory_order_relaxed);
        }
    }

    void InstallActivityTracker()
    {
        gamethread::AddPoller(L"input.activity", &PollActivity);
    }

    long long MsSinceInput()
    {
        const long long last = g_lastInputAt.load(std::memory_order_relaxed);
        if (last == 0) return 1'000'000;
        return NowMs() - last;
    }

    bool InputHeld()
    {
        return GameWindowInForeground() && (AnyKeyDown(0x8000) || AnyPadInput());
    }
}
